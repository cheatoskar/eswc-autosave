// The real menu on a real Direct3D 9 device: essentials::start + overlay as in the game, on a test window of this
// exe (it imports Direct3DCreate9 like TM Sunrise does). Checks:
//   - the exe import and the factory/device hooks are installed, the device is seen
//   - Ctrl+F2 opens the menu, the menu is drawn (pixels differ from the cleared frame), Esc closes it
//   - while the menu is open the DirectInput keyboard table is ours
//   - keyboard navigation: Down + Space toggles "Autosave", the ini gets Enabled=0, the switch reaches the autosave
//   - a saved replay shows the toast
// Writes build\test\menu_test.bmp (menu open) and menu_toast.bmp (toast) next to this exe. Not the game: the
// controller fix finds no table for this exe and says so; there is no login fix (Sunrise-like).
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>

#include <d3d9.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>

#include "../src/essentials.h"
#include "../src/overlay.h"
#include "../src/settings.h"
#include "../src/log.h"

// ---- stand-ins for autosave.cpp (the hooks need the real game)
static int g_setEnabledCalls = 0;
static bool g_enabled = true;
namespace autosave {
bool autosaveInstall() { return true; }
bool autosaveInstalled() { return true; }
void autosaveSetEnabled(bool on) {
    g_setEnabledCalls++;
    g_enabled = on;
}
void autosaveSetBestOnly(bool) {}
} // namespace autosave

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) {
    if (ok) g_pass++;
    else g_fail++;
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
}

static IDirect3DDevice9* g_dev = nullptr;
static HWND g_wnd = nullptr;
static const D3DCOLOR kClear = D3DCOLOR_XRGB(30, 60, 90);

static void pump() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static void frame() {
    pump();
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, kClear, 1.0f, 0);
    if (SUCCEEDED(g_dev->BeginScene())) g_dev->EndScene();
    g_dev->Present(nullptr, nullptr, nullptr, nullptr);
}

static void frames(int n) {
    for (int i = 0; i < n; i++) frame();
}

// back buffer (kept by D3DSWAPEFFECT_COPY) -> pixels; counts pixels that are not the clear colour in a rect
static int capture(const wchar_t* bmp, RECT area, int* changedInArea) {
    IDirect3DSurface9* back = nullptr;
    IDirect3DSurface9* sys = nullptr;
    D3DSURFACE_DESC d;
    if (FAILED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) return -1;
    back->GetDesc(&d);
    g_dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr);
    g_dev->GetRenderTargetData(back, sys);
    D3DLOCKED_RECT lr;
    sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
    int changed = 0, inArea = 0;
    for (UINT y = 0; y < d.Height; y++) {
        const DWORD* row = reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(lr.pBits) + y * lr.Pitch);
        for (UINT x = 0; x < d.Width; x++)
            if ((row[x] & 0xffffff) != (kClear & 0xffffff)) {
                changed++;
                if (static_cast<LONG>(x) >= area.left && static_cast<LONG>(x) < area.right && static_cast<LONG>(y) >= area.top &&
                    static_cast<LONG>(y) < area.bottom)
                    inArea++;
            }
    }
    // BMP, 32-bit bottom-up
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof(ih);
    ih.biWidth = static_cast<LONG>(d.Width);
    ih.biHeight = static_cast<LONG>(d.Height);
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    const DWORD bytes = d.Width * d.Height * 4;
    fh.bfType = 0x4d42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + bytes;
    HANDLE f = CreateFileW(bmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr = 0;
    WriteFile(f, &fh, sizeof(fh), &wr, nullptr);
    WriteFile(f, &ih, sizeof(ih), &wr, nullptr);
    for (int y = static_cast<int>(d.Height) - 1; y >= 0; y--)
        WriteFile(f, static_cast<const BYTE*>(lr.pBits) + y * lr.Pitch, d.Width * 4, &wr, nullptr);
    CloseHandle(f);
    sys->UnlockRect();
    sys->Release();
    back->Release();
    if (changedInArea) *changedInArea = inArea;
    return changed;
}

static void key(UINT vk, bool ctrl) {
    BYTE ks[256];
    GetKeyboardState(ks);
    ks[VK_CONTROL] = ctrl ? 0x80 : 0;
    ks[VK_LCONTROL] = ctrl ? 0x80 : 0;
    SetKeyboardState(ks);
    const LPARAM down = 1 | (static_cast<LPARAM>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16);
    SendMessageW(g_wnd, WM_KEYDOWN, vk, down);
    frames(2);
    SendMessageW(g_wnd, WM_KEYUP, vk, down | (1 << 30) | (1u << 31));
    ks[VK_CONTROL] = 0;
    ks[VK_LCONTROL] = 0;
    SetKeyboardState(ks);
    frames(2);
}

static bool tableIsOurs(void* fn) {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(fn), &m);
    return m == GetModuleHandleW(nullptr);
}

int wmain() {
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    *wcsrchr(dir, L'\\') = 0;
    wchar_t ini[MAX_PATH], bmp1[MAX_PATH], bmp2[MAX_PATH];
    swprintf_s(ini, L"%s\\eswc_autosave.ini", dir);
    swprintf_s(bmp1, L"%s\\menu_test.bmp", dir);
    swprintf_s(bmp2, L"%s\\menu_toast.bmp", dir);
    DeleteFileW(ini);
    autosave::logOpen(); // eswc_autosave.log next to this exe

    // a Sunrise-like exe id (no login fix), the defaults (menu key Ctrl+F2)
    static const autosave::ExeId kTestExe = {autosave::Game::Sunrise, "menu_test.exe", "", 0, 0};
    autosave::Settings set;
    autosave::essentials::start(kTestExe, set, reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), GetCurrentThreadId());
    check(autosave::overlay::installed(), "overlay installed (standalone, menu key set)");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LegacyEssentialsMenuTest";
    RegisterClassW(&wc);
    g_wnd = CreateWindowW(wc.lpszClassName, L"menu test", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
    RECT rc;
    GetClientRect(g_wnd, &rc);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION); // the exe's import: our wrapper patches the factory
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_COPY; // the back buffer keeps the frame for the capture
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = static_cast<UINT>(rc.right);
    pp.BackBufferHeight = static_cast<UINT>(rc.bottom);
    pp.EnableAutoDepthStencil = TRUE; // like the games: this is what makes it "the game's device"
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.hDeviceWindow = g_wnd;
    HRESULT hr = d3d ? d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_wnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_dev)
                     : E_FAIL;
    if (FAILED(hr) || !g_dev) {
        printf("no Direct3D 9 HAL device here (0x%08lx): menu test skipped\n", static_cast<unsigned long>(hr));
        return 0;
    }
    check(autosave::overlay::deviceHooked(), "game device seen by the CreateDevice hook");
    void** vt = *reinterpret_cast<void***>(g_dev);
    check(tableIsOurs(vt[17]), "Present in the device table is ours");

    frames(3); // first message: late start (controller: no table for this exe)
    check(!autosave::overlay::menuOpen(), "menu closed at start");
    int inArea = 0;
    RECT center = {rc.right / 2 - 100, rc.bottom / 2 - 100, rc.right / 2 + 100, rc.bottom / 2 + 100};
    int changed = capture(bmp1, center, &inArea);
    check(changed == 0, "nothing drawn while the menu is closed");

    key(VK_F2, false);
    check(!autosave::overlay::menuOpen(), "F2 alone does not open the menu");
    key(VK_F2, true);
    check(autosave::overlay::menuOpen(), "Ctrl+F2 opens the menu");
    frames(3);
    changed = capture(bmp1, center, &inArea);
    printf("      menu frame: %d pixels drawn, %d in the centre square\n", changed, inArea);
    check(changed > 20000 && inArea > 20000, "menu drawn in the middle of the screen");

    // DirectInput: the keyboard device table now leads to ours
    IDirectInput8W* di = nullptr;
    if (SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&di), nullptr))) {
        IDirectInputDevice8W* kb = nullptr;
        if (SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &kb, nullptr))) {
            void** kt = *reinterpret_cast<void***>(kb);
            check(tableIsOurs(kt[9]) && tableIsOurs(kt[10]), "keyboard GetDeviceState/GetDeviceData are ours");
            kb->Release();
        }
        di->Release();
    }

    // keyboard navigation: Down shows the cursor one below "Autosave", Up goes back, Space toggles it
    key(VK_DOWN, false);
    key(VK_UP, false);
    key(VK_SPACE, false);
    frames(3);
    const UINT en = GetPrivateProfileIntW(L"autosave", L"Enabled", 7, ini);
    printf("      after Space: Enabled=%u in the ini, autosave switch calls %d (on=%d)\n", en, g_setEnabledCalls, g_enabled);
    check(en == 0 && g_setEnabledCalls == 1 && !g_enabled, "Autosave switched off from the menu, written to the ini");
    key(VK_SPACE, false);
    frames(3);
    check(GetPrivateProfileIntW(L"autosave", L"Enabled", 7, ini) == 1 && g_enabled, "and on again");
    // Down goes to "Best times only": Space writes Mode=all
    key(VK_DOWN, false);
    key(VK_SPACE, false);
    frames(3);
    wchar_t mode[16] = L"";
    GetPrivateProfileStringW(L"autosave", L"Mode", L"", mode, 16, ini);
    check(wcscmp(mode, L"all") == 0, "Best times only switched off (Mode=all)");
    key(VK_SPACE, false);
    key(VK_UP, false); // back on "Autosave" for the screenshot
    changed = capture(bmp1, center, &inArea); // keep this one: the menu as the player sees it

    key(VK_ESCAPE, false);
    check(!autosave::overlay::menuOpen(), "Esc closes the menu");

    autosave::essentials::noteSaved(L"C:\\x\\GameData\\Tracks\\Replays\\My Replays\\A01(00'23''45).Replay.Gbx");
    frames(2);
    RECT topRight = {rc.right - 400, 0, rc.right, 120};
    changed = capture(bmp2, topRight, &inArea);
    printf("      toast frame: %d pixels drawn, %d top right\n", changed, inArea);
    check(changed > 300 && inArea == changed, "toast drawn top right only");

    g_dev->Release();
    d3d->Release();
    DestroyWindow(g_wnd);
    DeleteFileW(ini);
    printf("menu_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
