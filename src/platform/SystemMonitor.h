#pragma once
// System-wide CPU / GPU utilization and GPU memory, sampled ~1 Hz.
// GPU numbers come from the Windows "GPU Engine" / "GPU Adapter Memory"
// performance counters (Windows 10 1709+), filtered to one adapter LUID.

#include <atomic>
#include <mutex>
#include <string>

#include "platform/Win32.h"

namespace bgn {

struct SystemSample {
    double cpuUsage = -1;        // 0..1
    double gpuUsage = -1;        // 0..1 (3D engine), -1 unknown
    double gpuDedicatedMB = -1;  // all processes, -1 unknown
    double gpuDedicatedTotalMB = 0;
};

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();
    void setAdapter(LUID luid, double dedicatedTotalMB);
    void sample();             // call ~1x per second
    SystemSample latest() const;

private:
    bool initPdh();
    mutable std::mutex mutex_;
    SystemSample last_;
    LUID luid_{};
    bool luidSet_ = false;
    double totalMB_ = 0;
    // CPU
    ULONGLONG prevIdle_ = 0, prevKernel_ = 0, prevUser_ = 0;
    // PDH
    void* query_ = nullptr;
    void* gpuCounter_ = nullptr;
    void* memCounter_ = nullptr;
    bool pdhOk_ = false;
    bool pdhTried_ = false;
};

} // namespace bgn
