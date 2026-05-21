# 修复记录

## 1. judge_engine.cpp - 严重 Bug 修复

### 1.1 loadChart 缺少 closing brace
**文件**: core/src/engine/judge_engine.cpp  
**问题**: loadChart 函数体缺少一个 closing brace }，导致文件 brace count 不匹配（depth=1）。  
**修复**: 在 or (auto& an : active_notes_) 循环结束后添加 }。

### 1.2 update 函数 Auto Miss 块缺少 closing brace
**文件**: core/src/engine/judge_engine.cpp  
**问题**: 
`
if (audio_now_ms > an.time_ms + window_miss) {
    ...
    if (an.type == HOLD_HEAD) { ... }
    }  // 多余的
    continue;
}  // 真正应该关闭 auto_miss 的
// 触摸匹配  <- 这行本应在 for 循环体内，但实际在 for 循环外！
`
if (auto_miss) 块缺少 } 闭合，导致：
- continue; 后本应用于关闭 if 的 } 被跳过
- continue; 后的触摸匹配代码仍被 for 循环包含（缩进上）但实际未被编译进 for 循环
- 所有触摸匹配逻辑**永远不会执行**，音符完全无法通过触摸判定  
**修复**: 
1. 移除第 147 行多余的 } 
2. 在第 148-149 行正确放置 continue; 后紧跟 } 关闭 auto_miss if

## 2. scene_result.cpp - 准确率计算错误

### 2.1 accuracy 多乘了 100
**文件**: core/src/scenes/scene_result.cpp  
**问题**: judge_engine.cpp 的 updateStats 中 accuracy 已经是百分比值（如 95.5 表示 95.5%），
但 drawStatsPanel 中 stats_.accuracy * 100.0 又乘了一次 100，导致显示 9550%。  
**修复**: stats_.accuracy * 100.0 → stats_.accuracy

## 3. scene_gameplay.cpp - HoldActiveState 死代码

### 3.1 hold_active_states_ 永不填充
**文件**: core/src/scenes/scene_gameplay.cpp  
**问题**: HoldActiveState 结构体从未被填充任何数据，但 update 函数遍历它寻找尾判事件。
实际上 JudgeEngine 已经通过 rameResults().is_hold_tail 传递尾判事件。  
**修复**: 移除 hold_active_states_ 的遍历逻辑，只保留 
ote_judge_states_ 的头判同步。

### 3.2 scene_gameplay.h 清理
**文件**: core/src/scenes/scene_gameplay.h  
**修复**: 移除 HoldActiveState 结构体定义和 hold_active_states_ 成员变量。

### 3.3 spawnHitEffect 声明不完整
**文件**: core/src/scenes/scene_gameplay.h  
**问题**: spawnHitEffect 声明缺少第四个参数 int screen_w, int screen_h 和分号。  
**修复**: 补全参数列表和 ;。

## 4. scene_gameplay.cpp - HOLD 身体渲染（当前实现）

### 4.1 假身体 + 真身体设计
**文件**: core/src/scenes/scene_gameplay.cpp (drawNotes 函数)

以 DOWN 轨道为例：
`
[head]  ← 在上方，先落
   |
 假身体（半透明橙色，从 head→判定线，跟随 head 下落）
   |
 判定线
   |
 真身体（正常颜色，从判定线→tail，过线后逐渐收起变短）
   |
 [tail]  ← 在下方，后落
`

**假身体（ghost_len）**:
- 从 head_axis 延伸到 judge_axis（判定线）
- 半透明橙色 PackColor(255, 200, 100, a*0.3)，跟随 head 的 alpha
- 长度不超过 ull_len（duration 对应的距离）

**真身体（real_len）**:
- 从 judge_axis 向后延伸到 	ail_axis
- 长度 = ull_len - ghost_len
- **收起效果**：当 dt < 0（head 过线）后，以 shrink_speed 速度从尾端缩短
- 真身体用正常 color（与 head 相同）

**三方向适配**:
- **DOWN**: 垂直向下，head_axis = pos_y + draw_h*0.5，judge_axis = judge_y
- **RIGHT**: 水平向右，head_axis = pos_x + draw_w*0.5，judge_axis = screen_w - side_w
- **LEFT**: 水平向左，head_axis = pos_x + draw_w*0.5，judge_axis = side_w（方向反转）

**平铺渲染**: 每段 hold_seg_px（180px），最后一段不足时通过 UV 裁切

### 4.2 其他 Note 降速和淡出
- 所有 note 过线后 800ms ease-out 降速（	 = 1.0 + eased * 0.30）
- 已判定：800ms ease-out 淡出（lpha = 1 - fade_t²）
- 未判定过线：最多降至 40% 透明度

## 5. 审计算法 - 注意事项

### 5.1 所有文件 brace 平衡检查
| 文件 | 状态 |
|------|------|
| judge_engine.cpp | ✅ 已修复（depth: 0） |
| scene_gameplay.cpp | ✅ OK（depth: 0） |
| scene_gameplay.h | ✅ OK（depth: 0） |
| 其余 22 个文件 | ✅ OK（depth: 0） |

### 5.2 已知未解决项
1. **HOLD_TAIL 纹理**: 当前使用 getNoteHoldHeadTex() 作为后备，需要单独纹理
2. **左/右轨道假身体UV朝向**: LEFT 轨道旋转 180° 后 UV 方向需验证
3. **平铺 UV 裁切**: 最后一段不足 hold_seg_px 时 UV 裁切是否正确（当前实现：uv_start = 1 - seg/hold_seg_px，保持 UV 从物理底部对齐）
4. **hold_break_tolerance 未同步**: hold_break_tolerance_ms 在 JudgeEngine 中为 500ms，但未暴露给配置
