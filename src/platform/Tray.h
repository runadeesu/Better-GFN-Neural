#pragma once
// Notification-area icon with state (Waiting / Connected / Enhancing / Paused),
// tooltip, context menu and balloon notifications. Icons are drawn at runtime.

#include <string>

#include "platform/Win32.h"

namespace bgn {

enum class TrayState { Waiting, Connected, Enhancing, Paused };

class TrayIcon {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 10;
    enum Command : UINT { CmdOpen = 1001, CmdPauseResume, CmdLaunchGfn, CmdExit, CmdScreenshot };

    ~TrayIcon();
    bool create(HWND owner);
    void destroy();
    void recreate(); // after Explorer restarts (TaskbarCreated)
    void setState(TrayState state, const std::string& tooltip);
    // Shows the context menu and returns the chosen command (0 = none).
    UINT showMenu(bool paused, bool gfnRunning, bool canScreenshot = false);
    void notify(const std::string& title, const std::string& text);
    static UINT taskbarCreatedMessage();

private:
    HICON iconFor(TrayState s);
    HWND owner_ = nullptr;
    bool added_ = false;
    TrayState state_ = TrayState::Waiting;
    std::wstring tooltip_;
    HICON icons_[4] = {};
};

} // namespace bgn
