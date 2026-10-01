#include "platform/Controllers.h"

#include "platform/Win32.h"

#include <xinput.h>

#include <format>

namespace bgn {

namespace {

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using XInputGetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);

struct XInputApi {
    HMODULE module = nullptr;
    XInputGetStateFn getState = nullptr;
    XInputGetCapabilitiesFn getCaps = nullptr;
    XInputApi() {
        for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
            module = LoadLibraryW(dll);
            if (module) break;
        }
        if (module) {
            getState = reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"));
            getCaps = reinterpret_cast<XInputGetCapabilitiesFn>(GetProcAddress(module, "XInputGetCapabilities"));
        }
    }
};

XInputApi& xinput() {
    static XInputApi api;
    return api;
}

struct KnownHid {
    unsigned vid, pid;
    const char* name;
    const char* family;
};

const KnownHid kKnown[] = {
    {0x054C, 0x0CE6, "DualSense Wireless Controller", "PlayStation"},
    {0x054C, 0x0DF2, "DualSense Edge Wireless Controller", "PlayStation"},
    {0x054C, 0x05C4, "DUALSHOCK 4 Wireless Controller", "PlayStation"},
    {0x054C, 0x09CC, "DUALSHOCK 4 Wireless Controller", "PlayStation"},
    {0x054C, 0x0BA0, "DUALSHOCK 4 USB Wireless Adaptor", "PlayStation"},
    {0x057E, 0x2009, "Nintendo Switch Pro Controller", "Nintendo"},
    {0x2DC8, 0x0000, "8BitDo Controller", "Generic"},
};

} // namespace

std::vector<ControllerInfo> enumerateControllers() {
    std::vector<ControllerInfo> out;
    auto& xi = xinput();
    int xinputCount = 0;
    if (xi.getState) {
        for (DWORD i = 0; i < 4; ++i) {
            XINPUT_STATE st{};
            if (xi.getState(i, &st) != ERROR_SUCCESS) continue;
            ControllerInfo c;
            c.api = "XInput";
            c.family = "Xbox";
            c.slot = int(i);
            c.name = "Xbox Controller";
            XINPUT_CAPABILITIES caps{};
            if (xi.getCaps && xi.getCaps(i, 0, &caps) == ERROR_SUCCESS) {
                if (caps.SubType == XINPUT_DEVSUBTYPE_WHEEL) c.name = "Racing Wheel (XInput)";
                else if (caps.SubType == XINPUT_DEVSUBTYPE_ARCADE_STICK) c.name = "Arcade Stick (XInput)";
                else if (caps.SubType == XINPUT_DEVSUBTYPE_FLIGHT_STICK) c.name = "Flight Stick (XInput)";
            }
            out.push_back(c);
            ++xinputCount;
        }
    }
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0) return out;
    std::vector<RAWINPUTDEVICELIST> list(count);
    UINT got = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (got == UINT(-1)) return out;
    for (UINT i = 0; i < got; ++i) {
        if (list[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(info);
        UINT size = sizeof(info);
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) == UINT(-1)) continue;
        const auto& hid = info.hid;
        if (hid.usUsagePage != 0x01 || (hid.usUsage != 0x04 && hid.usUsage != 0x05)) continue; // joystick / gamepad
        if (hid.dwVendorId == 0x045E && xinputCount > 0) continue; // Xbox pads already listed via XInput
        ControllerInfo c;
        c.api = "HID";
        c.family = "Generic";
        c.name = std::format("Game controller ({:04X}:{:04X})", unsigned(hid.dwVendorId), unsigned(hid.dwProductId));
        for (const auto& k : kKnown) {
            if (k.vid == hid.dwVendorId && (k.pid == 0 || k.pid == hid.dwProductId)) {
                c.name = k.name;
                c.family = k.family;
                break;
            }
        }
        if (hid.dwVendorId == 0x045E) {
            c.name = "Xbox Controller (HID)";
            c.family = "Xbox";
        }
        out.push_back(c);
    }
    return out;
}

} // namespace bgn
