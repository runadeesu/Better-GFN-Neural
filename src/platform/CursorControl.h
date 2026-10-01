#pragma once
// Cursor handling for fullscreen-upscale output: the real cursor is confined to
// the (smaller) GFN client area and hidden, while the captured cursor is drawn
// scaled inside the output. Everything is restored on deactivation, on exit and
// from the crash handler.

#include "platform/Win32.h"

namespace bgn {

class CursorControl {
public:
    CursorControl();
    ~CursorControl();
    // Confine to |clip| (screen coordinates) and hide the system cursor.
    void activate(const RECT& clip);
    void deactivate();
    // Re-applies the clip if Windows reset it (focus changes do that).
    void tick();
    bool active() const { return active_; }

    static void emergencyRestore();

private:
    bool active_ = false;
    RECT clip_{};
};

} // namespace bgn
