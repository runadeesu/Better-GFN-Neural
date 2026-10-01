#pragma once
// Fixed-capacity sample window with mean / max / percentile queries and a
// timestamp-based rate counter. Portable, header-only.

#include <algorithm>
#include <cstddef>
#include <deque>
#include <vector>

namespace bgn {

class RollingStats {
public:
    explicit RollingStats(size_t capacity = 240) : capacity_(capacity) {}

    void add(double v) {
        samples_.push_back(v);
        if (samples_.size() > capacity_) samples_.pop_front();
    }
    void clear() { samples_.clear(); }
    size_t count() const { return samples_.size(); }
    bool empty() const { return samples_.empty(); }
    double last() const { return samples_.empty() ? 0.0 : samples_.back(); }

    double mean() const {
        if (samples_.empty()) return 0.0;
        double s = 0;
        for (double v : samples_) s += v;
        return s / double(samples_.size());
    }
    double max() const {
        double m = 0;
        for (double v : samples_) m = std::max(m, v);
        return m;
    }
    double min() const {
        if (samples_.empty()) return 0.0;
        double m = samples_.front();
        for (double v : samples_) m = std::min(m, v);
        return m;
    }
    // p in [0,1]; nearest-rank percentile.
    double percentile(double p) const {
        if (samples_.empty()) return 0.0;
        std::vector<double> sorted(samples_.begin(), samples_.end());
        std::sort(sorted.begin(), sorted.end());
        p = std::clamp(p, 0.0, 1.0);
        size_t idx = size_t(p * double(sorted.size() - 1) + 0.5);
        return sorted[idx];
    }
    const std::deque<double>& samples() const { return samples_; }

private:
    size_t capacity_;
    std::deque<double> samples_;
};

// Counts events over a sliding time window (seconds) => events per second.
class RateCounter {
public:
    explicit RateCounter(double windowSeconds = 1.0) : window_(windowSeconds) {}
    void tick(double nowSeconds) {
        times_.push_back(nowSeconds);
        prune(nowSeconds);
    }
    double rate(double nowSeconds) {
        prune(nowSeconds);
        if (times_.size() < 2) return 0.0;
        double span = nowSeconds - times_.front();
        if (span < window_ * 0.5) span = window_ * 0.5;
        return double(times_.size()) / std::max(span, 1e-6);
    }
    // Average interval between the most recent events (seconds), 0 if unknown.
    double meanInterval() const {
        if (times_.size() < 2) return 0.0;
        return (times_.back() - times_.front()) / double(times_.size() - 1);
    }
    void reset() { times_.clear(); }

private:
    void prune(double now) {
        while (!times_.empty() && now - times_.front() > window_) times_.pop_front();
    }
    double window_;
    std::deque<double> times_;
};

} // namespace bgn
