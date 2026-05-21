#pragma once
#include <atomic>
#include <cstdint>

/**
 * 音频采样时钟 —— 音游判定的唯一时间基准
 * 
 * 设计决策：
 * 1. 使用 std::atomic<int64_t> 保证音频回调线程与主渲染线程间的线程安全
 * 2. 以已播放采样数为单位，避免浮点误差累积
 * 3. 仅在查询时转换为毫秒，不存储毫秒值
 */
class AudioClock {
public:
    void init(int sample_rate, int channels);

    // 在 miniaudio 数据回调中累加已播放采样数（每通道）
    void onSamplesPlayed(int frames);

    // 线程安全，返回自播放开始的毫秒数（double）
    double nowMs() const;

    // 获取当前采样序号（用于精确同步）
    int64_t nowSamples() const;

private:
    std::atomic<int64_t> sample_count_{0};
    int sample_rate_ = 44100;
    int channels_ = 2;
};
