#pragma once
#include "audio_clock.h"
#include <string>
#include <memory>
#include <miniaudio.h>

/**
 * 音频引擎封装 —— 基于 miniaudio
 * 
 * 设计决策：
 * 1. 使用 miniaudio 的底层设备 API，而非高层引擎 API，以精确控制回调时机
 * 2. 所有音频解码在单独线程进行，主线程仅查询时钟
 * 3. 支持流式播放，避免大文件一次性加载
 */
class AudioEngine {
public:
    bool init(int sample_rate = 44100, int channels = 2, int buffer_frames = 128);
    void shutdown();

    bool loadSong(const std::string& path);
    void play();
    void pause();
    void stop();
    bool isPlaying() const;

    AudioClock& clock() { return clock_; }
    const AudioClock& clock() const { return clock_; }

    // 混音回调，由 miniaudio 数据回调调用
    void mixAudio(float* output, uint32_t frameCount);

private:
    AudioClock clock_;
    std::unique_ptr<ma_device> device_;
    std::unique_ptr<ma_decoder> decoder_;
    bool is_playing_ = false;

    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
};
