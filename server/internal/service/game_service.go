package service

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"time"

	"github.com/dynamite-rebuild/server/internal/model"
)

// GameService 游戏核心服务
type GameService struct {
	db *sql.DB
}

// NewGameService 创建服务实例
func NewGameService(db *sql.DB) *GameService {
	return &GameService{db: db}
}

// InitDB 初始化数据库表结构
func (s *GameService) InitDB() error {
	schema := `
	CREATE TABLE IF NOT EXISTS songs (
		id TEXT PRIMARY KEY,
		title TEXT,
		artist TEXT,
		bpm REAL,
		duration_sec INTEGER,
		cover_path TEXT,
		audio_path TEXT,
		chart_constant REAL
	);
	CREATE TABLE IF NOT EXISTS charts (
		id TEXT PRIMARY KEY,
		song_id TEXT,
		difficulty TEXT,
		file_path TEXT,
		note_count INTEGER,
		constant REAL
	);
	CREATE TABLE IF NOT EXISTS scores (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		chart_id TEXT,
		song_id TEXT,
		perfect INTEGER,
		good INTEGER,
		miss INTEGER,
		max_combo INTEGER,
		accuracy REAL,
		score INTEGER,
		is_full_combo INTEGER,
		is_all_perfect INTEGER,
		mods TEXT,
		played_at INTEGER
	);
	CREATE TABLE IF NOT EXISTS user_config (
		id INTEGER PRIMARY KEY CHECK (id = 1),
		offset_ms INTEGER DEFAULT 0,
		note_speed REAL DEFAULT 1.0,
		audio_buffer_size INTEGER DEFAULT 128,
		skin_name TEXT DEFAULT 'default',
		mirror_mod INTEGER DEFAULT 0,
		bleed_mod INTEGER DEFAULT 0,
		key_layout TEXT DEFAULT '{}'
	);
	INSERT OR IGNORE INTO user_config (id) VALUES (1);
	`
	_, err := s.db.Exec(schema)
	return err
}

// GetConfig 读取用户配置
func (s *GameService) GetConfig() (*model.UserConfig, error) {
	row := s.db.QueryRow(`SELECT offset_ms, note_speed, audio_buffer_size, skin_name, mirror_mod, bleed_mod, key_layout FROM user_config WHERE id = 1`)
	var c model.UserConfig
	var mirror, bleed int
	err := row.Scan(&c.OffsetMs, &c.NoteSpeed, &c.AudioBufferSize, &c.SkinName, &mirror, &bleed, &c.KeyLayout)
	if err != nil {
		return nil, err
	}
	c.MirrorMod = mirror != 0
	c.BleedMod = bleed != 0
	return &c, nil
}

// SaveConfig 保存用户配置
func (s *GameService) SaveConfig(c *model.UserConfig) error {
	_, err := s.db.Exec(
		`UPDATE user_config SET offset_ms=?, note_speed=?, audio_buffer_size=?, skin_name=?, mirror_mod=?, bleed_mod=?, key_layout=? WHERE id=1`,
		c.OffsetMs, c.NoteSpeed, c.AudioBufferSize, c.SkinName, b2i(c.MirrorMod), b2i(c.BleedMod), c.KeyLayout,
	)
	return err
}

// SubmitScore 提交成绩（含服务端校验）
func (s *GameService) SubmitScore(chartID string, result *model.Score) error {
	// 基础校验
	if result.Accuracy < 0 || result.Accuracy > 100 {
		return fmt.Errorf("invalid accuracy: %.2f", result.Accuracy)
	}
	total := result.Perfect + result.Good + result.Miss
	if total == 0 {
		return fmt.Errorf("empty score")
	}
	if result.MaxCombo > total {
		return fmt.Errorf("max combo exceeds note count")
	}

	result.PlayedAt = time.Now().UnixMilli()
	modsJSON, _ := json.Marshal(map[string]bool{
		"mirror": result.Mods != "" && result.Mods != "{}",
		"bleed":  false,
	})
	result.Mods = string(modsJSON)

	_, err := s.db.Exec(
		`INSERT INTO scores (chart_id, song_id, perfect, good, miss, max_combo, accuracy, score, is_full_combo, is_all_perfect, mods, played_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
		result.ChartID, result.SongID, result.Perfect, result.Good, result.Miss, result.MaxCombo,
		result.Accuracy, result.Score, b2i(result.IsFullCombo), b2i(result.IsAllPerfect), result.Mods, result.PlayedAt,
	)
	return err
}

// GetSongList 获取歌曲列表
func (s *GameService) GetSongList() ([]model.Song, error) {
	rows, err := s.db.Query(`SELECT id, title, artist, bpm, duration_sec, cover_path, audio_path, chart_constant FROM songs`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var songs []model.Song
	for rows.Next() {
		var song model.Song
		if err := rows.Scan(&song.ID, &song.Title, &song.Artist, &song.BPM, &song.DurationSec, &song.CoverPath, &song.AudioPath, &song.ChartConstant); err != nil {
			continue
		}
		songs = append(songs, song)
	}
	return songs, nil
}

func b2i(b bool) int {
	if b {
		return 1
	}
	return 0
}
