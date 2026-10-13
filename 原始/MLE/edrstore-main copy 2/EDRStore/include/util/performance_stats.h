#ifndef EDRSTORE_PERFORMANCE_STATS_H
#define EDRSTORE_PERFORMANCE_STATS_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

class PerformanceStats {
    public:
        using Clock = std::chrono::steady_clock;

        static uint64_t ElapsedNs(const Clock::time_point& begin,
            const Clock::time_point& end = Clock::now());
        static double NsToMs(uint64_t ns);
        static double BytesToMB(uint64_t bytes);
        static uint64_t CurrentRssKB();
};

// Samples server RSS while one request is active.  Sampling is deliberately
// coarse (10 ms) so the observer cannot materially affect the algorithms.
class PeakMemoryMonitor {
    private:
        std::atomic<bool> running_{false};
        std::atomic<uint64_t> peak_kb_{0};
        std::thread sampler_;

    public:
        PeakMemoryMonitor() = default;
        ~PeakMemoryMonitor();
        void Start();
        uint64_t Stop();
};

#endif
