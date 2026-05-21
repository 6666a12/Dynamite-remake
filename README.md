# Dynamite-remake

## 一个对社区音乐游戏 Dynamite 的重写

---

技术栈

| 层级 | 技术 | 版本 | 用途 |
|------|------|------|------|
| C++ Core | C++ | C++17 | 唯一跨平台核心 |
| | SDL3 | 3.2.x | 窗口/输入/GL上下文/线程 |
| | OpenGL ES | 3.0 | 渲染 |
| | miniaudio | 0.11.x | 音频解码/播放/采样时钟 |
| | glm | 0.9.9+ | 数学库（投影矩阵） |
| | nlohmann/json | 3.11+ | 配置/JSON谱面解析 |
| | stb_image | 2.28+ | PNG/JPG 解码 |
| | stb_truetype | 1.26+ | 动态字体光栅化 |
| Go DataLayer | Go | 1.22+ | gomobile bind |
| | mattn/go-sqlite3 | 1.14.22 | SQLite 驱动 |
| Android | Kotlin + NDK | API 24+ | SLDActivity 启动壳 |
| iOS | Objective-C++ | 13.0+ | UIApplication 入口 |

---
