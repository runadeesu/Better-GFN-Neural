#pragma once
// Common Win32 include + small helpers.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl/client.h>

#include <string>

namespace bgn {

using Microsoft::WRL::ComPtr;

std::string hrToString(HRESULT hr);
std::string lastErrorString(DWORD err = GetLastError());

// Seconds from the high resolution performance counter.
double qpcSeconds();
double qpcToSeconds(int64_t qpc);

// RAII for kernel handles
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE h) : h_(h) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    UniqueHandle& operator=(UniqueHandle&& o) noexcept {
        if (this != &o) {
            reset();
            h_ = o.h_;
            o.h_ = nullptr;
        }
        return *this;
    }
    void reset(HANDLE h = nullptr) {
        if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
        h_ = h;
    }
    HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ && h_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE h_ = nullptr;
};

} // namespace bgn
