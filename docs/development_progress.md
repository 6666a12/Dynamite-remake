# 开发进度报告

> 更新日期: 2026-05-22 | 项目: Dynamite 跨平台重构

---

## 当前阶段: Alpha 功能完善

### 已完成

- [x] **谱面解析**: XML/JSON/.chart 二进制缓存
- [x] **判定引擎**: P/G/M 三级判定 + Hold 容错 + 双押合并 + 糊谱惩罚
- [x] **音频时钟**: miniaudio 数据回调驱动，唯一时间基准
- [x] **输入管理**: SDL3 三侧触控映射，手指追踪
- [x] **渲染批处理**: OpenGL ES 3.0 合批，纹理/纯色/圆角/文字/斜纹
- [x] **场景系统**: SongSelect → Gameplay → Result 流程闭环
- [x] **Hold 纹理统一化**: 所有 hold 纹理改为 DOWN 轨道竖直方向
- [x] **hold_tail.png**: 添加尾部箭头纹理，完整 head-body-tail 三层结构
- [x] **零长度 hold**: 使用 UI0_Sprites_29.png 纹理（替代 tap 形态的 hold）
- [x] **判定通过逻辑**: 判定通过后隐藏 head 和假身体
- [x] **HUD**: Combo/Score/Accuracy/状态/判定线/斜纹背景
- [x] **45°棋盘格斜纹着色器**: 顶部/底部栏标志元素，动态滚动

### 进行中

- [ ] 真机构建调试（Android/iOS）
- [ ] BPM 变速支持
- [ ] 等宽 LCD 数字字体
- [ ] MULTI/SLIDE 渲染完善

### 待办

- [ ] ZIP 包解析完善（miniz 集成）
- [ ] hold_break_tolerance 可配置化
- [ ] Event 活动模式
- [ ] 联机对战基础

---

## 组件状态

| 模块 | 状态 | 备注 |
|------|------|------|
| main.cpp | ✅ | SDL3 场景栈，~60fps 限帧 |
| AudioEngine | ✅ | miniaudio 0.11，采样时钟 |
| JudgeEngine | ✅ | 三级判定，Hold 0.5s 容错 |
| InputManager | ✅ | SDL3 触摸，三侧区域 |
| ChartParser | ✅ | XML/JSON/.chart 缓存 |
| RenderBatch | ✅ | ES 3.0 合批，纹理/斜纹/字体 |
| HoldNoteRenderer | ✅ | 统一纹理方向，tail/head/body/ghost |
| ResourceCache | ✅ | weak_ptr LRU |
| SceneGameplay | ✅ | 903 行，渲染/判定/输入 |
| SceneSongSelect | ✅ | 歌曲卡片，45°斜纹背景 |
| SceneResult | ✅ | 分数动画，评级系统 |
| Go DataLayer | ✅ | SQLite，gomobile bind |
| Android Platform | ✅ | APK 已编译 |
| iOS Platform | ⚠️ | 骨架完成，未测试 |

---

## 最近变更

### 2026-05-22 — Hold 纹理系统重构

**问题**: hold 纹理需要根据轨道方向旋转 UV，逻辑复杂且容易出错。

**解决**: 将所有 hold 纹理（hold.png, hold_head.png, hold_pressed.png, hold_tail.png）在图像层面上直接旋转为 DOWN 轨道竖直方向，渲染器中不再需要旋转 UV。

**变更文件**:
- `hold_note_renderer.cpp` — 重写渲染逻辑，统一纹理方向，添加 tail 支持，修正判定过线状态
- `hold_note_renderer.hpp` — 无变化（接口不变）
- `render_batch.h` — 添加 `note_hold_tail_tex_` 和 `GetNoteHoldTailTex()`
- `render_batch.cpp` — 加载 `hold_tail.png`，添加 `getNoteHoldTailTex()`
- `i_renderer.hpp` — 添加 `GetNoteHoldTailTex()`、`GetNoteHoldZeroTex()` 虚接口
- `i_renderer.cpp` — 实现两个新接口
- `rotate_hold_textures.py` — 纹理旋转工具脚本

**纹理状态**: hold.png, hold_head.png, hold_pressed.png, hold_tail.png 全部为竖直方向。零长度 hold 使用 UI0_Sprites_29.png。

