#include "cash_sloth_diagnostics.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace cashsloth {

namespace {
struct FileLogger {
    std::ofstream stream;

    explicit FileLogger(const std::string& path) {
        stream.open(path, std::ios::app);
        if (stream.is_open()) {
            auto now = std::chrono::system_clock::now();
            std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &nowTime);
#else
            localtime_r(&nowTime, &local);
#endif
            stream << "\n--- Diagnostics session "
                   << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
                   << " ---\n";
        }
    }
};

FileLogger& getLogger(const std::string& path) {
    static FileLogger logger(path);
    return logger;
}

std::string buildTimingMessage(std::string_view area, double elapsedMs, double budgetMs) {
    std::ostringstream message;
    message << area << " dauerte " << std::fixed << std::setprecision(2) << elapsedMs << "ms";
    if (budgetMs > 0.0) {
        message << " (Budget " << std::fixed << std::setprecision(2) << budgetMs << "ms)";
    }
    return message.str();
}
}  // namespace

DiagnosticsMonitor& DiagnosticsMonitor::instance() {
    static DiagnosticsMonitor monitor;
    return monitor;
}

DiagnosticsMonitor::DiagnosticsMonitor()
    : start_(std::chrono::steady_clock::now()),
      logPath_("cash_sloth_diagnostics.log") {}

void DiagnosticsMonitor::recordInfo(std::string_view message) {
    logLine("INFO", message);
}

void DiagnosticsMonitor::recordWarning(std::string_view message) {
    logLine("WARN", message);
}

void DiagnosticsMonitor::recordError(std::string_view message) {
    logLine("ERROR", message);
}

void DiagnosticsMonitor::recordInefficiency(std::string_view area, double elapsedMs, double budgetMs) {
    std::ostringstream msg;
    msg << "Ineffizienz erkannt: " << buildTimingMessage(area, elapsedMs, budgetMs);
    logLine("PERF", msg.str());
}

void DiagnosticsMonitor::recordTiming(std::string_view area, double elapsedMs, double budgetMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& stats = timing_[std::string(area)];
    stats.count += 1;
    stats.totalMs += elapsedMs;
    stats.maxMs = std::max(stats.maxMs, elapsedMs);
    if (budgetMs > 0.0) {
        stats.budgetMs = budgetMs;
        if (elapsedMs > budgetMs) {
            stats.overBudgetCount += 1;
        }
    }
    if (budgetMs > 0.0 && elapsedMs > budgetMs) {
        recordInefficiency(area, elapsedMs, budgetMs);
    }
}

void DiagnosticsMonitor::flushSummary() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (timing_.empty()) {
        return;
    }
    logLine("INFO", "Performance-Zusammenfassung:");
    for (const auto& entry : timing_) {
        const auto& stats = entry.second;
        double average = stats.count ? (stats.totalMs / static_cast<double>(stats.count)) : 0.0;
        std::ostringstream summary;
        summary << "  " << entry.first
                << " | Aufrufe: " << stats.count
                << " | Durchschnitt: " << std::fixed << std::setprecision(2) << average << "ms"
                << " | Max: " << std::fixed << std::setprecision(2) << stats.maxMs << "ms";
        if (stats.budgetMs > 0.0) {
            summary << " | Budget: " << std::fixed << std::setprecision(2) << stats.budgetMs << "ms"
                    << " | Über Budget: " << stats.overBudgetCount;
        }
        logLine("INFO", summary.str());
    }
}

DiagnosticsMonitor::ScopedTimer::ScopedTimer(std::string_view area, double budgetMs)
    : area_(area), budgetMs_(budgetMs), start_(std::chrono::steady_clock::now()) {}

DiagnosticsMonitor::ScopedTimer::~ScopedTimer() {
    const auto end = std::chrono::steady_clock::now();
    const double elapsedMs =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - start_).count();
    DiagnosticsMonitor::instance().recordTiming(area_, elapsedMs, budgetMs_);
}

void DiagnosticsMonitor::logLine(std::string_view level, std::string_view message) {
    auto& logger = getLogger(logPath_);
    if (!logger.stream.is_open()) {
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_);
    logger.stream << '[' << elapsed.count() << "ms][" << level << "] " << message << '\n';
    logger.stream.flush();
}

}  // namespace cashsloth
