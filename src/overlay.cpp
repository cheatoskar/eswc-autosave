// The menu overlay (see overlay.h). Direct3D hook modelled on the Horizon Client's src/host/d3dhook.cpp, the
// input hold on its src/host/dinput.cpp: method-table slots and import slots only, never d3d9 or dinput8 code.
#define DIRECTINPUT_VERSION 0x0800
#include "overlay.h"

#include <d3d9.h>
#include <dinput.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../vendor/horizon/imgui/imgui.h"
#include "../vendor/horizon/imgui/backends/imgui_impl_dx9.h"
#include "../vendor/horizon/imgui/backends/imgui_impl_win32.h"
#include "log.h"
#include "logo_rgba.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace autosave {
namespace overlay {
namespace {

// ---------------------------------------------------------------- memory helpers
bool writePointer(void** slot, void* value) {
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

bool readPointer(const void* address, void** out) {
    __try {
        *out = *static_cast<void* const*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The exe's import slot of dll!function. Exes without a name table (the Sunrise repack) are searched by the
// address the loader put into the slot.
bool patchImport(HMODULE module, const char* dll, const char* function, void* replacement, void** originalOut) {
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; imp++) {
        if (_stricmp(reinterpret_cast<const char*>(base + imp->Name), dll) != 0) continue;
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        if (!imp->OriginalFirstThunk) {
            HMODULE m = GetModuleHandleA(dll);
            void* real = m ? reinterpret_cast<void*>(GetProcAddress(m, function)) : nullptr;
            if (!real) return false;
            for (; slots->u1.Function; slots++) {
                if (reinterpret_cast<void*>(slots->u1.Function) != real) continue;
                *originalOut = real;
                return writePointer(reinterpret_cast<void**>(&slots->u1.Function), replacement);
            }
            return false;
        }
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* named = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(named->Name), function) != 0) continue;
            *originalOut = reinterpret_cast<void*>(slots->u1.Function);
            return writePointer(reinterpret_cast<void**>(&slots->u1.Function), replacement);
        }
    }
    return false;
}

// "d3d9.dll+0x1234" for the log
void describeAddress(const void* address, char* out, size_t size) {
    HMODULE m = nullptr;
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      static_cast<LPCWSTR>(address), &m) && m) {
        wchar_t path[MAX_PATH];
        const DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t* name = n ? wcsrchr(path, L'\\') : nullptr;
        name = name ? name + 1 : path;
        char narrow[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr);
        _snprintf_s(out, size, _TRUNCATE, "%s+0x%x", narrow,
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(m)));
    } else {
        _snprintf_s(out, size, _TRUNCATE, "%p", address);
    }
}

// ---------------------------------------------------------------- state
using Create9Fn = IDirect3D9*(WINAPI*)(UINT);
using GetProcFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using CreateDeviceFn = HRESULT(APIENTRY*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using PresentFn = HRESULT(APIENTRY*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFn = HRESULT(APIENTRY*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
constexpr int kCreateDevice = 16, kReset = 16, kPresent = 17;

struct FactoryTable {
    void** table = nullptr;
    CreateDeviceFn next = nullptr, first = nullptr;
};
struct DeviceTable {
    void** table = nullptr;
    PresentFn nextPresent = nullptr, firstPresent = nullptr;
    ResetFn nextReset = nullptr, firstReset = nullptr;
};
constexpr int kMaxTables = 4;
FactoryTable g_factories[kMaxTables];
volatile LONG g_factoryCount = 0;
DeviceTable g_devices[kMaxTables];
volatile LONG g_deviceCount = 0;
volatile LONG g_rewrites = 0;
Create9Fn g_importCreate9 = nullptr;
Create9Fn g_dynCreate9 = nullptr;
GetProcFn g_getProc = nullptr;
IDirect3DDevice9* volatile g_game = nullptr;
int g_inCreate = 0, g_inPresent = 0, g_inReset = 0; // game thread only

bool g_installed = false;
Hotkey g_key;
DrawMenuFn g_draw = nullptr;
HWND g_hwnd = nullptr;
WNDPROC g_origProc = nullptr;
ImGuiContext* g_ctx = nullptr;
bool g_backends = false;
bool g_menu = false;
bool g_swallowed[3] = {};
float g_scale = 1.0f;
bool g_faulted = false;

// toasts (any thread -> game thread)
SRWLOCK g_toastLock = SRWLOCK_INIT;
struct Toast {
    char text[160];
    DWORD until;
};
Toast g_toasts[3];
int g_toastCount = 0;
constexpr DWORD kToastMs = 3500;

// ---------------------------------------------------------------- DirectInput hold (menu open)
using GetStateFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPVOID);
using GetDataFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using CapsFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, LPDIDEVCAPS);
using Create8Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
constexpr int kGetCaps = 3, kGetState = 9, kGetData = 10;

struct InputTable {
    void** table = nullptr;
    GetStateFn state = nullptr;
    GetDataFn data = nullptr;
    CapsFn caps = nullptr;
};
InputTable g_inTables[4];
LONG g_inTableCount = 0;
volatile LONG g_blocked = 0;
volatile LONG g_holdDik = -1; // a key (DirectInput scan code) hidden from the game until it is released
volatile LONG g_holdVk = 0;
struct Kind {
    void* dev;
    int kind; // 1 keyboard, 2 mouse, 3 other (never touched)
};
Kind g_kinds[32];
LONG g_kindCount = 0;

InputTable* inTableOf(IDirectInputDevice8W* dev) {
    void** t = *reinterpret_cast<void***>(dev);
    for (LONG i = 0; i < g_inTableCount; i++)
        if (g_inTables[i].table == t) return &g_inTables[i];
    return &g_inTables[0];
}

int kindOf(IDirectInputDevice8W* dev, InputTable* t) {
    for (LONG i = 0; i < g_kindCount; i++)
        if (g_kinds[i].dev == dev) return g_kinds[i].kind;
    DIDEVCAPS caps = {};
    caps.dwSize = sizeof(caps);
    int kind = 3;
    if (t->caps && SUCCEEDED(t->caps(dev, &caps))) {
        const DWORD type = GET_DIDEVICE_TYPE(caps.dwDevType);
        if (type == DI8DEVTYPE_KEYBOARD) kind = 1;
        else if (type == DI8DEVTYPE_MOUSE) kind = 2;
    }
    if (g_kindCount < 32) g_kinds[g_kindCount++] = Kind{dev, kind};
    return kind;
}

LONG heldDik() {
    const LONG dik = g_holdDik;
    if (dik < 0) return -1;
    if (!(GetAsyncKeyState(static_cast<int>(g_holdVk)) & 0x8000)) {
        InterlockedExchange(&g_holdDik, -1);
        return -1;
    }
    return dik;
}

HRESULT STDMETHODCALLTYPE getStateDetour(IDirectInputDevice8W* dev, DWORD size, LPVOID data) {
    InputTable* t = inTableOf(dev);
    const HRESULT hr = t->state(dev, size, data);
    if (FAILED(hr) || !data) return hr;
    const int kind = kindOf(dev, t);
    if (kind == 3) return hr;
    if (g_blocked) {
        memset(data, 0, size);
    } else if (kind == 1 && size == 256) {
        const LONG dik = heldDik();
        if (dik >= 0) static_cast<BYTE*>(data)[dik] = 0;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE getDataDetour(IDirectInputDevice8W* dev, DWORD objSize, LPDIDEVICEOBJECTDATA data, LPDWORD inOut, DWORD flags) {
    InputTable* t = inTableOf(dev);
    const HRESULT hr = t->data(dev, objSize, data, inOut, flags);
    if (FAILED(hr) || !inOut) return hr;
    const int kind = kindOf(dev, t);
    if (kind == 3) return hr;
    if (g_blocked) {
        *inOut = 0; // read and dropped: nothing piles up for after the menu
        return hr;
    }
    const LONG dik = kind == 1 ? heldDik() : -1;
    if (dik >= 0 && data && objSize >= sizeof(DIDEVICEOBJECTDATA)) {
        DWORD kept = 0;
        for (DWORD i = 0; i < *inOut; i++) {
            auto* e = reinterpret_cast<DIDEVICEOBJECTDATA*>(reinterpret_cast<BYTE*>(data) + i * objSize);
            if (static_cast<LONG>(e->dwOfs) == dik) continue;
            if (kept != i) memcpy(reinterpret_cast<BYTE*>(data) + kept * objSize, e, objSize);
            kept++;
        }
        *inOut = kept;
    }
    return hr;
}

void hookInputTable(IDirectInputDevice8W* dev, const char* what) {
    void** table = *reinterpret_cast<void***>(dev);
    for (LONG i = 0; i < g_inTableCount; i++)
        if (g_inTables[i].table == table) return;
    if (g_inTableCount >= 4) return;
    InputTable& t = g_inTables[g_inTableCount];
    t.table = table;
    t.state = reinterpret_cast<GetStateFn>(table[kGetState]);
    t.data = reinterpret_cast<GetDataFn>(table[kGetData]);
    t.caps = reinterpret_cast<CapsFn>(table[kGetCaps]);
    g_inTableCount++;
    writePointer(&table[kGetState], reinterpret_cast<void*>(&getStateDetour));
    writePointer(&table[kGetData], reinterpret_cast<void*>(&getDataDetour));
    char where[160];
    describeAddress(table, where, sizeof(where));
    AS_LOG("menu: DirectInput %s table at %s hooked (empty reads while the menu is open)", what, where);
}

void installInputHold() {
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 16) return;
    wcscat_s(path, L"\\dinput8.dll");
    HMODULE sys = LoadLibraryW(path);
    auto create = sys ? reinterpret_cast<Create8Fn>(GetProcAddress(sys, "DirectInput8Create")) : nullptr;
    if (!create) {
        AS_LOG("menu: system dinput8.dll not usable: the game keeps its keyboard and mouse while the menu is open");
        return;
    }
    const IID* iids[2] = {&IID_IDirectInput8W, &IID_IDirectInput8A};
    for (int k = 0; k < 2; k++) {
        IDirectInput8W* di = nullptr;
        if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, *iids[k], reinterpret_cast<void**>(&di), nullptr)) || !di) continue;
        const GUID* guids[2] = {&GUID_SysKeyboard, &GUID_SysMouse};
        for (int g = 0; g < 2; g++) {
            IDirectInputDevice8W* dev = nullptr;
            if (SUCCEEDED(di->CreateDevice(*guids[g], &dev, nullptr)) && dev) {
                hookInputTable(dev, k == 0 ? (g == 0 ? "keyboard (W)" : "mouse (W)") : (g == 0 ? "keyboard (A)" : "mouse (A)"));
                dev->Release();
            }
        }
        di->Release();
    }
}

void holdUntilReleased(UINT vk) {
    const UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    if (!sc) return;
    InterlockedExchange(&g_holdVk, static_cast<LONG>(vk));
    InterlockedExchange(&g_holdDik, static_cast<LONG>((sc & 0xff) | ((sc & 0xff00) ? 0x80 : 0)));
}

void setBlocked(bool on) {
    const LONG v = on ? 1 : 0;
    if (InterlockedExchange(&g_blocked, v) != v) AS_LOG("menu: %s", on ? "open (game keyboard and mouse held back)" : "closed");
}

// ---------------------------------------------------------------- window
bool isKeyMsg(UINT m) { return m == WM_KEYDOWN || m == WM_KEYUP || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP || m == WM_CHAR; }
bool isMouseMsg(UINT m) { return m >= WM_MOUSEFIRST && m <= WM_MOUSELAST; }
int buttonOf(UINT m, bool& up) {
    up = m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP;
    if (m == WM_LBUTTONDOWN || m == WM_LBUTTONUP || m == WM_LBUTTONDBLCLK) return 0;
    if (m == WM_RBUTTONDOWN || m == WM_RBUTTONUP || m == WM_RBUTTONDBLCLK) return 1;
    if (m == WM_MBUTTONDOWN || m == WM_MBUTTONUP || m == WM_MBUTTONDBLCLK) return 2;
    return -1;
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(l & (1 << 30)) && !g_faulted) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;
        if (hotkeyMatches(g_key, static_cast<UINT>(w), ctrl, shift, alt)) {
            setMenuOpen(!g_menu);
            holdUntilReleased(static_cast<UINT>(w));
            return 0;
        }
        if (g_menu && w == VK_ESCAPE) {
            setMenuOpen(false);
            holdUntilReleased(VK_ESCAPE);
            return 0;
        }
    }
    if (g_backends && g_ctx) {
        ImGui::SetCurrentContext(g_ctx);
        bool take = false;
        if (g_menu) {
            ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
            take = isKeyMsg(msg) || isMouseMsg(msg);
        }
        bool up = false;
        const int b = buttonOf(msg, up);
        if (b >= 0) {
            // a click that opened inside the menu also ends there (the game never sees half a click)
            if (up) {
                take = take || g_swallowed[b];
                g_swallowed[b] = false;
            } else {
                g_swallowed[b] = take;
            }
        }
        if (take) return 0;
    }
    return CallWindowProcW(g_origProc, h, msg, w, l);
}

// ---------------------------------------------------------------- ImGui
void applyStyle(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 8.0f;
    s.FrameRounding = 5.0f;
    s.GrabRounding = 5.0f;
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 7);
    s.WindowBorderSize = 1.0f;
    ImVec4* c = s.Colors;
    const ImVec4 orange(0.96f, 0.55f, 0.13f, 1.0f);
    const ImVec4 orangeHover(1.00f, 0.64f, 0.25f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.11f, 0.96f);
    c[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f);
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.95f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.58f, 0.63f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.21f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.32f, 1.0f);
    c[ImGuiCol_CheckMark] = orange;
    c[ImGuiCol_SliderGrab] = orange;
    c[ImGuiCol_SliderGrabActive] = orangeHover;
    c[ImGuiCol_HeaderActive] = orange;
    c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.11f, 0.13f, 0.98f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.27f, 0.31f, 1.0f);
    c[ImGuiCol_ButtonActive] = orange;
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.27f, 0.31f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    c[ImGuiCol_NavHighlight] = orangeHover;
    s.ScaleAllSizes(scale);
}

void createContext(HWND window) {
    IMGUI_CHECKVERSION();
    g_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableTooltip = false;
    // scale from the monitor the game is on (the device is often made small first and reset later)
    RECT rc = {};
    GetClientRect(window, &rc);
    long height = rc.bottom - rc.top;
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi) && mi.rcMonitor.bottom - mi.rcMonitor.top > height)
        height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    g_scale = static_cast<float>(height) / 1080.0f;
    if (g_scale < 1.0f) g_scale = 1.0f;
    if (g_scale > 2.0f) g_scale = 2.0f;
    wchar_t font[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(font, MAX_PATH);
    bool loaded = false;
    if (n && n < MAX_PATH - 24) {
        wcscat_s(font, L"\\Fonts\\segoeui.ttf");
        char utf8[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, font, -1, utf8, sizeof(utf8), nullptr, nullptr);
        if (GetFileAttributesW(font) != INVALID_FILE_ATTRIBUTES)
            loaded = io.Fonts->AddFontFromFileTTF(utf8, floorf(18.0f * g_scale)) != nullptr;
    }
    if (!loaded) {
        ImFontConfig cfg;
        cfg.SizePixels = floorf(13.0f * g_scale);
        io.Fonts->AddFontDefault(&cfg);
    }
    applyStyle(g_scale);
}

IDirect3DTexture9* g_logo = nullptr;

// the logo as a managed texture (survives Reset); made again for a new device
void makeLogo(IDirect3DDevice9* dev) {
    if (g_logo) {
        g_logo->Release();
        g_logo = nullptr;
    }
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(kLogoW, kLogoH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex) return;
    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
        tex->Release();
        return;
    }
    for (int y = 0; y < kLogoH; y++) {
        const unsigned char* s = kLogoRgba + y * kLogoW * 4;
        unsigned char* d = static_cast<unsigned char*>(lr.pBits) + y * lr.Pitch;
        for (int x = 0; x < kLogoW; x++) { // RGBA -> BGRA
            d[x * 4 + 0] = s[x * 4 + 2];
            d[x * 4 + 1] = s[x * 4 + 1];
            d[x * 4 + 2] = s[x * 4 + 0];
            d[x * 4 + 3] = s[x * 4 + 3];
        }
    }
    tex->UnlockRect(0);
    g_logo = tex;
}

void deviceCreated(IDirect3DDevice9* dev, HWND window) {
    if (!g_ctx) {
        g_hwnd = window;
        createContext(window);
        ImGui_ImplWin32_Init(window);
        g_origProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndProc)));
        wchar_t key[48];
        formatHotkey(g_key, key, 48);
        char k8[64];
        WideCharToMultiByte(CP_UTF8, 0, key, -1, k8, sizeof(k8), nullptr, nullptr);
        AS_LOG("menu: ready on the game window (scale %.2f), menu key %s", g_scale, k8);
    }
    ImGui::SetCurrentContext(g_ctx);
    if (g_backends) ImGui_ImplDX9_Shutdown();
    g_backends = ImGui_ImplDX9_Init(dev);
    makeLogo(dev);
}

void drawToasts(DWORD now) {
    AcquireSRWLockExclusive(&g_toastLock);
    int n = 0;
    for (int i = 0; i < g_toastCount; i++)
        if (static_cast<LONG>(g_toasts[i].until - now) > 0) g_toasts[n++] = g_toasts[i];
    g_toastCount = n;
    Toast copy[3];
    memcpy(copy, g_toasts, sizeof(Toast) * static_cast<size_t>(n));
    ReleaseSRWLockExclusive(&g_toastLock);
    if (!n) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pad = 16.0f * g_scale;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - pad, vp->WorkPos.y + pad), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##le-toasts", nullptr, f)) {
        for (int i = 0; i < n; i++) ImGui::TextUnformatted(copy[i].text);
    }
    ImGui::End();
}

void render(IDirect3DDevice9* dev) {
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->CmdListsCount == 0) return;
    IDirect3DSurface9* back = nullptr;
    IDirect3DSurface9* oldRt = nullptr;
    bool switched = false;
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && SUCCEEDED(dev->GetRenderTarget(0, &oldRt))) {
        if (oldRt != back) switched = SUCCEEDED(dev->SetRenderTarget(0, back));
    }
    if (SUCCEEDED(dev->BeginScene())) {
        ImGui_ImplDX9_RenderDrawData(dd);
        dev->EndScene();
    }
    if (switched) dev->SetRenderTarget(0, oldRt);
    if (oldRt) oldRt->Release();
    if (back) back->Release();
}

void frame(IDirect3DDevice9* dev) {
    if (!g_backends || !g_ctx || g_faulted) {
        setBlocked(false);
        return;
    }
    const DWORD now = GetTickCount();
    const bool toasts = g_toastCount > 0;
    setBlocked(g_menu);
    if (!g_menu && !toasts) return;
    if (dev->TestCooperativeLevel() != D3D_OK) return;
    ImGui::SetCurrentContext(g_ctx);
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = g_menu;
    if (g_menu && g_draw) {
        bool open = true;
        g_draw(&open);
        if (!open) setMenuOpen(false);
    }
    drawToasts(now);
    ImGui::Render();
    render(dev);
}

bool frameSeh(IDirect3DDevice9* dev) {
    __try {
        frame(dev);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---------------------------------------------------------------- Direct3D hooks
FactoryTable* factoryOf(void** table) {
    for (LONG i = 0; i < g_factoryCount; i++)
        if (g_factories[i].table == table) return &g_factories[i];
    return nullptr;
}
DeviceTable* deviceOf(IDirect3DDevice9* dev) {
    void** table = nullptr;
    if (!readPointer(dev, reinterpret_cast<void**>(&table))) return nullptr;
    for (LONG i = 0; i < g_deviceCount; i++)
        if (g_devices[i].table == table) return &g_devices[i];
    return nullptr;
}

HRESULT APIENTRY presentDetour(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty) {
    DeviceTable* t = deviceOf(dev);
    if (!t) t = &g_devices[0];
    if (g_inPresent) return (t->firstPresent ? t->firstPresent : t->nextPresent)(dev, src, dst, wnd, dirty);
    g_inPresent++;
    if (dev == g_game && !frameSeh(dev)) {
        // never again this start: the game keeps its input and runs on
        g_faulted = true;
        g_menu = false;
        InterlockedExchange(&g_blocked, 0);
        AS_LOG("menu: ERROR: exception while drawing, menu off until the next start");
    }
    const HRESULT hr = t->nextPresent(dev, src, dst, wnd, dirty);
    g_inPresent--;
    return hr;
}

HRESULT APIENTRY resetDetour(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* params) {
    DeviceTable* t = deviceOf(dev);
    if (!t) t = &g_devices[0];
    if (g_inReset) return (t->firstReset ? t->firstReset : t->nextReset)(dev, params);
    if (dev != g_game) return t->nextReset(dev, params);
    g_inReset++;
    if (g_backends && g_ctx) {
        ImGui::SetCurrentContext(g_ctx);
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
    const HRESULT hr = t->nextReset(dev, params);
    g_inReset--;
    static int s_lines = 0;
    if (s_lines++ < 10)
        AS_LOG("menu: Reset -> 0x%08lx (%ux%u)", static_cast<unsigned long>(hr), params ? params->BackBufferWidth : 0,
               params ? params->BackBufferHeight : 0);
    return hr;
}

void hookDevice(IDirect3DDevice9* dev) {
    void** table = nullptr;
    if (!readPointer(dev, reinterpret_cast<void**>(&table)) || !table) return;
    DeviceTable* t = nullptr;
    for (LONG i = 0; i < g_deviceCount; i++)
        if (g_devices[i].table == table) t = &g_devices[i];
    if (!t) {
        if (g_deviceCount >= kMaxTables) return;
        t = &g_devices[g_deviceCount];
        t->table = table;
        t->firstPresent = t->nextPresent = reinterpret_cast<PresentFn>(table[kPresent]);
        t->firstReset = t->nextReset = reinterpret_cast<ResetFn>(table[kReset]);
        InterlockedIncrement(&g_deviceCount);
    }
    char where[160];
    describeAddress(table[kPresent], where, sizeof(where));
    if (table[kPresent] != reinterpret_cast<void*>(&presentDetour)) {
        t->nextPresent = reinterpret_cast<PresentFn>(table[kPresent]);
        writePointer(&table[kPresent], reinterpret_cast<void*>(&presentDetour));
    }
    if (table[kReset] != reinterpret_cast<void*>(&resetDetour)) {
        t->nextReset = reinterpret_cast<ResetFn>(table[kReset]);
        writePointer(&table[kReset], reinterpret_cast<void*>(&resetDetour));
    }
    AS_LOG("menu: game device hooked (Present and Reset in its method table; Present was %s)", where);
}

HRESULT APIENTRY createDeviceDetour(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                    D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** out) {
    void** table = nullptr;
    readPointer(self, reinterpret_cast<void**>(&table));
    FactoryTable* f = factoryOf(table);
    if (!f) f = &g_factories[0];
    if (g_inCreate) return (f->first ? f->first : f->next)(self, adapter, type, window, flags, params, out);
    // the game's device: hardware, with a depth buffer (the games make no other kind)
    const bool gameDevice = type == D3DDEVTYPE_HAL && params && params->EnableAutoDepthStencil;
    g_inCreate++;
    const HRESULT hr = f->next(self, adapter, type, window, flags, params, out);
    g_inCreate--;
    if (gameDevice && SUCCEEDED(hr) && out && *out) {
        const bool first = g_game == nullptr;
        g_game = *out;
        HWND w = window ? window : params->hDeviceWindow;
        hookDevice(*out);
        deviceCreated(*out, w);
        AS_LOG("menu: %s (%ux%u, %s)", first ? "game device created" : "the game made a new device", params->BackBufferWidth,
               params->BackBufferHeight, params->Windowed ? "windowed" : "fullscreen");
    }
    return hr;
}

bool patchFactory(IDirect3D9* factory, const char* why) {
    void** table = nullptr;
    if (!factory || !readPointer(factory, reinterpret_cast<void**>(&table)) || !table) return false;
    void* now = nullptr;
    if (!readPointer(&table[kCreateDevice], &now)) return false;
    if (now == reinterpret_cast<void*>(&createDeviceDetour)) return true;
    FactoryTable* f = factoryOf(table);
    if (!f) {
        if (g_factoryCount >= kMaxTables) return false;
        f = &g_factories[g_factoryCount];
        f->table = table;
        f->first = f->next = reinterpret_cast<CreateDeviceFn>(now);
        if (!writePointer(&table[kCreateDevice], reinterpret_cast<void*>(&createDeviceDetour))) return false;
        InterlockedIncrement(&g_factoryCount);
        char holder[160];
        describeAddress(now, holder, sizeof(holder));
        AS_LOG("menu: CreateDevice hooked in the Direct3D factory table (%s; it held %s)", why, holder);
        return true;
    }
    f->next = reinterpret_cast<CreateDeviceFn>(now);
    if (!writePointer(&table[kCreateDevice], reinterpret_cast<void*>(&createDeviceDetour))) return false;
    if (InterlockedIncrement(&g_rewrites) <= 5) AS_LOG("menu: CreateDevice slot was written over (%s): ours in front again", why);
    return true;
}

IDirect3D9* WINAPI importCreate9Detour(UINT sdk) {
    IDirect3D9* f = g_importCreate9 ? g_importCreate9(sdk) : nullptr;
    if (f) patchFactory(f, "the game's Direct3DCreate9 import");
    return f;
}

IDirect3D9* WINAPI dynCreate9Detour(UINT sdk) {
    IDirect3D9* f = g_dynCreate9 ? g_dynCreate9(sdk) : nullptr;
    if (f) patchFactory(f, "the game's Direct3DCreate9 by GetProcAddress");
    return f;
}

FARPROC WINAPI getProcDetour(HMODULE module, LPCSTR name) {
    FARPROC p = g_getProc(module, name);
    if (p && reinterpret_cast<uintptr_t>(name) > 0xffff && strcmp(name, "Direct3DCreate9") == 0) {
        g_dynCreate9 = reinterpret_cast<Create9Fn>(p);
        return reinterpret_cast<FARPROC>(&dynCreate9Detour);
    }
    return p;
}

DWORD WINAPI watch(LPVOID) {
    // until the game's device exists (at most 60 s): other mods and Windows' shims may write the factory slot
    for (int i = 0; i < 1200 && !g_game; i++) {
        Sleep(50);
        for (LONG t = 0; t < g_factoryCount; t++) {
            void** table = g_factories[t].table;
            void* now = nullptr;
            if (!readPointer(&table[kCreateDevice], &now) || now == reinterpret_cast<void*>(&createDeviceDetour)) continue;
            void* standIn = table;
            patchFactory(reinterpret_cast<IDirect3D9*>(&standIn), "found by the 50 ms check");
        }
    }
    if (!g_game) AS_LOG("menu: no game device seen in 60 s: no menu this start");
    // then the device's slots, every 500 ms (another mod may write its table again)
    for (;;) {
        Sleep(500);
        IDirect3DDevice9* dev = g_game;
        if (!dev) continue;
        void** table = nullptr;
        if (!readPointer(dev, reinterpret_cast<void**>(&table)) || !table) continue;
        void* p = nullptr;
        void* r = nullptr;
        if (!readPointer(&table[kPresent], &p) || !readPointer(&table[kReset], &r)) continue;
        if (p == reinterpret_cast<void*>(&presentDetour) && r == reinterpret_cast<void*>(&resetDetour)) continue;
        if (InterlockedIncrement(&g_rewrites) <= 10) AS_LOG("menu: the device's Present/Reset slot was written over: ours in front again");
        hookDevice(dev);
    }
}

} // namespace

void install(const Hotkey& menuKey, DrawMenuFn drawMenu) {
    if (g_installed) return;
    g_installed = true;
    g_key = menuKey;
    g_draw = drawMenu;
    HMODULE exe = GetModuleHandleW(nullptr);
    void* orig = nullptr;
    if (patchImport(exe, "d3d9.dll", "Direct3DCreate9", reinterpret_cast<void*>(&importCreate9Detour), &orig))
        g_importCreate9 = reinterpret_cast<Create9Fn>(orig);
    orig = nullptr;
    if (patchImport(exe, "kernel32.dll", "GetProcAddress", reinterpret_cast<void*>(&getProcDetour), &orig))
        g_getProc = reinterpret_cast<GetProcFn>(orig);
    AS_LOG("menu: exe imports: Direct3DCreate9 %s, GetProcAddress %s", g_importCreate9 ? "hooked" : "not imported",
           g_getProc ? "hooked" : "not imported");
    // our own factory from the d3d9.dll the game will use (a wrapper in the game folder comes first)
    HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    if (!d3d9) d3d9 = LoadLibraryW(L"d3d9.dll");
    auto create = d3d9 ? reinterpret_cast<Create9Fn>(GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
    if (IDirect3D9* f = create ? create(D3D_SDK_VERSION) : nullptr) {
        patchFactory(f, "our own factory");
        f->Release();
    } else {
        AS_LOG("menu: could not make a Direct3D 9 factory");
    }
    installInputHold();
    HANDLE th = CreateThread(nullptr, 0, &watch, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
}

bool installed() { return g_installed; }
bool deviceHooked() { return g_game != nullptr; }

void toast(const char* utf8) {
    if (!g_installed || !utf8) return;
    AcquireSRWLockExclusive(&g_toastLock);
    if (g_toastCount == 3) {
        memmove(&g_toasts[0], &g_toasts[1], sizeof(Toast) * 2);
        g_toastCount = 2;
    }
    Toast& t = g_toasts[g_toastCount++];
    strncpy_s(t.text, utf8, _TRUNCATE);
    t.until = GetTickCount() + kToastMs;
    ReleaseSRWLockExclusive(&g_toastLock);
}

void setMenuOpen(bool open) {
    if (open && (!g_backends || g_faulted)) return;
    g_menu = open;
    if (!open) setBlocked(false);
}

bool menuOpen() { return g_menu; }
float scale() { return g_scale; }
void* logoTexture() { return g_logo; }

} // namespace overlay
} // namespace autosave
