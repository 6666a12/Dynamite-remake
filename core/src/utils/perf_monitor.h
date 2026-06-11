#pragma once
#include <cstdint>
#include <array>
#include <algorithm>
#include <chrono>

/**
 * 轻量级性能监控器 — 开发模式帧时间统计
 * 
 * 用法:
 *   PerfMonitor pm;
 *   pm.beginFrame();
 *   // ... update + render ...
 *   pm.endFrame();
 *   if (pm.shouldReport()) {
 *       Logger::debug("Frame time: avg=%.1f max=%.1f", pm.avgMs(), pm.maxMs());
 *   }
 */
class PerfMonitor {
public:
    static constexpr int kWindowSize = 60;  // 最近 60 帧

    void beginFrame() {
        begin_ = nowUs();
    }

    void endFrame() {
        int64_t elapsed = nowUs() - begin_;
        ring_[idx_ % kWindowSize] = elapsed;
        idx_++;
        count_ = std::min(count_ + 1, kWindowSize);
    }

    /// 最近 N 帧平均耗时 (ms)
    float avgMs() const {
        if (count_ == 0) return 0.0f;
        int64_t sum = 0;
        for (int i = 0; i < count_; i++) sum += ring_[i];
        return static_cast<float>(sum) / static_cast<float>(count_) / 1000.0f;
    }

    /// 最近 N 帧最大耗时 (ms)
    float maxMs() const {
        if (count_ == 0) return 0.0f;
        int64_t mx = ring_[0];
        for (int i = 1; i < count_; i++) mx = std::max(mx, ring_[i]);
        return static_cast<float>(mx) / 1000.0f;
    }

    /// 是否应该输出报告（每 120 帧一次）
    bool shouldReport() const {
        return (idx_ % 120) == 0 && count_ >= kWindowSize;
    }

    int frameCount() const { return idx_; }

private:
    static int64_t nowUs() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
    }

    std::array<int64_t, kWindowSize> ring_{};
    int idx_ = 0;
    int count_ = 0;
    int64_t begin_ = 0;
};
