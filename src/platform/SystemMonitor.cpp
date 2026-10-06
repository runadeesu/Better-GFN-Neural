#include "platform/SystemMonitor.h"

#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cwchar>
#include <map>
#include <vector>

#include "core/Log.h"

namespace bgn {

static ULONGLONG ft(const FILETIME& f) { return (ULONGLONG(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

SystemMonitor::SystemMonitor() {}

SystemMonitor::~SystemMonitor() {
    if (query_) PdhCloseQuery(static_cast<PDH_HQUERY>(query_));
}

void SystemMonitor::setAdapter(LUID luid, double dedicatedTotalMB) {
    std::lock_guard lock(mutex_);
    luid_ = luid;
    luidSet_ = true;
    totalMB_ = dedicatedTotalMB;
}

bool SystemMonitor::initPdh() {
    pdhTried_ = true;
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return false;
    PDH_HCOUNTER gpu = nullptr, mem = nullptr;
    PDH_STATUS s1 = PdhAddEnglishCounterW(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &gpu);
    PDH_STATUS s2 = PdhAddEnglishCounterW(q, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &mem);
    if (s1 != ERROR_SUCCESS && s2 != ERROR_SUCCESS) {
        PdhCloseQuery(q);
        BGN_LOG_WARN("SystemMonitor", "GPU performance counters unavailable");
        return false;
    }
    query_ = q;
    gpuCounter_ = s1 == ERROR_SUCCESS ? gpu : nullptr;
    memCounter_ = s2 == ERROR_SUCCESS ? mem : nullptr;
    PdhCollectQueryData(q);
    return true;
}

static std::vector<std::pair<std::wstring, double>> readArray(PDH_HCOUNTER counter) {
    std::vector<std::pair<std::wstring, double>> out;
    DWORD bytes = 0, count = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr);
    if (st != PDH_MORE_DATA || bytes == 0) return out;
    std::vector<BYTE> buf(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items) != ERROR_SUCCESS) return out;
    for (DWORD i = 0; i < count; ++i)
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
            out.emplace_back(items[i].szName, items[i].FmtValue.doubleValue);
    return out;
}

void SystemMonitor::sample() {
    SystemSample s;
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        ULONGLONG i = ft(idle), k = ft(kernel), u = ft(user);
        ULONGLONG di = i - prevIdle_, dk = k - prevKernel_, du = u - prevUser_;
        if (prevKernel_ != 0 && dk + du > 0) s.cpuUsage = std::clamp(1.0 - double(di) / double(dk + du), 0.0, 1.0);
        prevIdle_ = i;
        prevKernel_ = k;
        prevUser_ = u;
    }
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps)) {
        s.onBattery = ps.ACLineStatus == 0;
        s.batteryPercent = ps.BatteryLifePercent <= 100 ? int(ps.BatteryLifePercent) : -1;
    }
    if (!pdhTried_) pdhOk_ = initPdh();
    LUID luid{};
    bool luidSet;
    {
        std::lock_guard lock(mutex_);
        luid = luid_;
        luidSet = luidSet_;
        s.gpuDedicatedTotalMB = totalMB_;
    }
    if (pdhOk_ && PdhCollectQueryData(static_cast<PDH_HQUERY>(query_)) == ERROR_SUCCESS) {
        wchar_t luidTag[64];
        std::swprintf(luidTag, 64, L"luid_0x%08X_0x%08X", unsigned(luid.HighPart), unsigned(luid.LowPart));
        if (gpuCounter_) {
            // Sum per engine (several processes share an engine), take the busiest engine.
            std::map<std::wstring, double> perEngine;
            for (auto& [name, v] : readArray(static_cast<PDH_HCOUNTER>(gpuCounter_))) {
                if (luidSet && name.find(luidTag) == std::wstring::npos) continue;
                size_t p = name.find(L"luid_");
                std::wstring engine = p == std::wstring::npos ? name : name.substr(p);
                perEngine[engine] += v;
            }
            double best = -1;
            for (auto& [e, v] : perEngine) best = std::max(best, v);
            if (best >= 0) s.gpuUsage = std::clamp(best / 100.0, 0.0, 1.0);
        }
        if (memCounter_) {
            double total = 0;
            bool any = false;
            for (auto& [name, v] : readArray(static_cast<PDH_HCOUNTER>(memCounter_))) {
                if (luidSet && name.find(luidTag) == std::wstring::npos) continue;
                total += v;
                any = true;
            }
            if (any) s.gpuDedicatedMB = total / (1024.0 * 1024.0);
        }
    }
    std::lock_guard lock(mutex_);
    last_ = s;
}

SystemSample SystemMonitor::latest() const {
    std::lock_guard lock(mutex_);
    return last_;
}

} // namespace bgn
