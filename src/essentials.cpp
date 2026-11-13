// Legacy Essentials: start order, Horizon Client check, login fix, controller fix, game settings, menu.
//
// At start (inside the game's first DirectInput8Create): the exe is known and the ini read (autosave.cpp).
//   - Horizon Client expected (our loader runs as dinput8.chain.dll and horizon\host.dll is there): no menu,
//     the login fix waits for the first message.
//   - else: login fix now, menu hooks now (the game makes its Direct3D device after this call).
//   - either way: the ini's volumes and window mode go into the game's settings now (unless the client's Game
//     settings module is there to do it), so the game's own start uses them.
// On the game thread's first message (the client, if any, has started by then): login fix unless the client
// runs, controller fix (claimed by whoever comes first: the client's module claims in its start).
// The autosave hooks are autosave.cpp's (the client's Autosave module pauses itself for this mod).
#include "essentials.h"

#include <objbase.h>
#include <process.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/horizon/imgui/imgui.h"
#include "../vendor/horizon/source.h"
#include "coexist.h"
#include "controller.h"
#include "gamesettings.h"
#include "log.h"
#include "overlay.h"
#include "shim/host/host.h"

namespace autosave {
namespace essentials {
namespace {

// HttpOpenRequestA import slots (PE import table): ESWC 0x00aca340, Original 0x00a162d4
constexpr uintptr_t kEswcIatHttpOpenRequestA = 0x00aca340;
constexpr uintptr_t kOriginalIatHttpOpenRequestA = 0x00a162d4;
constexpr WPARAM kMarker = 0x4C455353; // 'LESS': a controller step for the game thread

const ExeId* g_exe = nullptr;
Settings g_set;
wchar_t g_ini[MAX_PATH] = L"";
uintptr_t g_base = 0x00400000;
DWORD g_thread = 0;
uint32_t g_timestamp = 0;
bool g_expectHorizon = false;
bool g_horizon = false;
bool g_lateDone = false;
bool g_loginInstalled = false; // our login fix was installed this start
bool g_loginActive = false;    // ... and is in place
HHOOK g_hook = nullptr;
HWND g_kickWnd = nullptr;
volatile LONG g_queued = 0;
wchar_t g_lastFolder[MAX_PATH] = L"";
unsigned g_saved = 0;

bool gameHasLoginFix() { return g_exe && (g_exe->game == Game::Eswc || g_exe->game == Game::Original); }
uintptr_t loginSlot() { return g_exe->game == Game::Eswc ? kEswcIatHttpOpenRequestA : kOriginalIatHttpOpenRequestA; }

const char* gameShort() {
    if (!g_exe) return "";
    switch (g_exe->game) {
    case Game::Eswc: return "TM Nations ESWC";
    case Game::Sunrise: return "TM Sunrise";
    case Game::Original: return "TM Original";
    default: return "";
    }
}

// the exe's HttpOpenRequestA import no longer leads to WinINet: a login fix (ours or the client's) is in place
bool loginHooked() {
    HMODULE wi = GetModuleHandleW(L"wininet.dll");
    void* real = wi ? reinterpret_cast<void*>(GetProcAddress(wi, "HttpOpenRequestA")) : nullptr;
    void** slot = reinterpret_cast<void**>(g_base - 0x00400000 + loginSlot());
    return real && *slot != real;
}

void installLoginFix() {
    if (g_loginInstalled) return;
    g_loginInstalled = true;
    horizon::onlineFixInstall(g_base, loginSlot()); // logs "online: fix active" or why not
    g_loginActive = loginHooked();
}

// ---------------------------------------------------------------- controller fix: game-thread steps
BOOL CALLBACK pickWindow(HWND h, LPARAM lp) {
    HWND* out = reinterpret_cast<HWND*>(lp);
    if (!*out) *out = h;
    if (IsWindowVisible(h)) {
        *out = h;
        return FALSE;
    }
    return TRUE;
}

void kick() { // worker thread
    if (InterlockedExchange(&g_queued, 1) != 0) return;
    if (!g_kickWnd || !IsWindow(g_kickWnd)) {
        HWND w = nullptr;
        EnumThreadWindows(g_thread, &pickWindow, reinterpret_cast<LPARAM>(&w));
        g_kickWnd = w;
    }
    BOOL ok = g_kickWnd ? PostMessageW(g_kickWnd, WM_NULL, kMarker, 0) : FALSE;
    if (!ok) ok = PostThreadMessageW(g_thread, WM_NULL, kMarker, 0);
    if (!ok) InterlockedExchange(&g_queued, 0);
}

void toast(const char* text) { overlay::toast(text); }

void lateStart() {
    g_lateDone = true;
    const HorizonProbe p = probeHorizon();
    const Plan plan = makePlan(p, Stage::FirstMessage, gameHasLoginFix(), g_set.onlineFix, g_set.controllerFix, g_set.autosave,
                               g_set.menuKey.vk != 0);
    g_horizon = plan.horizon;
    char probe[200];
    describeProbe(p, probe, sizeof(probe));
    AS_LOG("first message: %s -> %s", probe, plan.horizon ? "Horizon Client runs" : "no Horizon Client");
    if (g_expectHorizon && !plan.horizon)
        AS_LOG("note: the Horizon Client did not start (Ctrl+Shift held, or it refused this exe): no menu this start, "
               "everything else runs");
    if (gameHasLoginFix()) {
        if (plan.loginFix) installLoginFix();
        else if (plan.horizon) AS_LOG("login fix: the Horizon Client's own (OnlineFix in its host.json); ours stays off");
        else if (!g_set.onlineFix) AS_LOG("login fix: OnlineFix=0, nothing changed");
    }
    controller::init(g_base, g_timestamp, &kick, &toast, plan.controller);
}

LRESULT CALLBACK msgHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE) {
        if (!g_lateDone) lateStart();
        const MSG* m = reinterpret_cast<const MSG*>(lp);
        if (m && m->message == WM_NULL && m->wParam == kMarker) {
            InterlockedExchange(&g_queued, 0);
            controller::tick();
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

// ---------------------------------------------------------------- menu
void folderNow(wchar_t* out, size_t cap) {
    if (g_lastFolder[0]) {
        wcsncpy_s(out, cap, g_lastFolder, _TRUNCATE);
        return;
    }
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* slash = (n && n < MAX_PATH) ? wcsrchr(exe, L'\\') : nullptr;
    if (!slash) {
        out[0] = 0;
        return;
    }
    slash[1] = 0;
    const wchar_t* subs[] = {L"GameData\\Tracks\\Replays\\My Replays", L"GameData\\Tracks\\Replays", L""};
    for (const wchar_t* s : subs) {
        wchar_t p[MAX_PATH];
        swprintf_s(p, L"%s%s", exe, s);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
            wcsncpy_s(out, cap, p, _TRUNCATE);
            return;
        }
    }
    out[0] = 0;
}

unsigned __stdcall openFolderThread(void* arg) {
    wchar_t* path = static_cast<wchar_t*>(arg);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
    CoUninitialize();
    free(path);
    return 0;
}

void openFolder() {
    wchar_t folder[MAX_PATH];
    folderNow(folder, MAX_PATH);
    if (!folder[0]) return;
    wchar_t* copy = _wcsdup(folder);
    if (!copy) return;
    const uintptr_t h = _beginthreadex(nullptr, 0, openFolderThread, copy, 0, nullptr);
    if (h) CloseHandle(reinterpret_cast<HANDLE>(h));
    else free(copy);
    char f8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, folder, -1, f8, sizeof(f8), nullptr, nullptr);
    AS_LOG("menu: open folder %s", f8);
}

void save(const wchar_t* section, const wchar_t* key, bool on) {
    if (!writeSettingBool(g_ini, section, key, on)) AS_LOG("menu: could not write %ls=%d to the ini (error %lu)", key, on, GetLastError());
    else AS_LOG("menu: %ls=%d", key, on ? 1 : 0);
}

// status on the right of a row
void status(const char* text, bool dim = true) {
    const float w = ImGui::CalcTextSize(text).x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w);
    if (dim) ImGui::TextDisabled("%s", text);
    else ImGui::TextUnformatted(text);
}

void drawMenu(bool* open) {
    const float s = overlay::scale();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float width = 330.0f * s;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, vp->WorkSize.y));
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("Legacy Essentials##le-menu", open, f)) {
        ImGui::End();
        return;
    }
    // header: logo + name
    const float logo = 30.0f * s;
    if (void* tex = overlay::logoTexture()) {
        ImGui::Image((ImTextureID)(uintptr_t)tex, ImVec2(logo, logo));
        ImGui::SameLine();
    }
    const float textH = ImGui::GetTextLineHeight();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (logo - textH) * 0.5f);
    ImGui::TextUnformatted("Legacy Essentials");
    ImGui::Separator();

    // Autosave
    bool on = g_set.autosave;
    if (ImGui::Checkbox("Autosave", &on)) {
        g_set.autosave = on;
        save(L"autosave", L"Enabled", on);
        if (on && !autosaveInstalled()) autosaveInstall();
        autosaveSetEnabled(on);
    }
    const bool asBroken = g_set.autosave && !autosaveInstalled();
    if (asBroken) status("Unavailable");
    else if (g_set.autosave && g_saved) {
        char t[32];
        _snprintf_s(t, _TRUNCATE, "%u saved", g_saved);
        status(t);
    } else status(g_set.autosave ? "On" : "Off");
    ImGui::Indent(22.0f * s);
    ImGui::BeginDisabled(!g_set.autosave);
    bool best = g_set.bestOnly;
    if (ImGui::Checkbox("Best times only", &best)) {
        g_set.bestOnly = best;
        if (!writeSetting(g_ini, L"autosave", L"Mode", best ? L"best" : L"all")) AS_LOG("menu: could not write Mode to the ini");
        else AS_LOG("menu: Mode=%s", best ? "best" : "all");
        autosaveSetBestOnly(best);
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Open folder")) openFolder();
    ImGui::Unindent(22.0f * s);
    ImGui::Separator();

    // Controller fix
    const controller::State cs = controller::state();
    const bool csFixed = cs == controller::State::Client || cs == controller::State::Unknown || cs == controller::State::Failed;
    bool pad = cs == controller::State::Running || (g_set.controllerFix && csFixed);
    ImGui::BeginDisabled(csFixed);
    if (ImGui::Checkbox("Controller fix", &pad)) {
        g_set.controllerFix = pad;
        save(L"controller", L"ControllerFix", pad);
        controller::setEnabled(pad);
    }
    ImGui::EndDisabled();
    switch (controller::state()) {
    case controller::State::Running: {
        const int n = controller::padsInGame();
        char t[32];
        if (n) _snprintf_s(t, _TRUNCATE, n == 1 ? "1 pad" : "%d pads", n);
        status(n ? t : "On");
        break;
    }
    case controller::State::Client: status("Other copy"); break;
    case controller::State::Unknown: status("Unsupported"); break;
    case controller::State::Failed: status("Error"); break;
    default: status("Off"); break;
    }

    // Login fix (ESWC, Original)
    if (gameHasLoginFix()) {
        ImGui::Separator();
        bool login = g_set.onlineFix;
        if (ImGui::Checkbox("Login fix", &login)) {
            g_set.onlineFix = login;
            save(L"online", L"OnlineFix", login);
        }
        if (g_set.onlineFix != g_loginActive) status(g_set.onlineFix && g_loginInstalled ? "Error" : "Restart game", false);
        else status(g_loginActive ? "On" : "Off");
    }

    // Volume, Music, Window mode
    ImGui::Separator();
    gamesettings::drawRows(g_ini, s);

    // About
    ImGui::Separator();
    ImGui::TextDisabled("Version %s", kProductVersion);
    status(gameShort());
    wchar_t key[48];
    formatHotkey(g_set.menuKey, key, 48);
    char k8[64];
    WideCharToMultiByte(CP_UTF8, 0, key, -1, k8, sizeof(k8), nullptr, nullptr);
    ImGui::TextDisabled("Menu key");
    status(k8);
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0.0f))) *open = false;
    ImGui::End();
}

} // namespace

void start(const ExeId& exe, const Settings& settings, uintptr_t exeBase, DWORD mainThread) {
    g_exe = &exe;
    g_set = settings;
    g_base = exeBase;
    g_thread = mainThread;
    iniPath(g_ini, MAX_PATH);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(exeBase + dos->e_lfanew);
    g_timestamp = nt->FileHeader.TimeDateStamp;
    AS_LOG("shared code: horizon-host %s (%s): login fix, controller fix, ImGui %s; %s (%s): game settings", HORIZON_SOURCE_SHORT,
           HORIZON_SOURCE_DATE, IMGUI_VERSION, HORIZON_SETTINGS_SHORT, HORIZON_SETTINGS_DATE);

    const HorizonProbe p = probeHorizon();
    const Plan plan = makePlan(p, Stage::Start, gameHasLoginFix(), g_set.onlineFix, g_set.controllerFix, g_set.autosave,
                               g_set.menuKey.vk != 0);
    g_expectHorizon = plan.horizon;
    char probe[200];
    describeProbe(p, probe, sizeof(probe));
    AS_LOG("start: %s -> %s", probe,
           plan.horizon ? "Horizon Client expected: its menu, login fix and controller fix are used; autosave stays here"
                        : "standalone");

    if (!plan.horizon && gameHasLoginFix()) {
        if (plan.loginFix) installLoginFix();
        else AS_LOG("login fix: OnlineFix=0, nothing changed");
    } else if (!gameHasLoginFix()) {
        AS_LOG("login fix: not needed for this game, nothing changed");
    }
    // volumes and window mode: into the game's settings now, before it makes its Direct3D device and audio
    gamesettings::start(g_base, g_timestamp, g_set, plan.gameSettings, plan.menu);
    if (plan.menu) overlay::install(g_set.menuKey, &drawMenu);
    else if (!plan.horizon) AS_LOG("menu: MenuKey=0, no menu");

    // the rest on the game thread's first message (see the top of this file)
    g_hook = SetWindowsHookExW(WH_GETMESSAGE, &msgHook, nullptr, mainThread);
    if (!g_hook) AS_LOG("ERROR: message hook failed (%lu): no controller fix this start", GetLastError());
}

void noteSaved(const wchar_t* fullPath) {
    g_saved++;
    wchar_t folder[MAX_PATH];
    wcsncpy_s(folder, fullPath, _TRUNCATE);
    if (wchar_t* slash = wcsrchr(folder, L'\\')) {
        *slash = 0;
        wcsncpy_s(g_lastFolder, folder, _TRUNCATE);
    }
    overlay::toast("Replay saved");
}

} // namespace essentials
} // namespace autosave
