# Dynamite Rebuild — 架构设计书 v3.0

> **项目**：基于 Dynamite（Tuner Games）社区音游的 iOS / Android 双端跨平台重构
> **技术栈**：C++17 GameCore + OpenGL ES 3.0 + miniaudio + Go DataLayer (gomobile)
> **设计原则**：判定以音频时钟为唯一基准 · 社区生态零侵入兼容 · UI 像素级还原 Dynamite 风格
> **版本**：v3.0 | **日期**：2026-06-09

---

## 目录

- [1. 总体架构](#1-总体架构)
- [2. 核心设计决策](#2-核心设计决策)
- [3. 模块职责与边界](#3-模块职责与边界)
- [4. 判定系统设计](#4-判定系统设计)
- [5. 渲染系统设计](#5-渲染系统设计)
- [6. 场景与 UI 系统设计](#6-场景与-ui-系统设计)
- [7. 数据层设计](#7-数据层设计)
- [8. 社区兼容策略](#8-社区兼容策略)
- [9. Event 活动系统设计](#9-event-活动系统设计)
- [10. 安全架构](#10-安全架构)
- [11. 资源与构建系统](#11-资源与构建系统)
- [12. 当前实现状态与重构路线](#12-当前实现状态与重构路线)
- [13. 代码审计发现：隐性风险与设计债务](#13-代码审计发现隐性风险与设计债务)
- [14. 重构执行建议](#14-重构执行建议)
- [15. 硬约束清单](#15-硬约束清单)

---

## 1. 总体架构

### 1.1 分层模型

```
┌────────────────────────────────────────────┐
│  Platform Shell (最小化)                    │
│  Android: Kotlin SDLActivity · iOS: ObjC++ │
│  职责：启动 SDL3、初始化 Go DataLayer、      │
│        传递 assets/db 路径给 C++ Core       │
├────────────────────────────────────────────┤
│  C++ GameCore (跨平台核心，独占渲染/音频/判定) │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌───────────┐ │
│  │渲染层 │ │音频层 │ │判定层 │ │场景管理层  │ │
│  │GL ES │ │minia │ │Judge │ │Scene Stack │ │
│  │3.0   │ │udio  │ │Engine│ │(状态机)    │ │
│  └──────┘ └──┬───┘ └──────┘ └───────────┘ │
│              │ AudioClock (唯一时间基准)     │
├──────────────┼─────────────────────────────┤
│  Go DataLayer (嵌入式数据层, C ABI 桥接)     │
│  SQLite · 用户配置 · 成绩 · 歌曲元数据        │
│  编译为 AAR (Android) / Framework (iOS)     │
└────────────────────────────────────────────┘
```

### 1.2 核心数据流

```
触摸输入 → InputManager → JudgeEngine → 判定结果 → 结算统计
                ↑              ↑
          AudioClock      Chart (谱面数据)
                ↑              ↑
          AudioEngine → .ogg/.mp3 文件    ChartParser → DynaMaker 格式
                                                     → .chart 二进制缓存

渲染流程 (每帧)：
  AudioClock.nowMs() → 计算 note 位置 → GameplayRenderFrame
  → LaneCoordinateTransformer → 各 Renderer → RenderBatch.flush()
```

### 1.3 关键架构约束

- **判定时间基准唯一**：整个系统中，判定逻辑只信任 `AudioClock::nowMs()`，它是从 miniaudio 数据回调中累加的采样数除以采样率得出的。`SDL_GetTicks()` 和 `std::chrono` 仅用于非判定用途（UI 动画、场景过渡）。
- **渲染与判定分离**：判定引擎不关心 note 长什么样，渲染器不关心 note 怎么判。两者通过 `GameplayRenderFrame` 结构体交换数据。
- **Go 层只做数据**：Go 不参与任何实时音频/渲染/判定路径。C++ 对 Go 的调用必须是异步的，或者在调用点保证 < 2ms 返回。

---

## 2. 核心设计决策

### 2.1 为什么选择 C++ + Go 而非纯 C++ 或 Unity？

| 方案 | 优势 | 劣势 |
|---|---|---|
| **C++ + Go (当前)** | 判定/渲染极致性能；Go 擅长的数据/网络层天然适合移动端 gomobile 绑定 | 双语言维护成本；C-Go 桥接有开销 |
| 纯 C++ | 无桥接开销 | 数据层/网络层需要大量 C++ 胶水代码；移动端包管理繁琐 |
| Unity 还原 | 开发快 | Dynamite 原版用 Unity 但已停服多年，性能不佳正是重写动机之一 |

**决策**：保持 C++ + Go。C++ 专注实时路径（<16ms 帧预算），Go 专注非实时数据路径。

### 2.2 为什么使用 miniaudio 而非平台原生音频 API？

- **统一接口**：一份代码跑 Android AAudio/OpenSL ES 和 iOS AudioUnit
- **采样时钟内建**：miniaudio 的数据回调天然提供精确的已播放采样数，可以直接构建 `AudioClock`
- **低延迟**：支持 AAudio 的 `PERFORMANCE_MODE_LOW_LATENCY`，128 frames 缓冲 = ~2.9ms

### 2.3 为什么选择 OpenGL ES 3.0 而非 Vulkan/Metal？

- **跨平台覆盖**：GLES 3.0 在 Android API 24+ 和 iOS 13+ 上覆盖率 > 99%
- **2D 批处理场景**：音游的渲染需求以 2D 精灵为主，不需要 Vulkan/Metal 的底层控制力
- **SDL3 原生支持**：SDL3 的 OpenGL ES Context 创建在移动端是经过充分测试的路径

### 2.4 为什么要设计 .chart 二进制缓存格式？

DynaMaker 的 XML/JSON 谱面解析需要 50-500ms（涉及大量字符串解析和 BPM 计算），这在移动端冷启动时不可接受。首次解析后写入二进制 `.chart` 文件（直接 mmap + 结构体 overlay），后续加载 < 5ms。这是一种**空间换时间**的编译缓存策略。

### 2.5 场景栈 vs 场景切换

采用**栈式场景管理**而非简单的当前场景指针：
- `PUSH`：进入子场景（如 主菜单 → 选歌）
- `POP`：返回上一场景（按返回键）  
- `REPLACE`：替换当前场景（如 选歌 → 游玩）

好处：自然支持 Android 返回键语义，不需要每个场景手动记录"上一场景是谁"。

---

## 3. 模块职责与边界

### 3.1 C++ GameCore 模块地图

```
                    ┌─────────────┐
                    │   main.cpp   │  程序入口 + 主循环
                    └──────┬──────┘
                           │
          ┌────────────────┼────────────────┐
          │                │                │
    ┌─────▼─────┐   ┌──────▼──────┐   ┌─────▼─────┐
    │ Scene Stack│   │ InputManager│   │RenderBatch │
    │ (状态管理) │   │ (触摸收集)  │   │ (OpenGL)   │
    └─────┬─────┘   └──────┬──────┘   └─────┬─────┘
          │                │                │
    ┌─────▼────────────────▼────────────────▼─────┐
    │              SceneGameplay                   │
    │  ┌──────────┐ ┌────────┐ ┌───────────────┐  │
    │  │AudioEngine│ │JudgeEng│ │LaneCoordinate │  │
    │  │+AudioClk │ │ine     │ │Transformer    │  │
    │  └──────────┘ └────────┘ └───────┬───────┘  │
    │                                  │          │
    │  ┌──────────────────────────────▼────────┐  │
    │  │ 7 个 Renderer (Tap/Hold/Line/HUD/...) │  │
    │  └───────────────────────────────────────┘  │
    └─────────────────────────────────────────────┘
```

### 3.2 模块职责表

| 模块 | 负责 | 不负责 |
|---|---|---|
| **AudioClock** | 从 miniaudio 回调累加采样数，提供 `nowMs()` | 音频解码、播放、混音 |
| **AudioEngine** | 解码音频文件、启动/暂停播放、将 AudioClock 挂接到 miniaudio 回调 | 判定逻辑 |
| **JudgeEngine** | 以 audio_now_ms 为基准，判定 note 的 Perfect/Good/Miss，管理 Hold 和糊谱检测 | 触摸坐标收集、谱面加载 |
| **InputManager** | 将 SDL3 原始触摸事件转换为归一化坐标并打上 audio_now_ms 时间戳 | 判定逻辑（但提供 `queryTouchInRegion` 辅助） |
| **ChartParser** | 解析 DynaMaker XML/JSON/ZIP、读写 .chart 二进制缓存、BPM 计算 | 判定、渲染 |
| **LaneCoordinateTransformer** | 将三侧局部坐标 (lane_pos, distance) 转换为屏幕像素坐标 (x, y, w, h) | 谱面数据、判定 |
| **各 Renderer** | 接收 `NoteRenderCommand` / `HitEffectCommand`，调用 RenderBatch 绘制 | 坐标计算、判定 |
| **RenderBatch** | OpenGL ES 调用的唯一出口，管理 VBO/IBO/Shader，按纹理合批 | UI 布局、场景逻辑 |
| **SceneBase** | 生命周期接口 (init/enter/exit/update/render/handleInput) + 转场请求 | 具体场景逻辑 |
| **Go DataLayer** | SQLite CRUD、用户配置 JSON 序列化、成绩本地排行榜 | 实时路径（音频/判定/渲染） |
| **Go Bridge** | 将 Go 的 C ABI 包装为 C++ 可用的 `nlohmann::json` 接口 | 业务逻辑 |

### 3.3 各模块之间的"不要做"清单

- **JudgeEngine 不要**自己获取触摸数据——通过参数传入
- **Renderer 不要**自己计算 note 的屏幕位置——从 `LaneCoordinateTransformer` 获取
- **Scene 不要**直接调用 `gl*` 函数——全部通过 `RenderBatch`
- **AudioEngine 不要**关心判定窗口——它只负责播放和提供时钟
- **ChartParser 不要**缓存解析结果到自己的静态变量——由调用方决定何时写入 .chart 文件
- **Go DataLayer 不要**在 C++ 调用的同一线程做耗时 I/O——如果查询复杂，C++ 侧应异步调用

---

## 4. 判定系统设计

### 4.1 判定窗口

Dynamite 的判定标准（与社区私服一致）：

| 判定 | 窗口 | 得分权重 |
|---|---|---|
| **Perfect** | ±59ms | 100% |
| **Good** | ±90ms（触摸匹配上限） | 50% |
| **Miss** | > 150ms 自动 Miss | 0% |

窗口值可通过配置调整（如对于 CASUAL 难度可放宽），但默认值必须与 Dynamite 社区一致以保证 R 值公平性。

### 4.2 三侧判定区域

判定区域为**二维矩形**（非单点），基于 1920×1080 设计分辨率归一化：

- **LEFT 判线**：x = 108/1920 ± 24/1920，y 全屏高
- **DOWN 判线**：x 在左右判线之间，y = 945/1080 为底判线
- **RIGHT 判线**：x = 1812/1920 ± 24/1920，y 全屏高

设计要点：LEFT/RIGHT 的判线是**竖直**的，音符从屏幕中央向外侧水平/斜向飞入；DOWN 是传统下落式，判线是**水平**的。

### 4.3 SLIDE 的双机制设计

SLIDE（滑条）在 Dynamite 中有两种触发方式，共用同一个 note 判定窗口但行为不同：

| 类型 | 触发条件 | 特性 |
|---|---|---|
| **Catch-SLIDE** | 新按下手指 + delta ≥ -59ms | 参与糊谱检测（一个触控不能判多个不同时的 note） |
| **Swipe-SLIDE** | 任意触摸（包括滑动经过） | **不**参与糊谱检测（允许滑动穿过多个 SLIDE） |

判定窗口：SLIDE 使用**非对称**窗口——早了（delta < -59ms）不触发，等后续 Swipe；晚了（+59~+90ms）判 Late Good。这是 Dynamite 区别于传统音游的关键设计。

### 4.4 糊谱惩罚（Multi-hit Detection）

**问题**：玩家用一个手指同时按在所有轨道上，试图糊过快速连打。

**设计**：
- 同一个触控（finger_id）在同一帧可以判定**多个时间相同**的 note（正常多押）
- 但如果一个触控尝试匹配**时间不同**的多个 note → 该触控的所有候选判定强制 Miss
- 使用 `touch_first_matched_time` 追踪每个触控首次匹配的 note 时间

### 4.5 垂直投影（Vertical Judge）

**问题**：Dynamite 的 DOWN 轨道判线在底部（y ≈ 0.875），但玩家习惯在下半屏任意位置点击。如果只在下半屏的一个窄带内判定 DOWN，手感很差。

**设计**：
- 当触摸在屏幕下半部（y ≥ 0.5）且 x 在 DOWN 区域内时，自动生成一个"投影触摸"
- 投影触摸的 y 被设为底判线位置，x 保持不变
- 原触摸保留用于 LEFT/RIGHT 判定，投影触摸用于 DOWN 判定
- 这是一个**纯判定层的抽象**，不影响渲染

### 4.6 Hold 长条的状态机

```
[头判等待] ──按压→ [头判完成] ──持续按压──→ [尾判等待]
    │                  │                      │
    │ 超时 Auto Miss   │ 松手 > 500ms          │ 到达 tail_time
    ▼                  ▼                      ▼
[整体 Miss]      [Miss 断连]            [尾判 = 头判结果]
```

关键设计：
- 头判 Miss → 整根长条淡出，`is_held = false`，尾判必然 Miss。这样玩家不会以为"我还在按住为什么断了"
- 500ms 松手容错（`hold_break_tolerance_ms`）：允许玩家长条中途短暂抬手（如换手），只要在 500ms 内重新按下就不会断连
- 尾判结果继承头判结果（头判 Perfect → 尾判 Perfect），因为长条的质量主要由头和过程决定

### 4.7 结算统计与 R 值

**准确率公式**：`(Perfect × 1.0 + Good × 0.5) / (Perfect + Good + Miss) × 100%`

**评价标签**：
- 无 Miss → FULL COMBO
- 全 Perfect → ALL PERFECT
- 恰好 1 Good + 无 Miss → ONE GOOD
- 恰好 1 Miss → ONE MISS

**R 值**：单谱面 R = f(chart_constant, accuracy)，算法与社区私服（Explode-Node）一致。总 R 值 = 所有谱面中 best20 的 R 值之和。

---

## 5. 渲染系统设计

### 5.1 核心思路：统一的局部坐标模型

Dynamite 的三侧轨道不是简单的三个方向。架构中的关键抽象是 **LaneCoordinateTransformer**：

```
输入：NoteRenderCommand { lane_type, lane_pos, distance, front_width, front_thickness }
  ↓
LaneCoordinateTransformer（根据 lane_type 选择变换规则）
  ↓
输出：ScreenRect { x, y, w, h }（轴对齐包围盒）
```

所有 Renderer **不需要知道** note 来自哪个轨道——它们拿到的是一个已经正确的屏幕 AABB，直接绘制即可。

### 5.2 三侧轨道的坐标语义

| 轨道 | 判定线方向 | 音符飞行方向 | width 含义 | thickness 含义 |
|---|---|---|---|---|
| **Down (正面)** | 水平 | 从上向下 ↓ | 水平宽度 | 垂直高度 |
| **Left (侧面)** | 竖直 | 从右向左 ← | 竖直高度 | 水平宽度 |
| **Right (侧面)** | 竖直 | 从左向右 → | 竖直高度 | 水平宽度 |

**侧轨道非等比例缩放**：侧面音符的 width（沿轨道方向）乘 0.85 缩放，thickness（垂直于轨道方向）保持不变。这样设计是因为 Dynamite 原版的侧面音符不是简单的"旋转 90°"，而是有透视感的视觉处理。

### 5.3 渲染分层与顺序

每帧的绘制严格按照以下层级（从下到上），保证正确的遮挡：

| 层 | 内容 | 说明 |
|---|---|---|
| 1 | 背景（封面 + 半透明暗化遮罩） | 确保前景 note 可读 |
| 2 | 轨道遮罩与判定线基底 | 半透明背景 |
| 3 | **Note 本体**（按 distance 降序排列） | 远的先画，近的盖在远的上面 |
| 4 | 判定线高亮（白色 3px + 光晕） | 始终在最上方可见 |
| 5 | 判定特效（火花、文字弹出） | 短暂出现（200-400ms） |
| 6 | HUD（分数、Combo、统计） | 半透明底衬 |
| 7 | Footer（曲名、难度标签） | 底部固定 |
| 8 | 暂停遮罩（如有） | 最高层 |

### 5.4 合批策略

`RenderBatch` 是所有 OpenGL 调用的**唯一出口**。设计要点：

- **按纹理合批**：每帧收集所有 `submit()` 调用，内部按纹理指针排序，同一纹理的精灵一次性绘制
- **单 VBO + 动态更新**：使用 `glBufferSubData` 每帧更新顶点数据（而非每帧重新分配），上限 65536 顶点
- **专用斜纹着色器**：45° hatch pattern 通过 GLSL 程序化生成（而非加载纹理），支持方向（顶部栏向左、底部栏向右）和滚动偏移

### 5.5 材质与纯色回退

每个 Renderer 优先使用纹理（从 skin 目录加载的 PNG），如果纹理不存在或未加载，则回退到纯色图形（通过 `submitRoundedRect` 等方法）。这保证了：
- 开发阶段不需要完整的美术资源就能看到完整的渲染结果
- 运行时如果 skin 损坏不会导致崩溃

---

## 6. 场景与 UI 系统设计

### 6.1 场景状态机

```
SceneBase（抽象基类）
  ├── init()      — 一次性初始化（创建子渲染器、分配资源）
  ├── enter()     — 场景激活（加载数据、播放入场动画）
  ├── exit()      — 场景离开（释放场景级资源、停止音频）
  ├── update(audio_now_ms)    — 每帧逻辑更新
  ├── render(batch, audio_now_ms) — 每帧渲染
  ├── handleInput(touches, audio_now_ms) — 触摸事件
  ├── currentTimeMs() — 时间源（Gameplay 覆盖为 AudioClock）
  ├── onPause() / onResume() — 前后台切换
  └── transitionRequest() — 转场请求（PUSH/POP/REPLACE）
```

**场景列表**：LOGIN → MAIN_MENU → SONG_SELECT → CHART_DETAIL → GAMEPLAY → RESULT。外加 SETTINGS / EVENT / SKILL_SET / SHOP 作为侧入口。

### 6.2 Dynamite UI 视觉系统

Dynamite 的 UI 语言是 **"深色装甲 + 霓虹点缀 + 45° 斜纹"**。

| 视觉元素 | 设计决策 |
|---|---|
| **45° 斜纹背景** | 所有顶部栏和底部栏的强制性装饰，用 GLSL 着色器程序化生成（不需要纹理文件），深浅灰交替 `#1E1E1E`/`#0A0A0A` |
| **等宽数字** | 所有分数、Combo、排名使用等宽 LCD 风格字体，确保数字滚动时不发生水平抖动 |
| **全局顶部状态栏** | 除游玩场景外所有场景共享同一顶部栏（头像/用户名/资源图标/返回），高度 56dp |
| **角色立绘发光** | 主菜单和结算界面的角色立绘必须有白色外发光（glow 12px），带呼吸动画（±4px, 3s 正弦周期） |
| **难度颜色系统** | CASUAL=绿 / NORMAL=蓝 / HARD=红 / MEGA=品红 / GIGA=紫 / TERA=橙，全应用统一 |
| **弹窗规范** | 深色底+白色半透明 1px 边框+遮罩淡入+缩放动画，按钮为胶囊形白色边框透明底 |

### 6.3 按钮与交互反馈

- **按下**：缩放至 0.95 (80ms) + 亮度 +20%
- **释放（同按钮）**：回弹至 1.0 (120ms ease-out) + 触发操作
- **释放（移出）**：仅恢复缩放，无操作
- **最小触摸区**：所有交互元素 ≥ 44×44dp（约 7mm 物理尺寸）
- **转场**：菜单间切换 = 左移淡出 + 右滑入 (300ms) / 弹窗 = 遮罩淡入 + 缩放 (200ms) / 游玩开始 = 黑闪 (100ms)

### 6.4 各场景的设计要点

| 场景 | 核心设计 |
|---|---|
| **登录** | 3D 等距 Logo 立方体（cyan 发光描边）+ 粒子背景 + 暗色输入框 |
| **主菜单** | 4-5 个大圆角渐变按钮（单人蓝/段位绿/Multi 品红/商店橙/Event 紫金）+ 右下角色立绘 |
| **选歌/商店** | 3 列网格 + 难度圆形标签（左下角叠加）+ 胶囊操作按钮 + 底部筛选栏 |
| **谱面详情** | 全宽封面 + 落速/PP 滑块 + 三个 Mod 胶囊按钮（BLEED/MIRROR/AUTO） |
| **游玩** | 全屏沉浸、无顶部栏、中央三侧轨道+判定区、背景暗化 50%+ |
| **结算** | 大号等宽分数 + S/A/B 评级（发光）+ 双列统计 + 频谱可视化条 + 排名字段 |
| **Event** | 全宽横幅 + 实时倒计时（金色等宽数字）+ 活动歌曲列表（复用选歌卡片）+ 奖励进度条 |

---

## 7. 数据层设计

### 7.1 Go DataLayer 的定位

Go 层通过 `gomobile bind` 编译为平台原生库（Android .aar / iOS .framework），由 C++ 通过 C ABI 调用。它的角色是：

- **本地数据库**：SQLite 存储歌曲元数据、成绩记录、用户配置
- **网络客户端**（预留）：REST 或 GraphQL 调用社区私服
- **配置管理**：JSON 序列化/反序列化用户设置和皮肤配置

**它不承担**：音频处理、渲染、判定、UI——这些全部在 C++ 侧。

### 7.2 数据模型

核心实体关系：

```
Song (歌曲) 1 ── N Chart (谱面)
                    │
                    N ── 1 Score (成绩) ── 1 User
                                         │
                                    UserConfig
```

**设计要点**：
- `Chart.constant`（谱面定数）是 R 值计算的核心参数，必须与社区私服一致
- `Score.mods` 以 JSON 字符串存储（`{"mirror":false,"bleed":true}`），便于扩展新 Mod 而无需迁移数据库
- `UserConfig` 同样以 JSON 灵活存储，支持动态添加配置项

### 7.3 C++ 与 Go 的桥接设计

C++ 侧通过 `GoBridge` 命名空间调用 Go 函数，所有返回值以 JSON 字符串传递（使用 nlohmann/json 解析）：

```
C++ GoBridge::getSongList()
  → Go DataManager.GetSongList() 返回 JSON string
  → C++ 侧 nlohmann::json::parse()
```

**设计约束**：
- Go 函数必须在 < 2ms 内返回（禁止在调用路径上做网络请求或复杂查询）
- 涉及网络的调用（如提交成绩到远程服务器）应由 Go 侧异步执行，C++ 侧通过回调或轮询获取结果
- 桌面开发时使用 `go_bridge_desktop.cpp` 的 Mock 实现，不依赖 Go 运行时

---

## 8. 社区兼容策略

### 8.1 兼容范围

Dynamite 虽已停服，但社区形成了三个关键生态节点，重构项目必须全部兼容：

| 节点 | 作用 | 兼容方式 |
|---|---|---|
| **Explode 私服** (NodeJS/Kotlin) | 社区自建服务器，提供歌曲/成绩/排行 | GraphQL Schema 100% 对齐 + MongoDB Schema 对齐 |
| **DynaMaker** (Web 制谱器) | 社区创作工具，导出谱面 | 原生解析 XML/JSON/ZIP 三种导出格式 |
| **Dynamite 客户端** (原 Unity 版) | 仍有大量用户在使用 | 转服码兼容，使原客户端可连接我们的服务器 |

### 8.2 零侵入原则

Event 模式、商店扩展等新功能**绝不修改**社区标准的 `ChartSet` / `Chart` / `Score` Collection 结构。所有扩展使用独立的 Collection 或在现有 Collection 中仅追加 `omitempty` 可选字段。

### 8.3 资源路径兼容

Dynamite 客户端硬编码了以下资源请求路径，我们的资源服务器必须完全还原：

```
/download/music/encoded/{setId}    — 加密歌曲
/download/cover/encoded/{setId}    — 加密封面
/download/chart/encoded/{chartId}  — 加密谱面
/download/avatar/256x256_jpg/{userId}
```

**加密策略**：同时支持 `/encoded/`（加密返回）和明文路径。客户端优先请求明文（性能更好），若请求 `/encoded/` 则实时加密后返回。对于本地已有的明文资源，支持运行时加密流式返回，避免存储两份文件。

### 8.4 转服码

Dynamite 通过转服码（Base64 编码的 JSON）切换服务器：

```
{ 服务器信息 JSON } → UTF-8 → Base64(标准无换行) → 二维码/剪贴板分享
```

重构项目作为新客户端，支持解析转服码并自动切换到对应服务器。

### 8.5 DynaMaker 谱面兼容

DynaMaker 导出三种格式：XML（CMap 序列化）、JSON（Web 版）、ZIP（社区分发包）。ChartParser 通过**扩展名自动检测**选择解析器，首次解析后写入 `.chart` 二进制缓存，后续加载走快速路径。

---

## 9. Event 活动系统设计

### 9.1 核心设计思路

Event 是**主玩法之上的运营层扩展**，不改动核心判定、音频、渲染系统。设计原则：

1. **零侵入**：Event 数据使用独立的 MongoDB Collection，不修改社区标准表
2. **复用游玩场景**：Event 游玩完全复用 `SceneGameplay`，仅通过启动参数 `is_event_mode=true` 区分
3. **运营自治**：提供 Admin API，运营人员通过 HTTP 请求即可创建/修改活动，无需改代码
4. **优雅降级**：连接不支持 Event 的社区私服时，所有 Event UI 自动隐藏或置灰

### 9.2 活动数据独立存储

Event 使用独立的 MongoDB Collection（而非嵌入 User 或 ChartSet），包含：
- 时间窗口（UTC 起始/结束时间）
- 关联歌曲列表 + 活动期间免费歌曲列表
- 奖励轨道（多个阈值 → 奖励类型的映射）
- 排行榜类型（按分数/准确率/Combo/活动积分排名）
- 积分规则（基础积分 + 分数倍率 + 准确率加成 + Combo 加成 + 首次游玩奖励）

### 9.3 客户端活动流程

```
主菜单 Event 按钮 → SceneEvent（横幅+倒计时+歌曲列表+奖励进度）
  → 点击活动歌曲 → SceneGameplay(is_event_mode=true)
  → 游玩结束 → 正常结算 + 额外提交活动成绩
  → 服务器返回：获得积分 + 解锁奖励列表
  → 若解锁新奖励 → 结算界面弹出奖励解锁浮层
```

### 9.4 积分计算的服务器权威

活动积分全部由服务器计算，客户端仅显示结果。计算公式：`活动积分 = 基础积分 + (分数 × 分数倍率) + (准确率 × 准确率加成) + (Combo × Combo加成) + 首玩奖励`。防止客户端作弊修改积分。

### 9.5 Event UI 设计要点

- **主菜单按钮**：紫→金渐变（营造限时珍贵感），脉冲发光 3s 周期
- **无活动时**：按钮置灰 + 文字 "暂无活动"，图标透明度 30%
- **新活动首次上线**：按钮右上角红色圆点（未读标记），用户进入后消失
- **场景布局**：顶部横幅 (35%) + 中部活动歌曲列表 (45%) + 底部奖励进度 (20%)
- **倒计时**：金色等宽数字，格式 "剩余: XX天XX时XX分"，基于服务器时间而非本地时钟
- **奖励节点**：未达成=灰色锁 / 已达成=金色勾（可领取）/ 已领取=彩色图标

---

## 10. 安全架构

### 10.1 分层防御

```
┌─────────────────────────────────┐
│  服务端（最终权威）                │
│  · 成绩合理性校验（物量/分数上限）  │
│  · 积分服务器计算                  │
│  · JWT 认证 + 请求限流            │
├─────────────────────────────────┤
│  客户端（纵深防御）                │
│  · 谱面文件哈希校验                │
│  · 内存安全（智能指针/RAII）       │
│  · 路径遍历防护                    │
│  · 异常熔断 → 安全场景            │
└─────────────────────────────────┘
```

### 10.2 服务端防作弊

成绩提交到服务器时必须通过以下校验链：

1. **物量一致性**：`Perfect + Good + Miss == chart.NoteCount`
2. **分数上限**：`Score <= theoreticalMax(chart.Constant, noteCount)`
3. **准确率范围**：`0.0 <= Accuracy <= 100.0`
4. **Combo 上限**：`MaxCombo <= NoteCount`
5. **时间合理性**：提交时间戳不能太旧（> 1小时）或指向未来（> 1分钟）
6. **游玩时长**：`playDuration >= chart.Duration × 0.8`（防止加速外挂）
7. **重复提交**：5 分钟内同用户同谱面的重复成绩去重
8. **谱面哈希校验**：Chart 记录预存 SHA-256 哈希，客户端加载前校验

### 10.3 客户端内存安全

- **禁止裸 new/delete**：所有堆内存由 `std::unique_ptr` / `std::shared_ptr` 管理
- **C 库句柄 RAII 封装**：SDL_Window、GL Context、ma_device 等必须包装为 RAII 类
- **边界检查**：所有数组访问使用 `at()` 或前置边界检查
- **栈保护**：栈上禁止分配 > 256 字节的数组
- **游戏局间内存归零**：每局 GameSession 析构时必须完整释放本局分配的所有内存（note 列表、特效粒子、音频 buffer）

### 10.4 资源加载安全

- **路径遍历防护**：五步校验链（拒绝绝对路径 → 拒绝 `..` → 扩展名白名单 → 规范化 → 确认最终路径在 base 目录下）
- **魔数校验**：.chart → `DYNT` / PNG → `0x89 'PNG'` / OGG → `OggS`
- **资源炸弹防护**：文件大小上限（纹理 16MB / 音频 50MB）、像素上限（4096×4096）、JSON 深度限制

### 10.5 异常熔断

当客户端检测到安全异常（校验失败、内存越界、文件篡改）时：
1. 立即停止音频播放
2. 丢弃当前成绩（不上传）
3. 显示通用错误提示（不暴露安全细节）
4. 记录安全日志（含设备指纹）
5. 强制返回标题画面

---

## 11. 资源与构建系统

### 11.1 资源标准化流程

Unity 解包资源 → `tools/normalize.py` 标准化 → 以下目录结构：

```
assets/songs/{song_id}/
  ├── metadata.json        — 歌曲信息 + 各难度定数
  ├── cover.png            — 512×512 RGBA
  ├── bgm.ogg              — 44100Hz 立体声
  ├── preview.ogg          — 15s 预览
  └── chart_{diff}.chart   — 各难度 .chart 二进制
assets/skins/{skin_name}/
  ├── note_*.png           — 音符纹理（tap/hold_head/body/tail/slide）
  ├── effect_*.png         — 判定特效帧
  ├── lane_*.png           — 轨道背景
  ├── judge_line.png       — 判定线
  ├── font.ttf             — 游戏字体
  └── skin.json            — 皮肤配置（颜色覆盖）
assets/config/
  └── default_settings.json
```

### 11.2 资源加载优先级

```
1. 本地 LRU 缓存（最快）
2. .explode_data 社区资源目录
3. 远程 CDN / 社区服务器（302 重定向）
4. 默认占位资源（保底）
```

### 11.3 构建流水线

```
1. Python tools/normalize.py     — 资源标准化
2. Go gomobile bind               — 生成数据层 .aar / .framework
3. CMake (Android NDK / iOS)     — 编译 C++ GameCore .so / .a
4. Gradle (Android) / Xcode (iOS) — 组装最终 APK / IPA
```

所有第三方库（SDL3、miniaudio、glm、stb、nlohmann/json）通过 git submodule 管理，版本锁定。

---

## 12. 当前实现状态与重构路线

### 12.1 已完成模块（dynamite-remake 当前状态）

| 模块 | 状态 | 备注 |
|---|---|---|
| AudioClock + AudioEngine | ✅ 完成 | miniaudio 回调驱动，采样时钟正确 |
| JudgeEngine | ✅ 完成 | 三侧 + SLIDE 双机制 + Hold + 糊谱 + 垂直投影 |
| InputManager | ✅ 完成 | SDL3 多指 + 垂直投影 |
| ChartParser | ✅ 完成 | DynaMaker XML/JSON/ZIP + .chart 二进制缓存 |
| Gameplay 渲染链 (v1.1) | ✅ 完成 | LaneCoordinateTransformer + 7 个 Renderer |
| RenderBatch | ✅ 完成 | 合批 + 斜纹着色器 |
| MainMenu / SongSelect / Result | ⚠️ 基础 | 可用但缺乏 UI 打磨 |
| Go DataLayer | ⚠️ 基础 | SQLite CRUD 完成，缺网络层 |

### 12.2 需要重构的架构问题

以下是当前代码中需要在重构中解决的结构性问题：

**1. 场景切换硬编码** — `main.cpp` 中使用 `switch(SceneID)` 和手动 `make_unique`。应改为场景工厂 + 注册机制，便于添加新场景。

**2. GameplayRenderFrame 构建与判定耦合** — `SceneGameplay::BuildRenderFrame()` 直接从 Chart 数据构建渲染命令，中间缺少抽象层。应增加一个 `ChartToRenderCommand` 转换器。

**3. UI 组件缺乏复用** — 各场景独立绘制按钮、卡片、标签。应提取为可复用的 UI 组件（Button / Card / Badge / Slider 等）。

**4. Go 桥接同步阻塞** — C++ 同步调用 Go 的 `GetSongList()` 等函数，若 Go 侧执行 SQL 查询可能阻塞渲染线程。应改为异步调用 + 回调模式。

**5. 纹理生命周期混乱** — Note 纹理在 `RenderBatch` 中持有，但场景中也各自管理 `cover_tex_`。应将所有纹理统一到 `ResourceCache`（LRU + 引用计数）。

**6. 配置系统单薄** — `ConfigManager` 仅管理屏幕参数。应扩展为三层：`GameConfig`（屏幕/性能）+ `UserConfig`（偏移/速度/Mods）+ `SkinConfig`（颜色/纹理路径）。

**7. 缺乏统一资源管道** — 没有优先级队列和加载状态管理。应设计 `AssetPipeline` 支持预加载、优先级、取消、进度回调。

**8. SLIDE 滑动检测精度不足** — 当前通过 `new_fingers` 集合区分 Catch 和 Swipe，但快速滑动时帧间手指可能被判定为"新手指"。需要更精细的触摸轨迹追踪。

### 12.3 缺失模块（按优先级）

**P0 — 必须完成才能发布：**
- Android / iOS 平台壳重建（已被作者删除）
- 第三方库补充（SDL3、miniaudio、glm 等）
- 播放器时序同步修复

**P1 — 核心体验：**
- 登录/注册场景 + 认证系统
- 谱面详情场景（难度选择 + Mod 开关）
- 设置场景（落速/偏移/Mods）
- 服务端成绩防作弊完整校验

**P2 — 社区功能：**
- Event 活动系统全链路
- 商店购买完整流程
- GraphQL + REST 后端完整实现
- Admin 运营后台

**P3 — 增强体验：**
- R 值全局排行
- 多语言 + 多皮肤
- CI 安全工具链集成

---

## 13. 代码审计发现：隐性风险与设计债务

以下问题来自对 dynamite-remake 现有代码的逐文件审查，它们不是架构文档层面的缺失，而是已写入代码中的具体风险——很可能正是上一次重构"效果不好"的原因。

### 13.1 全局可变状态

**问题**：跨场景的数据传递依赖全局 `extern` 变量。

```
main.cpp 中无声明，但 scene_gameplay_new.cpp 中：
  extern JudgeEngine::Stats s_pending_stats;
  extern std::string s_pending_song_title;
  extern std::string s_pending_difficulty;
```

`SceneGameplay` 在检测到谱面结束时，将结果写入这三个全局变量。`SceneResult` 再从全局变量中读取。这制造了：
- **隐式耦合**：Result 必须"知道"Gameplay 写入了哪些全局变量
- **不可重入**：如果场景栈中同时存在两个 Gameplay（理论上可以通过 REPLACE 产生），后者会覆盖前者的结果
- **测试困难**：无法独立测试 Result 的渲染逻辑——必须先运行 Gameplay

**应改为**：场景间通过 `TransitionRequest` 携带数据载荷（给 `TransitionRequest` 增加一个 `void*` 或 `std::any` 的 payload 字段）。

**类似问题**：`scene_song_select.cpp` 中的 `g_stripe_time_ms_` 和 `handleInput()` 中的 `static std::unordered_map` ——都是文件级静态变量，在多实例或快速场景切换时会出问题。

### 13.2 硬编码数据阻断真实数据通路

`SceneSongSelect::loadSongs()` 创建了 4 首硬编码的 mock 歌曲：

```cpp
SongCard s;
s.id = "dyn_001";
s.title = "Neon Pulse";
s.artist = "CyberBeats";
// ... 完全硬编码，没有调用 GoBridge::getSongList()
```

这意味着**整个选歌→游玩→结算的数据通路从未被端到端测试过**。真实的流程应该是 `Go DataLayer → JSON → C++ 解析 → SongCard 列表`，但当前代码绕过了 Go Bridge，使得：
- Go DataLayer 的 `GetSongList()` 从未在真实场景中被调用
- JSON 反序列化的性能路径从未被验证
- 歌曲元数据（BPM、时长、定数）与谱面文件的一致性从未被校验

**重构时必须**：优先打通 Go Bridge → SongSelect 的数据通路，删除所有 mock 数据。

### 13.3 文件 I/O 在主线程同步执行

`SceneGameplay::loadChart()` 中以下操作全部在主线程同步执行：
- `ChartParser::parseWithCache(path)` — 首次加载需解析 XML（50-500ms）
- `audio_->loadSong(song_path)` — 打开并解码 OGG 文件头
- `std::make_unique<Texture>(dir + "cover.png")` — stb_image 解码 PNG

在移动设备上，这三个操作合计可能阻塞主线程 500ms-2s。在此期间：
- 渲染完全冻结（无帧输出，用户看到卡死）
- 触摸事件被积压（松开后才批量处理）
- 在 Android 上超过 5s 会触发 ANR

**应改为**：
- `loadChart()` 拆分为 `prepareAsync()`（后台线程加载）+ `commitOnMain()`（主线程完成 GPU 上传）
- 加载期间显示过渡动画（loading spinner 或封面渐入）
- Texture 构造与 OpenGL 上传分离：`stbi_load` 在后台线程，`glTexImage2D` 在主线程

### 13.4 音频时钟生命周期与重置时机

当前代码中，`AudioClock::init()` 会将 `sample_count_` 归零，但这只在 `AudioEngine::init()` 时调用一次（也就是 `SceneGameplay::init()` 时）。而 `SceneGameplay` 通过 `make_unique` 重建，所以每次进入新游戏会话会得到新时钟——当前逻辑是正确的。

**但这里埋着一个脆弱的假设**：场景必须被 `make_unique` 重建才能正确重置时钟。如果未来有人优化为复用场景（只调 `enter()` 不重建），时钟不会自动归零，判定会全部错误——`audio_now_ms` 会从上一局的结束时间继续累加，而 `note.time_ms` 是新谱面的相对时间。

**应在**：`AudioEngine` 增加一个显式的 `resetClock()` 方法，由 `SceneGameplay::enter()` 或 `loadChart()` 显式调用，不依赖场景重建隐式清零。

### 13.5 BuildRenderFrame 的单一职责崩溃

`SceneGameplay::BuildRenderFrame()` 是一个 ~200 行的函数，混合了以下职责：
1. NoteData → NoteRenderCommand 的**数据转换**
2. 下落曲线的**动画计算**（approach time / fall range / t 值）
3. 判定后淡出**动画计算**（post_judge_slowdown / alpha）
4. Hold 按住状态的**跨系统查询**（查 JudgeEngine 的 HoldState）
5. 按 distance 的**排序**（渲染顺序）
6. HUD 数据的**填充**（从 JudgeEngine::Stats 拷贝到 GameplayRenderFrame）

当需要调整下落曲线的缓动函数、或者修改判定后淡出时长、或者改动排序策略时，都需要在这同一个函数中操作。这使得任何视觉调整都可能意外影响判定逻辑（或反之）。

**应拆分为**：
- `NoteToRenderCommandConverter`：纯数据转换，不涉及时间计算
- `ApproachCurve`：独立的缓动函数对象，可替换（线性 / ease-out / Dynamite 原生曲线）
- `PostJudgmentAnimator`：管理判定后的 alpha/scale 动画状态

### 13.6 谱面结束检测的竞态窗口

```cpp
if (chart_ && audio_now_ms > static_cast<int64_t>(chart_->duration_ms) + 2000) {
    s_pending_stats = stats;
    transition_request_.type = Transition::REPLACE;
    transition_request_.target_scene_id = static_cast<int>(SceneID::RESULT);
}
```

这个检测每帧执行一次。问题在于 `chart_->duration_ms` 来自谱面文件的 `duration_ms` 字段，但**这个值是谱面编译器估算的**，不一定与实际音频长度一致。如果：
- 音频比 `duration_ms` 长（如末尾有静音）：音乐没播完就被切到结算
- 音频比 `duration_ms` 短（如提前结束）：音乐播完后还要等 2 秒空白才能结算

**应改为**：以 `AudioEngine` 的播放结束信号（解码器 read 返回 0 → 发送 `OnPlaybackEnd` 事件）为准，而非谱面元数据的 `duration_ms`。

### 13.7 缺少系统——测试、性能监控、错误恢复

以下三个横切关注点在当前架构中完全缺席：

**测试**：
- 无任何测试框架集成（C++ 侧可用 Catch2 或 doctest，Go 侧有原生 testing）
- 判定引擎是**纯函数**（输入 touches + 谱面 → 输出 JudgeResult），非常适合单元测试——但一行测试也没有
- 坐标转换器也是纯函数，同样应该通过参数化测试覆盖三个轨道的各种边界情况

**性能监控**：
- 无帧时间统计（`glFinish` 或 timestamp queries）
- 无 draw call 计数
- 无内存预算追踪（移动端 GPU 内存通常只有 512MB-1GB 共享内存）
- 无音频 underrun 检测（miniaudio 回调中如果 `ma_decoder_read_pcm_frames` 返回不足，说明解码跟不上播放——这是一个严重的性能告警，当前代码中只填充了静音但没有记录任何警告）

**错误恢复**：
- `AudioEngine::init()` 失败后，`audio_` 保持 nullptr，游戏继续运行但不播放音频——用户看到 note 在飞但没有声音，不知道发生了什么
- `loadSong()` 失败同理：静默失败，用户体验极差
- 缺少一个统一的 `ErrorState` 机制：当关键子系统失败时，应显示明确的错误界面并提供恢复选项（重试 / 跳过音频 / 返回菜单）

### 13.8 移动端特有的未覆盖风险

| 风险 | 说明 |
|---|---|
| **音频焦点** | 电话呼入、闹钟、通知音会抢占音频焦点。Android 的 AudioManager 和 iOS 的 AVAudioSession 需要正确处理焦点丢失/恢复。当前代码对此无处理 |
| **发热降频** | 连续 60fps 渲染 + 音频解码会使中低端设备在 15-30 分钟内触发 thermal throttling。需要帧率自适应（检测到降频时降至 30fps） |
| **触摸延迟差异** | Android 设备的触摸管线延迟从 20ms 到 80ms 不等。音游需要校准偏移，但当前 `UserConfig.OffsetMs` 虽然定义了，在代码中从未被应用 |
| **OpenGL 上下文丢失** | Android 的 `SDL_EVENT_DID_ENTER_BACKGROUND` 可能导致 GL 上下文被销毁。当前代码只暂停/恢复音频，不处理纹理/VBO 的重建 |
| **屏幕比例** | 硬编码 1920×1080（16:9），现代手机通常是 20:9 甚至 21:9。黑边区域的触摸坐标映射在 `handleInput` 中有处理，但 UI 布局没有适配——长屏幕设备的底部栏和顶部栏间距会异常 |

---

## 14. 重构执行建议

上一次重构"效果不好"——理解为什么失败，比再写一遍代码更重要。

### 14.1 推测：上次重构可能的问题

基于代码状态，推测以下因素导致了上次重构效果不佳：

1. **一次性改动过大**：试图同时重构场景管理、渲染管线、数据层桥接。结果可能是所有模块都"改了一半"，没有一个能端到端工作。
2. **没有测试的安全网**：改动判定引擎或坐标转换器后，没有自动化测试来验证行为没有退化。只能手动跑一遍游戏感觉"差不多"。
3. **平台壳的删除**：作者删除了所有 Android/iOS 代码和第三方库后，代码实际上无法在任何设备上运行。桌面模拟和真机行为差异巨大（尤其是触摸延迟和音频延迟），可能在桌面上"改好了"但手机上完全不工作。
4. **UI 和逻辑的纠缠**：各场景的渲染代码和输入处理写在一起，修改卡片布局可能破坏滚动逻辑。

### 14.2 推荐的重构策略

**原则：增量、可验证、先骨架后血肉**

**第一阶段：建立安全网（1-2 天）**
- 为 JudgeEngine 编写单元测试（给定已知谱面 + 已知触摸序列 → 验证判定结果序列）
- 为 LaneCoordinateTransformer 编写参数化测试（输入各种 lane_type/lane_pos/distance → 验证输出 AABB）
- 为 ChartParser 的二进制读写编写往返测试（Chart → writeChart → readChart → 验证等价）
- 确保这些测试在 CI 中运行（GitHub Actions / 本地 pre-commit hook）

**第二阶段：打通端到端数据通路（2-3 天）**
- 删除 `SceneSongSelect` 中的 mock 数据
- 实现 Go Bridge 的 `getSongList()` 真实调用（先从 SQLite 本地数据库读取）
- 让选歌 → 加载谱面 → 游玩 → 结算这一条链路在桌面上能完整跑通
- **此时不要改 UI，不要优化渲染**

**第三阶段：逐个解决架构问题（每个 1-2 天，独立 PR）**
- 每次只解决 §12.2 中的一个问题
- 优先解决 §13 中的隐性风险（全局变量 → payload、BuildRenderFrame 拆分、音频时钟显式重置）
- 每个 PR 必须有对应的测试更新

**第四阶段：移动端适配（3-5 天）**
- 重建 Android 平台壳
- 在真机上测量端到端延迟（音频 + 触摸）
- 校准默认偏移值
- 处理前后台切换、音频焦点、GL 上下文重建

**第五阶段：UI 打磨与新功能（持续）**
- 提取可复用 UI 组件
- 实现缺失场景（登录、设置、Event）
- 性能调优与多设备适配

### 14.3 关键原则

- **每次改完后必须在真机上运行**：桌面 OpenGL ≠ 移动端 OpenGL ES，桌面音频延迟（通常 ~10ms WASAPI）≠ 移动端音频延迟（AAudio 低延迟模式 ~5ms，普通模式 ~40ms+）
- **渲染与判定用不同频率更新**：判定应跟随音频回调频率（~344Hz @ 128 frames buffer），渲染按 VSync（60/90/120Hz）。当前代码中判定在 `update()` 中每帧执行一次（即 60Hz），这意味着最坏情况下有 ~16ms 的判定延迟——可以考虑在音频回调中直接驱动判定，将结果写入 lock-free ring buffer，渲染线程消费
- **不要过早优化合批**：RenderBatch 的纹理排序合批是正确的设计，但在 UI 阶段（主菜单、选歌）draw call 通常在 10-20 个以内，合批收益很小。集中精力保证正确性，性能问题用 profiling 驱动
- **配置偏移必须端到端生效**：`UserConfig.OffsetMs` 定义在 Go 数据模型中，但如果在 C++ 判定路径中没有减去这个偏移，则整个校准功能是空的。这类"定义了但没用"的字段应该全部排查一遍

---

## 15. 硬约束清单

### 15.1 时序与性能

| # | 约束 |
|---|---|
| 1 | 判定系统**只**使用 `AudioClock::nowMs()`，禁止 `SDL_GetTicks()` / `std::chrono` |
| 2 | Android AAudio 必须请求 `PERFORMANCE_MODE_LOW_LATENCY`，默认缓冲 ≤ 128 frames |
| 3 | SDL3 触摸事件在事件循环中**立即**标记 `audio_now_ms`，禁止异步缓冲 |
| 4 | 开启 VSync (`SDL_GL_SetSwapInterval(1)`)，支持 60/90/120Hz 自适应 |
| 5 | Go DataLayer 调用必须异步或 < 2ms 返回，**禁止在音频线程调用** |

### 15.2 判定规则

| # | 约束 |
|---|---|
| 6 | Hold 长条必须严格实现 500ms 松手容错 |
| 7 | 头判 Miss 时整根长条立即淡出 + 尾判自动 Miss |
| 8 | 糊谱惩罚：一触控多个不同时 note → 全部强制 Miss |

### 15.3 社区兼容

| # | 约束 |
|---|---|
| 9 | ObjectId 必须是 24 位 hex 字符串，禁止自增整数 |
| 10 | `/download/{type}/encoded/{id}` 路径模板必须 100% 还原 |
| 11 | 禁止修改社区标准 Collection（ChartSet / Chart / Score）结构 |
| 12 | 必须原生支持 DynaMaker XML/JSON/ZIP，不得要求外部转换 |
| 13 | R 值算法必须与社区私服一致 |

### 15.4 UI 与视觉

| # | 约束 |
|---|---|
| 14 | 所有顶部栏和底部栏必须覆盖 45° 斜纹纹理 |
| 15 | 所有分数/Combo/排名必须使用等宽字体 |
| 16 | 难度标签颜色必须严格映射（HARD=红 / MEGA=品红 / NORMAL=蓝 / CASUAL=绿 / GIGA=紫 / TERA=橙） |
| 17 | 所有交互元素触摸区域 ≥ 44×44dp |
| 18 | 游玩场景背景必须暗化 ≥ 50% 以保证 note 可读 |

### 15.5 安全

| # | 约束 |
|---|---|
| 19 | C++ 禁止裸 new/delete 持有堆内存（C 库句柄除外） |
| 20 | Go 所有数据库查询必须参数化（`?` 占位符或 `bson.M`） |
| 21 | 所有二进制文件加载前必须通过魔数校验 |
| 22 | 服务端成绩提交必须校验：物量、分数上限、时间戳、游玩时长、重复提交 |
| 23 | JWT 解析必须显式校验 `alg` 字段，拒绝 `none` |
| 24 | 资源路径加载必须经过扩展名白名单 + `..` 过滤 + 路径归一化三重校验 |
| 25 | 敏感数据（Token / 密码 / 用户隐私）在日志中必须脱敏 |
| 26 | 生产环境 API 必须强制 TLS 1.2+ |
| 27 | 检测到安全异常时，必须丢弃成绩、停止音频、返回安全场景 |

---

*本文档基于以下 7 份原始规格书合并精简而成：*
*dynamite_rebuild_architecture.md / dynamite_event_mode_spec.md / dynamite_gameplay_render_spec_v1.1.md / dynamite_backend_compatibility.md / dynamite_security_spec.md / dynamite_shop_scene_spec.md / dynamite_ui_design_spec_v2.md*

*当前实现状态基于 dynamite-remake 仓库代码审计（2026-05-22）*
