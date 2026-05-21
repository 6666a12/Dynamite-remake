package model

// Song 歌曲元数据
type Song struct {
	ID            string  `gorm:"primaryKey" json:"id"`
	Title         string  `json:"title"`
	Artist        string  `json:"artist"`
	BPM           float64 `json:"bpm"`
	DurationSec   int     `json:"duration_sec"`
	CoverPath     string  `json:"cover_path"`
	AudioPath     string  `json:"audio_path"`
	ChartConstant float64 `json:"chart_constant"` // 谱面定数
}

// Chart 单谱面
type Chart struct {
	ID         string  `gorm:"primaryKey" json:"id"`
	SongID     string  `gorm:"index" json:"song_id"`
	Difficulty string  `json:"difficulty"` // CASUAL..TERA
	FilePath   string  `json:"file_path"`
	NoteCount  int     `json:"note_count"`
	Constant   float64 `json:"constant"` // 该难度定数
}

// Score 成绩记录
type Score struct {
	ID           uint64  `gorm:"primaryKey" json:"id"`
	ChartID      string  `gorm:"index" json:"chart_id"`
	SongID       string  `gorm:"index" json:"song_id"`
	Perfect      int     `json:"perfect"`
	Good         int     `json:"good"`
	Miss         int     `json:"miss"`
	MaxCombo     int     `json:"max_combo"`
	Accuracy     float64 `json:"accuracy"`
	Score        int     `json:"score"`
	IsFullCombo  bool    `json:"is_full_combo"`
	IsAllPerfect bool    `json:"is_all_perfect"`
	Mods         string  `json:"mods"` // JSON: {"mirror":false,"bleed":true}
	PlayedAt     int64   `json:"played_at"` // Unix timestamp ms
}

// UserConfig 用户配置
type UserConfig struct {
	OffsetMs        int     `json:"offset_ms"`
	NoteSpeed       float64 `json:"note_speed"`
	AudioBufferSize int     `json:"audio_buffer_size"` // 96/128/256/512
	SkinName        string  `json:"skin_name"`
	MirrorMod       bool    `json:"mirror_mod"`
	BleedMod        bool    `json:"bleed_mod"`
	KeyLayout       string  `json:"key_layout"` // 预留键位映射 JSON
}
