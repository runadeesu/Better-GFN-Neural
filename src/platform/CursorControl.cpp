#include "platform/CursorControl.h"

#include <atomic>

#include "core/Log.h"
#include "platform/CrashHandler.h"

namespace bgn {

namespace {
using MagInitializeFn = BOOL(WINAPI*)();
using MagUninitializeFn = BOOL(WINAPI*)();
using MagShowSystemCursorFn = BOOL(WINAPI*)(BOOL);

struct MagApi {
    HMODULE module = nullptr;
    MagInitializeFn init = nullptr;
    MagUninitializeFn uninit = nullptr;
    MagShowSystemCursorFn show = nullptr;
    bool initialized = false;
    MagApi() {
        module = LoadLibraryW(L"Magnification.dll");
        if (!module) return;
        init = reinterpret_cast<MagInitializeFn>(GetProcAddress(module, "MagInitialize"));
        uninit = reinterpret_cast<MagUninitializeFn>(GetProcAddress(module, "MagUninitialize"));
        show = reinterpret_cast<MagShowSystemCursorFn>(GetProcAddress(module, "MagShowSystemCursor"));
    }
    bool hide(bool h) {
        if (!show) return false;
        if (!initialized && init) initialized = init() == TRUE;
        return show(h ? FALSE : TRUE) == TRUE;
    }
};

MagApi& mag() {
    static MagApi api;
    return api;
}

std::atomic<bool> gCursorHidden{false};
std::atomic<bool> gCursorClipped{false};
} // namespace

CursorControl::CursorControl() {
    static bool registered = false;
    if (!registered) {
        registerEmergencyCallback(&CursorControl::emergencyRestore);
        registered = true;
    }
}

CursorControl::~CursorControl() { deactivate(); }

void CursorControl::activate(const RECT& clip) {
    clip_ = clip;
    ClipCursor(&clip_);
    gCursorClipped = true;
    if (!active_) {
        if (mag().hide(true)) gCursorHidden = true;
        else BGN_LOG_WARN("Cursor", "system cursor could not be hidden; it will be visible next to the scaled cursor");
        active_ = true;
    }
}

void CursorControl::deactivate() {
    if (!active_) return;
    ClipCursor(nullptr);
    gCursorClipped = false;
    if (gCursorHidden.exchange(false)) mag().hide(false);
    active_ = false;
}

void CursorControl::tick() {
    if (!active_) return;
    RECT cur{};
    if (GetClipCursor(&cur) && (cur.left != clip_.left || cur.top != clip_.top || cur.right != clip_.right || cur.bottom != clip_.bottom)) ClipCursor(&clip_);
}

void CursorControl::emergencyRestore() {
    if (gCursorClipped.exchange(false)) ClipCursor(nullptr);
    if (gCursorHidden.exchange(false)) mag().hide(false);
}

} // namespace bgn
