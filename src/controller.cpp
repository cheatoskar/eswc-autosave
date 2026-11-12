// Controller fix glue, modelled on the client's standalone build (plugins/controller-hotplug/standalone/
// standalone.cpp): exe table by PE timestamp, one owner per process (claim), code check, worker thread, steps
// on the game thread. The fix itself is vendor/horizon/controller-hotplug, unchanged.
#include "controller.h"

#include <vector>

#include "../vendor/horizon/controller-hotplug/hp_service.h"
#include "log.h"

namespace autosave {
namespace controller {
namespace {

uintptr_t g_base = 0;
uint32_t g_timestamp = 0;
KickFn g_kick = nullptr;
ToastFn g_toast = nullptr;
State g_state = State::Off;
HANDLE g_claim = nullptr;
hp::GameBackend* g_backend = nullptr;
hp::Service* g_service = nullptr;

void logCb(void*, int level, const char* text) {
    AS_LOG("controller: %s%s", level == 2 ? "ERROR: " : level == 1 ? "warning: " : "", text);
}
void toastCb(void*, const char* title) {
    if (g_toast) g_toast(title);
}
void kickCb(void*) {
    if (g_kick) g_kick();
}

State doStart() {
    const hp::GameTable* table = hp::tableForTimestamp(g_timestamp);
    if (!table) {
        AS_LOG("controller: no table for this exe (PE timestamp %08lX): fix off", static_cast<unsigned long>(g_timestamp));
        return State::Unknown;
    }
    if (!hp::claimOwner(&g_claim)) {
        AS_LOG("controller: hot-plug already runs in this game (Horizon Client module or another copy): ours stays off");
        return State::Client;
    }
    g_backend = new hp::GameBackend(table, g_base, &logCb, nullptr);
    if (!g_backend->verify()) { // logged: a code signature differs
        hp::releaseOwner(g_claim);
        g_claim = nullptr;
        return State::Unknown;
    }
    g_service = new hp::Service();
    hp::ServiceCallbacks cb = {nullptr, &logCb, &toastCb, &kickCb};
    if (!g_service->start(g_backend, cb, false)) {
        hp::releaseOwner(g_claim);
        g_claim = nullptr;
        return State::Failed;
    }
    AS_LOG("controller: ready (%s), watching for controllers plugged in while the game runs", table->name);
    return State::Running;
}

} // namespace

State init(uintptr_t exeBase, uint32_t exeTimestamp, KickFn kick, ToastFn toast, bool on) {
    g_base = exeBase;
    g_timestamp = exeTimestamp;
    g_kick = kick;
    g_toast = toast;
    if (!on) {
        AS_LOG("controller: ControllerFix=0, not started");
        g_state = State::Off;
        return g_state;
    }
    g_state = doStart();
    return g_state;
}

void tick() {
    if (g_service) g_service->tick(); // paused: Service::tick only takes the worker's log notes
}

void setEnabled(bool on) {
    if (on) {
        if (g_state == State::Off) g_state = doStart();
        else if (g_state == State::Paused && g_service) {
            g_service->setEnabled(true);
            g_state = State::Running;
        }
    } else if (g_state == State::Running && g_service) {
        g_service->setEnabled(false);
        g_state = State::Paused;
    }
}

State state() { return g_state; }

int padsInGame() {
    if (!g_service) return 0;
    std::vector<hp::Tracker::View> v;
    g_service->view(v);
    int n = 0;
    for (const auto& c : v)
        if (c.present && c.inGame) n++;
    return n;
}

const char* stateName(State s) {
    switch (s) {
    case State::Off: return "off";
    case State::Running: return "on";
    case State::Paused: return "paused";
    case State::Client: return "Horizon Client";
    case State::Unknown: return "unknown game code";
    case State::Failed: return "failed";
    }
    return "?";
}

} // namespace controller
} // namespace autosave
