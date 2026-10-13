#include "../../include/util/performance_stats.h"

#include <algorithm>
#include <fstream>
#include <unistd.h>

uint64_t PerformanceStats::ElapsedNs(const Clock::time_point& begin,
    const Clock::time_point& end) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - begin).count();
}

double PerformanceStats::NsToMs(uint64_t ns) {
    return static_cast<double>(ns) / 1000000.0;
}

double PerformanceStats::BytesToMB(uint64_t bytes) {
    return static_cast<double>(bytes) / 1000000.0;
}

uint64_t PerformanceStats::CurrentRssKB() {
    std::ifstream statm("/proc/self/statm");
    uint64_t virtual_pages = 0;
    uint64_t resident_pages = 0;
    if (!(statm >> virtual_pages >> resident_pages)) {
        return 0;
    }
    return resident_pages * static_cast<uint64_t>(sysconf(_SC_PAGESIZE)) /
        1024;
}

PeakMemoryMonitor::~PeakMemoryMonitor() {
    Stop();
}

void PeakMemoryMonitor::Start() {
    peak_kb_.store(PerformanceStats::CurrentRssKB(),
        std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);
    sampler_ = std::thread([this]() {
        while (running_.load(std::memory_order_acquire)) {
            const uint64_t current_kb = PerformanceStats::CurrentRssKB();
            uint64_t old_peak = peak_kb_.load(std::memory_order_relaxed);
            while (current_kb > old_peak &&
                !peak_kb_.compare_exchange_weak(old_peak, current_kb,
                    std::memory_order_relaxed)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
}

uint64_t PeakMemoryMonitor::Stop() {
    if (running_.exchange(false, std::memory_order_acq_rel) &&
        sampler_.joinable()) {
        sampler_.join();
    }
    const uint64_t final_kb = PerformanceStats::CurrentRssKB();
    return std::max(final_kb, peak_kb_.load(std::memory_order_relaxed));
}
