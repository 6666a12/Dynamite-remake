/**
 * @file audio_clock.cpp
 * @brief 音频采样时钟实现 —— 音游判定的唯一时间基准
 *
 * 设计要点：
 * 1. 所有状态使用 std::atomic 保护，确保音频回调线程与主线程的线程安全
 * 2. 内部以"总采样数"为单位累加，查询时才转换为毫秒，避免浮点误差累积
 * 3. 不依赖系统时钟，完全由音频设备驱动的硬件时钟
 */

#include "engine/audio_clock.h"
#include <cmath> // 用于 std::round

/**
 * @brief 初始化音频时钟参数
 * @param sample_rate 采样率（Hz），常见值为 44100 或 48000
 * @param channels    声道数，通常为 2（立体声）
 *
 * 说明：
 * - 采样率和声道数在初始化后通常不变，若需动态调整须重新 init
 * - 原子变量 sample_count_ 会自动清零（构造时初始化为 0）
 */
void AudioClock::init(int sample_rate, int channels) {
    // 使用 release 语义写入，确保后续线程能立即看到更新后的值
    sample_rate_ = sample_rate;
    channels_    = channels;

    // 显式将已播放采样数归零，防止重复 init 时残留旧值
    sample_count_.store(0, std::memory_order_release);
}

/**
 * @brief 在音频数据回调中累加已播放帧数
 * @param frames 本回调周期内每声道播放的帧数
 *
 * 线程安全：
 * - 本函数仅在 miniaudio 的音频回调线程中调用
 * - 使用 memory_order_relaxed 即可，因为与主线程的同步通过 nowMs()/nowSamples() 的 acquire 语义完成
 *
 * 说明：
 * - 音游判定基于时间，与声道数无关，因此直接累加帧数
 * - 1 帧 = 1 个采样点（每声道），44100 帧 = 1 秒 @ 44.1kHz
 */
void AudioClock::onSamplesPlayed(int frames) {
    if (frames <= 0) {
        return; // 无效帧数直接忽略
    }

    // 直接累加帧数（不乘声道数），时间计算与声道数无关
    sample_count_.fetch_add(static_cast<int64_t>(frames), std::memory_order_relaxed);
}

/**
 * @brief 获取自播放开始以来的毫秒数
 * @return double 毫秒值，可包含小数部分（亚毫秒精度）
 *
 * 线程安全：
 * - 可在任意线程调用，使用 memory_order_acquire 确保看到最新的采样数
 *
 * 计算公式：
 *   ms = frames / sample_rate * 1000.0
 *      = frames * 1000.0 / sample_rate
 */
double AudioClock::nowMs() const {
    const int64_t frames = sample_count_.load(std::memory_order_acquire);

    if (sample_rate_ <= 0) {
        return 0.0;
    }

    return static_cast<double>(frames) * 1000.0 / static_cast<double>(sample_rate_);
}

/**
 * @brief 获取当前总采样序号（用于精确同步）
 * @return int64_t 自播放开始以来所有声道的累计采样数
 *
 * 典型用途：
 * - 与音频解码器的 PCM 帧位置做精确对齐
 * - 在混音器中进行采样级同步
 */
int64_t AudioClock::nowSamples() const {
    return sample_count_.load(std::memory_order_acquire);
}
