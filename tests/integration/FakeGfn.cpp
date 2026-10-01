// FakeGfn: a stand-in for the GeForce NOW streaming window used by the
// integration tests. It is copied to "GeForceNOW.exe" so the detector sees the
// same process name, shows a 60 fps animated GDI picture and follows a script:
//
//   FakeGfn.exe --title "Cyberpunk 2077® on GeForce NOW" --size 1280x720
//               --script "8:title=Fortnite on GeForce NOW;12:resize=1600x900;16:fullscreen;20:windowed;24:minimize;26:restore;30:exit"
//
// Times are seconds since start. \uXXXX escapes are accepted in titles.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Action {
    double t;
    std::wstring verb, arg;
};

std::wstring unescape(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 5 < s.size() + 0 && s[i + 1] == L'u') {
            out.push_back(wchar_t(std::wcstol(s.substr(i + 2, 4).c_str(), nullptr, 16)));
            i += 5;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::vector<Action> parseScript(const std::wstring& script) {
    std::vector<Action> out;
    size_t start = 0;
    while (start < script.size()) {
        size_t end = script.find(L';', start);
        if (end == std::wstring::npos) end = script.size();
        std::wstring item = script.substr(start, end - start);
        size_t colon = item.find(L':');
        if (colon != std::wstring::npos) {
            Action a;
            a.t = _wtof(item.substr(0, colon).c_str());
            std::wstring rest = item.substr(colon + 1);
            size_t eq = rest.find(L'=');
            a.verb = eq == std::wstring::npos ? rest : rest.substr(0, eq);
            a.arg = eq == std::wstring::npos ? L"" : unescape(rest.substr(eq + 1));
            out.push_back(a);
        }
        start = end + 1;
    }
    return out;
}

double gStart = 0;
double now() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

void paint(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    HDC wdc = GetDC(hwnd);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, std::max(w, 1), std::max(h, 1));
    HGDIOBJ old = SelectObject(dc, bmp);
    const double t = now() - gStart;
    // background gradient bands (scrolling)
    for (int y = 0; y < h; y += 8) {
        int v = int(40 + 30 * std::sin(y * 0.01 + t));
        HBRUSH b = CreateSolidBrush(RGB(v / 2, v, 60 + v));
        RECT r{0, y, w, y + 8};
        FillRect(dc, &r, b);
        DeleteObject(b);
    }
    // moving fence
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(200, 200, 190));
    HGDIOBJ op = SelectObject(dc, pen);
    int off = int(t * 240) % 24;
    for (int x = -24 + off; x < w; x += 24) {
        MoveToEx(dc, x, h / 2, nullptr);
        LineTo(dc, x, h - 20);
    }
    SelectObject(dc, op);
    DeleteObject(pen);
    // moving disc
    HBRUSH disc = CreateSolidBrush(RGB(230, 90, 60));
    HGDIOBJ ob = SelectObject(dc, disc);
    int cx = int((std::fmod(t * 0.2, 1.0)) * w), cy = h / 3;
    Ellipse(dc, cx - 40, cy - 40, cx + 40, cy + 40);
    SelectObject(dc, ob);
    DeleteObject(disc);
    // HUD text
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    wchar_t buf[128];
    swprintf_s(buf, L"FAKE GFN STREAM  t=%.1fs  %dx%d", t, w, h);
    TextOutW(dc, 20, 20, buf, int(wcslen(buf)));
    BitBlt(wdc, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(hwnd, wdc);
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        paint(hwnd);
        return 0;
    }
    default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring title = L"GeForce NOW", script;
    int w = 1280, h = 720;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--title" && i + 1 < argc) title = unescape(argv[++i]);
        else if (a == L"--size" && i + 1 < argc) swscanf_s(argv[++i], L"%dx%d", &w, &h);
        else if (a == L"--script" && i + 1 < argc) script = argv[++i];
    }
    LocalFree(argv);
    std::vector<Action> actions = parseScript(script);
    gStart = now();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"FakeGfnStreamWindow";
    RegisterClassExW(&wc);
    RECT r{0, 0, w, h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, 40, 40, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    size_t next = 0;
    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        const double t = now() - gStart;
        while (next < actions.size() && actions[next].t <= t) {
            const Action& a = actions[next++];
            if (a.verb == L"title") SetWindowTextW(hwnd, a.arg.c_str());
            else if (a.verb == L"resize") {
                int nw = w, nh = h;
                swscanf_s(a.arg.c_str(), L"%dx%d", &nw, &nh);
                RECT rr{0, 0, nw, nh};
                AdjustWindowRect(&rr, WS_OVERLAPPEDWINDOW, FALSE);
                SetWindowPos(hwnd, nullptr, 0, 0, rr.right - rr.left, rr.bottom - rr.top, SWP_NOMOVE | SWP_NOZORDER);
            } else if (a.verb == L"fullscreen") {
                MONITORINFO mi{sizeof(mi)};
                GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
                SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                             SWP_FRAMECHANGED);
            } else if (a.verb == L"windowed") {
                SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                SetWindowPos(hwnd, nullptr, 40, 40, 1296, 759, SWP_FRAMECHANGED | SWP_NOZORDER);
            } else if (a.verb == L"minimize") ShowWindow(hwnd, SW_MINIMIZE);
            else if (a.verb == L"restore") {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            } else if (a.verb == L"focus") SetForegroundWindow(hwnd);
            else if (a.verb == L"exit") {
                DestroyWindow(hwnd);
            }
        }
        paint(hwnd);
        Sleep(16);
    }
}
