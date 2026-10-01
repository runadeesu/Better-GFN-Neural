#pragma once
// Game controller detection (read-only). XInput slots are queried and HID game
// controllers are listed from the raw input device list. Devices are never
// opened or written to, so controller input to GeForce NOW is not affected.

#include <string>
#include <vector>

namespace bgn {

struct ControllerInfo {
    std::string name;       // "Xbox Controller", "DualSense Wireless Controller", ...
    std::string family;     // "Xbox", "PlayStation", "Nintendo", "Generic"
    std::string api;        // "XInput" or "HID"
    int slot = -1;          // XInput user index
};

std::vector<ControllerInfo> enumerateControllers();

} // namespace bgn
