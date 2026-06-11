/**
 * @file audio_engine.cpp
 * @brief 基于 miniaudio 的跨平台音频引擎实现
 *
 * 设计要点：
 * 1. 使用 miniaudio 底层设备 API，精确控制数据回调时机
 * 2. 音频解码与设备播放分离，支持流式读取避免大文件一次性加载
 * 3. 在 data_callback 中同时完成混音与音频时钟更新，确保时钟与输出严格同步
 * 4. 所有 miniaudio 对象使用智能指针管理内存，通过 RAII 在 shutdown 中完成反初始化
 */

// 在一个且仅一个 .cpp 中定义 miniaudio 的实现
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "engine/audio_engine.h"
#include "utils/logger.h"
#include <algorithm> // std::fill
#include <cstring>   // memset

/**
 * @brief 初始化音频引擎与播放设备
 * @param sample_rate    采样率（Hz），默认 44100
 * @param channels       声道数，默认 2（立体声）
 * @param buffer_frames  每周期缓冲帧数，默认 128（低延迟）
 * @return true 初始化成功，false 失败
 *
 * 实现细节：
 * - 先初始化 AudioClock，使其与设备参数保持一致
 * - 再配置并创建 miniaudio 播放设备
 * - 设备未启动前不会进入数据回调
 */
bool AudioEngine::init(int sample_rate, int channels, int buffer_frames) {
    // 1. 初始化音频时钟，建立采样率/声道数基准
    clock_.init(sample_rate, channels);

    // 2. 配置 miniaudio 播放设备
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;      // 使用 float32 格式，与混音函数一致
    config.playback.channels = static_cast<ma_uint32>(channels);
    config.sampleRate        = static_cast<ma_uint32>(sample_rate);
    config.periodSizeInFrames = static_cast<ma_uint32>(buffer_frames);
    config.dataCallback      = data_callback;       // 注册静态回调
    config.pUserData         = this;                // 将实例指针传给回调，用于反查

    // 3. 创建设备对象（使用智能指针管理裸内存）
    device_ = std::make_unique<ma_device>();
    if (ma_device_init(nullptr, &config, device_.get()) != MA_SUCCESS) {
        device_.reset(); // 清理未成功初始化的设备内存
        return false;
    }

    return true;
}

/**
 * @brief 关闭音频引擎，释放所有资源
 *
 * 释放顺序：
 * 1. 先停止设备（若正在播放），防止回调在销毁期间被触发
 * 2. 反初始化设备与解码器（释放其内部持有的系统资源）
 * 3. 释放 C++ 层分配的内存（由 unique_ptr 自动完成）
 */
void AudioEngine::shutdown() {
    if (device_) {
        ma_device_stop(device_.get());     // 停止音频流
        ma_device_uninit(device_.get());   // 释放设备占用的系统资源
        device_.reset();                    // 释放 ma_device 结构体内存
    }

    if (decoder_) {
        ma_decoder_uninit(decoder_.get()); // 关闭音频文件与解码器
        decoder_.reset();                   // 释放 ma_decoder 结构体内存
    }

    is_playing_ = false;
}

/**
 * @brief 加载指定路径的音频文件作为当前曲目
 * @param path 音频文件路径（支持 mp3、ogg、wav 等 miniaudio 支持的格式）
 * @return true 加载成功，false 失败
 *
 * 说明：
 * - 若已有旧解码器，会自动被新解码器替换（旧解码器先 uninit）
 * - 解码器输出格式被强制转换为与设备一致的 f32/2ch/44100
 */
bool AudioEngine::loadSong(const std::string& path) {
    reached_end_ = false;  // 新歌曲，重置结束标志
    if (!device_) {
        return false; // 设备未初始化，无法加载
    }

    // 若存在旧解码器，先安全释放
    if (decoder_) {
        ma_decoder_uninit(decoder_.get());
        decoder_.reset();
    }

    // 创建新解码器
    decoder_ = std::make_unique<ma_decoder>();
    ma_decoder_config decoderConfig = ma_decoder_config_init(
        ma_format_f32,
        device_->playback.channels,
        device_->sampleRate
    );

    ma_result init_result = ma_decoder_init_file(path.c_str(), &decoderConfig, decoder_.get());
    if (init_result != MA_SUCCESS) {
        Logger::error("ma_decoder_init_file failed: {} (path={})", static_cast<int>(init_result), path);
        decoder_.reset();
        return false;
    }

    // 打印解码器内部格式信息（用于诊断）
    Logger::info("Decoder init OK: fmt={}, ch={}, sr={}",
                 static_cast<int>(decoder_->outputFormat),
                 decoder_->outputChannels,
                 decoder_->outputSampleRate);

    // 验证解码器能实际读出数据（某些格式 init 成功但 read 返回 0）
    float test_buf[256];
    ma_uint64 test_frames = 0;
    ma_result test_result = ma_decoder_read_pcm_frames(decoder_.get(), test_buf, 128, &test_frames);
    if (test_result != MA_SUCCESS || test_frames == 0) {
        Logger::error("Decoder test read failed: result={}, frames={}", static_cast<int>(test_result), test_frames);
        decoder_.reset();
        return false;
    }
    // 读完后 seek 回开头
    ma_decoder_seek_to_pcm_frame(decoder_.get(), 0);

    Logger::info("Audio loaded: {}", path);
    return true;
}

/**
 * @brief 开始或恢复播放
 *
 * 注意：
 * - 若设备已启动，重复调用无额外效果
 * - 播放状态通过 is_playing_ 标记，用于混音回调中的静音控制
 */
void AudioEngine::play() {
    if (device_ && !is_playing_) {
        ma_device_start(device_.get());
        is_playing_ = true;
    }
}

/**
 * @brief 暂停播放
 *
 * 说明：
 * - 暂停会停止设备的数据回调，但不会重置解码器位置
 * - 再次调用 play() 可从暂停处恢复
 */
void AudioEngine::pause() {
    if (device_ && is_playing_) {
        ma_device_stop(device_.get());
        is_playing_ = false;
    }
}

/**
 * @brief 停止播放并复位到开头
 *
 * 说明：
 * - 停止后会将解码器 PCM 帧位置重置为 0
 * - 音频时钟的采样计数不会自动清零（由调用方在切歌时处理）
 */
void AudioEngine::stop() {
    if (device_) {
        ma_device_stop(device_.get());
        is_playing_ = false;
    }
    if (decoder_) {
        ma_decoder_seek_to_pcm_frame(decoder_.get(), 0);
    }
}

/**
 * @brief 查询当前是否处于播放状态
 * @return true 正在播放，false 已暂停或停止
 */
bool AudioEngine::isPlaying() const {
    return is_playing_;
}

bool AudioEngine::hasReachedEnd() const {
    // 使用 reached_end_ 标志（在 mixAudio 中设置），
    // 避免依赖 ma_data_source_get_length 对解码器的不可靠返回
    if (!decoder_) return true;
    return reached_end_;
}

/**
 * @brief 混音主函数 —— 从解码器读取 PCM 并填充到输出缓冲区
 * @param output     输出缓冲区（float32 格式，交错排列 L/R/L/R...）
 * @param frameCount 需要填充的帧数（每帧包含 playback.channels 个采样）
 *
 * 实现细节：
 * - 若未加载解码器或未在播放状态，输出静音（全 0）
 * - 若歌曲已读到末尾，剩余部分填充静音
 */
void AudioEngine::mixAudio(float* output, uint32_t frameCount) {
    if (!decoder_ || !is_playing_) {
        // 安全获取声道数，若设备未就绪则默认按 2 声道填充
        const ma_uint32 channels = device_ ? device_->playback.channels : 2;
        const size_t totalSamples = static_cast<size_t>(frameCount) * channels;

        // 边界检查：防止 frameCount 异常导致溢出
        if (output && totalSamples > 0) {
            std::fill(output, output + totalSamples, 0.0f);
        }
        return;
    }

    // 从解码器读取 PCM 帧
    ma_uint64 framesRead = 0;
    ma_result result = ma_decoder_read_pcm_frames(decoder_.get(), output, frameCount, &framesRead);

    // 若读到文件末尾或发生错误，将剩余缓冲区填充为静音
    if (result != MA_SUCCESS || framesRead < frameCount) {
        reached_end_ = true;  // 标记播放结束
        const ma_uint32 channels = device_->playback.channels;
        const size_t samplesRead = static_cast<size_t>(framesRead) * channels;
        const size_t totalSamples = static_cast<size_t>(frameCount) * channels;

        if (output && totalSamples > samplesRead) {
            std::fill(output + samplesRead, output + totalSamples, 0.0f);
        }
    }
}

/**
 * @brief miniaudio 数据回调入口（静态方法）
 * @param pDevice   miniaudio 设备指针
 * @param pOutput   输出缓冲区
 * @param pInput    输入缓冲区（播放设备不使用，可为 nullptr）
 * @param frameCount 本周期需要填充的帧数
 *
 * 线程安全：
 * - 本函数在 miniaudio 内部的高优先级音频线程中执行
 * - 仅调用 mixAudio（无锁）和 clock_.onSamplesPlayed（原子操作），避免任何阻塞
 */
void AudioEngine::data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput; // 播放设备不使用输入，显式忽略防止编译器警告

    // 通过 pUserData 获取 AudioEngine 实例指针
    AudioEngine* engine = static_cast<AudioEngine*>(pDevice->pUserData);
    if (!engine) {
        return;
    }

    // 将 void* 转换为 float*（因为设备格式已固定为 f32）
    float* pOutputF32 = static_cast<float*>(pOutput);

    // 执行混音
    engine->mixAudio(pOutputF32, frameCount);

    // 更新音频时钟：通知已播放的帧数
    // 注意：frameCount 是"每声道"的帧数，AudioClock 内部会乘以 channels
    engine->clock_.onSamplesPlayed(static_cast<int>(frameCount));
}
