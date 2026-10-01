#include "platform/Tray.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/StringUtil.h"
#include "ui/I18n.h"

namespace bgn {

namespace {
// hWnd + uID identification (a GUID would be bound to one executable path,
// breaking the icon when the portable and installed versions are both used)
constexpr UINT kTrayId = 1;

// Draws an anti-aliased state icon: dark rounded tile + colored ring + core.
HICON makeIcon(int size, COLORREF color, bool hollow) {
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return nullptr;
    auto* px = static_cast<uint32_t*>(bits);
    const float cr = GetRValue(color) / 255.0f, cg = GetGValue(color) / 255.0f, cb = GetBValue(color) / 255.0f;
    const float c = (size - 1) * 0.5f;
    const float rTile = size * 0.5f, rRing = size * 0.36f, ringW = std::max(1.5f, size * 0.10f), rCore = size * 0.17f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            // 4x supersampling
            float aTile = 0, aRing = 0, aCore = 0;
            for (int s = 0; s < 16; ++s) {
                float fx = x + ((s & 3) + 0.5f) / 4.0f - 0.5f, fy = y + ((s >> 2) + 0.5f) / 4.0f - 0.5f;
                float d = std::sqrt((fx - c) * (fx - c) + (fy - c) * (fy - c));
                if (d <= rTile) aTile += 1;
                if (std::fabs(d - rRing) <= ringW * 0.5f) aRing += 1;
                if (!hollow && d <= rCore) aCore += 1;
            }
            aTile /= 16;
            aRing /= 16;
            aCore /= 16;
            float r = 0.06f, g = 0.08f, b = 0.11f, a = aTile;
            float fg = std::min(1.0f, aRing + aCore);
            r = r * (1 - fg) + cr * fg;
            g = g * (1 - fg) + cg * fg;
            b = b * (1 - fg) + cb * fg;
            // premultiplied alpha
            uint32_t A = uint32_t(a * 255 + 0.5f), R = uint32_t(r * a * 255 + 0.5f), G = uint32_t(g * a * 255 + 0.5f), B = uint32_t(b * a * 255 + 0.5f);
            px[y * size + x] = (A << 24) | (R << 16) | (G << 8) | B;
        }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}
} // namespace

TrayIcon::~TrayIcon() { destroy(); }

UINT TrayIcon::taskbarCreatedMessage() {
    static UINT msg = RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
}

HICON TrayIcon::iconFor(TrayState s) {
    int idx = int(s);
    if (!icons_[idx]) {
        int size = GetSystemMetrics(SM_CXSMICON);
        if (size < 16) size = 16;
        switch (s) {
        case TrayState::Waiting: icons_[idx] = makeIcon(size, RGB(107, 122, 144), true); break;
        case TrayState::Connected: icons_[idx] = makeIcon(size, RGB(108, 123, 255), false); break;
        case TrayState::Enhancing: icons_[idx] = makeIcon(size, RGB(43, 227, 176), false); break;
        case TrayState::Paused: icons_[idx] = makeIcon(size, RGB(255, 181, 71), true); break;
        }
    }
    return icons_[idx];
}

bool TrayIcon::create(HWND owner) {
    owner_ = owner;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uID = kTrayId;
    nid.uCallbackMessage = kCallbackMessage;
    nid.hIcon = iconFor(state_);
    wcsncpy_s(nid.szTip, tooltip_.empty() ? L"Better GFN Neural" : tooltip_.c_str(), _TRUNCATE);
    // A stale icon with our GUID may exist after a crash: delete first.
    Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) == TRUE;
    if (added_) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
    return added_;
}

void TrayIcon::recreate() {
    added_ = false;
    if (owner_) create(owner_);
}

void TrayIcon::destroy() {
    if (added_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = owner_;
        nid.uFlags = 0;
        nid.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        added_ = false;
    }
    for (HICON& i : icons_) {
        if (i) DestroyIcon(i);
        i = nullptr;
    }
}

void TrayIcon::setState(TrayState state, const std::string& tooltip) {
    std::wstring tip = widen(tooltip);
    if (state == state_ && tip == tooltip_) return;
    state_ = state;
    tooltip_ = tip;
    if (!added_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner_;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uID = kTrayId;
    nid.hIcon = iconFor(state);
    wcsncpy_s(nid.szTip, tooltip_.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

UINT TrayIcon::showMenu(bool paused, bool gfnRunning) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CmdOpen, widen(tr("Open")).c_str());
    AppendMenuW(menu, MF_STRING, CmdPauseResume, widen(paused ? tr("Resume enhancement") : tr("Pause enhancement")).c_str());
    AppendMenuW(menu, MF_STRING | (gfnRunning ? MF_GRAYED : 0), CmdLaunchGfn, widen(tr("Launch GeForce NOW")).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CmdExit, widen(tr("Exit")).c_str());
    SetMenuDefaultItem(menu, CmdOpen, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(owner_);
    UINT cmd = UINT(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, owner_, nullptr));
    PostMessageW(owner_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return cmd;
}

void TrayIcon::notify(const std::string& title, const std::string& text) {
    if (!added_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner_;
    nid.uFlags = NIF_INFO;
    nid.uID = kTrayId;
    nid.dwInfoFlags = NIIF_NONE | NIIF_NOSOUND;
    wcsncpy_s(nid.szInfoTitle, widen(title).c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, widen(text).c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

} // namespace bgn
