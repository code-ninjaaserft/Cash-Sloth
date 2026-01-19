#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cashsloth {

struct TimingStats {
    std::size_t count = 0;
    double totalMs = 0.0;
    double maxMs = 0.0;
    std::size_t overBudgetCount = 0;
    double budgetMs = 0.0;
};

class DiagnosticsMonitor {
public:
    static DiagnosticsMonitor& instance();

    void recordInfo(std::string_view message);
    void recordWarning(std::string_view message);
    void recordError(std::string_view message);
    void recordInefficiency(std::string_view area, double elapsedMs, double budgetMs);
    void recordTiming(std::string_view area, double elapsedMs, double budgetMs);
    void flushSummary();

    class ScopedTimer {
    public:
        ScopedTimer(std::string_view area, double budgetMs = 0.0);
        ~ScopedTimer();

        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        std::string area_;
        double budgetMs_ = 0.0;
        std::chrono::steady_clock::time_point start_;
    };

private:
    DiagnosticsMonitor();

    void logLine(std::string_view level, std::string_view message);

    std::mutex mutex_{};
    std::unordered_map<std::string, TimingStats> timing_{};
    std::chrono::steady_clock::time_point start_{};
    std::string logPath_;
};

}  // namespace cashsloth
