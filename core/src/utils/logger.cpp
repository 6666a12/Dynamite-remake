/**
 * Logger 类实现 —— 基础日志输出到 stderr
 * 
 * 设计说明：
 * 1. 当前使用最简实现（fprintf 到 stderr），零依赖、零分配
 * 2. 后续可扩展为：写入文件、上传日志服务器、Android __android_log_print 等
 * 3. 线程安全：当前未加锁，若需多线程安全可在此添加 mutex
 */

#include "utils/logger.h"
#include <cstdio>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

// 静态成员变量定义：默认日志级别为 INFO
LogLevel Logger::current_level_ = LogLevel::INFO;

/**
 * 设置全局日志级别，低于此级别的日志将被丢弃
 */
void Logger::setLevel(LogLevel level) {
    current_level_ = level;
}

/**
 * 输出一条日志到 stderr
 * 
 * @param level 日志级别，用于前缀和过滤
 * @param msg   已格式化的日志正文
 */
void Logger::log(LogLevel level, const std::string& msg) {
    // 级别过滤：低于当前设置级别的日志直接忽略
    if (level < current_level_) {
        return;
    }

    const char* prefix = "";
    switch (level) {
        case LogLevel::DEBUG: prefix = "[DEBUG] "; break;
        case LogLevel::INFO:  prefix = "[INFO]  "; break;
        case LogLevel::WARN:  prefix = "[WARN]  "; break;
        case LogLevel::ERROR: prefix = "[ERROR] "; break;
    }

    // 直接输出到标准错误流，保证即使 stdout 被重定向，错误信息仍可显示
#if defined(__ANDROID__)
    int android_prio = ANDROID_LOG_INFO;
    switch (level) {
        case LogLevel::DEBUG: android_prio = ANDROID_LOG_DEBUG; break;
        case LogLevel::INFO:  android_prio = ANDROID_LOG_INFO;  break;
        case LogLevel::WARN:  android_prio = ANDROID_LOG_WARN;  break;
        case LogLevel::ERROR: android_prio = ANDROID_LOG_ERROR; break;
    }
    __android_log_print(android_prio, "Dynamite", "%s%s", prefix, msg.c_str());
#else
    fprintf(stderr, "%s%s\n", prefix, msg.c_str());
#endif
}
