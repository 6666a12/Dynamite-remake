# 谱面格式规范

> 本文档定义 Dynamite 重构项目使用的谱面格式标准。项目原生兼容 **DynaMaker 社区谱面格式**，无需格式转换。

---

## 1. 核心理念

**直接兼容 DynaMaker 社区生态，不发明自定义二进制格式。**

DynaMaker 社区是全球 Dynamite 玩家的主要谱面分发渠道，拥有成熟的谱面创建、发布和分享工具链。本项目的设计目标是**无缝兼容**这些现有资源。

---

## 2. 谱面文件格式

### 2.1 DynaMaker XML（.xml）

原始游戏使用的 XML 序列化格式，以 CMap 为根元素的 Unity 序列化结构。

**示例：**
`xml
<?xml version=""1.0"" encoding=""UTF-8"" ?>
<CMap xmlns:xsd=""http://www.w3.org/2001/XMLSchema"" xmlns:xsi=""http://www.w3.org/2001/XMLSchema-instance"">
  <m_barPerMin>180</m_barPerMin>       <!-- BPM -->
  <m_timeOffset>0.0193875</m_timeOffset> <!-- 全局偏移（秒） -->
  <m_leftRegion>MIXER</m_leftRegion>    <!-- 左侧轨道类型 -->
  <m_rightRegion>PAD</m_rightRegion>    <!-- 右侧轨道类型 -->
  <m_mapID>_dym_SongName_H</m_mapID>    <!-- 谱面 ID（末尾字母标识难度） -->
  <m_notes>                              <!-- DOWN 轨道 -->
    <m_notes>
      <CMapNoteAsset>
        <m_id>0</m_id>
        <m_type>NORMAL</m_type>         <!-- NORMAL | HOLD | SUB | CHAIN -->
        <m_time>8</m_time>              <!-- 拍数时间（单位：拍） -->
        <m_position>0.75</m_position>   <!-- 轨道位置（0~5） -->
        <m_width>1.5</m_width>          <!-- 宽度 -->
        <m_subId>-1</m_subId>           <!-- HOLD→SUB 关联（-1=无） -->
      </CMapNoteAsset>
    </m_notes>
  </m_notes>
  <m_notesLeft>...</m_notesLeft>         <!-- LEFT 轨道 -->
  <m_notesRight>...</m_notesRight>       <!-- RIGHT 轨道 -->
</CMap>
`

**字段映射：**

| XML 字段 | 说明 | 类型映射 |
|----------|------|----------|
| m_type | 音符类型 | NORMAL→TAP，HOLD→HOLD_HEAD，SUB→HOLD_TAIL，CHAIN→SLIDE |
| m_time | 拍数时间 | 	ime_sec = m_time × (60 / BPM) - offset |
| m_position | 轨道位置 | 范围 0~5，对应横/纵向位置 |
| m_width | 音符宽度 | 范围 0~5 |
| m_subId | HOLD 关联 | HOLD→SUB 的 id 关联，用于计算 duration |

### 2.2 DynaMaker Web JSON（.json）

网页版 DynaMaker 使用的 JSON 格式，以轨道分组的数组结构。

**示例：**
`json
{
  ""left"": [{"time": 1000, ""type"": ""TAP"", ""position"": 0.75}],
  ""down"": [...],
  ""right"": [...],
  ""slide"": [{"time": 2000, ""type"": ""SLIDE"", ""position"": 2.5}]
}
`

### 2.3 社区分发包（.zip）

社区谱面分享标准格式：

`
[H10 G15]SongName.zip
├── info.json         # 元数据（曲名、作者、难度等）
├── music.mp3         # 完整曲目
├── preview.mp3       # 30秒预览
├── cover.png         # 封面图
├── HARD.xml          # HARD 难度谱面
└── GIGA.xml          # GIGA 难度谱面
`

**info.json 结构：**
`json
{
  ""musicName"": ""Rosenkreuz†Vampir"",
  ""musicComposer"": ""Mori†"",
  ""noterName"": ""NOCTURNE"",
  ""musicFile"": ""music.mp3"",
  ""cover"": ""cover.png"",
  ""previewFile"": ""preview.mp3"",
  ""difficulties"": {
    ""HARD"": ""10"",
    ""GIGA"": ""15""
  }
}
`

---

## 3. 运行时数据结构

### 3.1 音符数据（NoteData）

`cpp
struct NoteData {
    uint32_t   id;          // 音符唯一编号
    NoteType   type;        // TAP / HOLD_HEAD / HOLD_BODY / HOLD_TAIL / SLIDE
    double     time_sec;    // 判定时间（秒，已包含 offset 校正）
    uint64_t   time_ms;     // 判定时间（毫秒）
    SideType   side;        // LEFT / DOWN / RIGHT
    float      position;    // 轨道位置 0~5
    float      width;       // 音符宽度
    uint32_t   sub_id;      // HOLD→SUB 关联 id
    uint64_t   duration_ms; // 持续时间（由 sub_id 自动计算）
    uint32_t   flags;       // bit0=双押, bit1=滑条起点, bit2=滑条终点
    uint32_t   next_id;     // Slide 链中下一个 id
};
`

### 3.2 BPM 事件（BPMEvent）

`cpp
struct BPMEvent {
    double   time_sec;   // 秒
    uint64_t time_ms;    // 毫秒
    float    bpm;
    uint32_t beat_num;   // 拍号分子
    uint32_t beat_den;   // 拍号分母
};
`

### 3.3 谱面（Chart）

`cpp
struct Chart {
    std::string song_id;
    std::string difficulty;     // ""CASUAL"" / ""NORMAL"" / ""HARD"" / ""MEGA"" / ""GIGA"" / ""TERA""
    std::string map_id;         // XML 中的 m_mapID
    float       bpm;            // 基础 BPM
    double      offset_sec;     // 全局时间偏移
    std::string left_region;    // LEFT 轨道类型
    std::string right_region;   // RIGHT 轨道类型
    // ...notes, bpm_events...
};
`

---

## 4. 解析流程

`
输入文件 → 识别后缀
  ├── .xml  → parseXmlFile()  → 正则提取 CMapNoteAsset → beatToSec() 转换 → Chart
  ├── .json → parseDynaMakerJson() → nlohmann/json 解析 → Chart
  └── .zip  → parsePackage()  → 解压 info.json + *.xml → 逐一解析 → [Chart...]
`

### 时间转换公式

XML 中 m_time 以「拍」为单位，转换为秒：

`
time_sec = m_time × (60.0 / bpm) - m_timeOffset
`

---

## 5. 目录组织

### 内置歌曲
`
assets/songs/{song_id}/
├── bgm.mp3          # 完整曲目
├── preview.mp3      # 预览
├── cover.png        # 封面
├── metadata.json    # 元数据
├── HARD.xml         # HARD 难度
└── GIGA.xml         # GIGA 难度
`

### 用户导入
`
assets/imported/
└── [H10 G15]SongName.zip  # 社区包直接拖入
`

---

*文档版本: v2.0 | 更新日期: 2026-05-20*