package mobile

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"github.com/dynamite-rebuild/server/internal/model"
	"github.com/dynamite-rebuild/server/internal/service"
	_ "github.com/mattn/go-sqlite3"
)

// DataManager 是 gomobile bind 暴露的主入口
type DataManager struct {
	svc *service.GameService
	db  *sql.DB
}

// Init 初始化数据库（在平台启动时调用）
func (d *DataManager) Init(dbPath string) error {
	// 确保目录存在
	dir := filepath.Dir(dbPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("create db dir: %w", err)
	}

	db, err := sql.Open("sqlite3", dbPath)
	if err != nil {
		return fmt.Errorf("open sqlite: %w", err)
	}
	d.db = db
	d.svc = service.NewGameService(db)
	return d.svc.InitDB()
}

// Close 关闭数据库
func (d *DataManager) Close() error {
	if d.db != nil {
		return d.db.Close()
	}
	return nil
}

// LoadUserConfig 读取用户配置（返回 JSON 字符串）
func (d *DataManager) LoadUserConfig() string {
	cfg, err := d.svc.GetConfig()
	if err != nil {
		return "{}"
	}
	b, _ := json.Marshal(cfg)
	return string(b)
}

// SaveUserConfig 保存用户配置（接收 JSON 字符串）
func (d *DataManager) SaveUserConfig(jsonStr string) error {
	var cfg model.UserConfig
	if err := json.Unmarshal([]byte(jsonStr), &cfg); err != nil {
		return err
	}
	return d.svc.SaveConfig(&cfg)
}

// GetSongList 返回歌曲列表 JSON
func (d *DataManager) GetSongList() string {
	songs, err := d.svc.GetSongList()
	if err != nil {
		return "[]"
	}
	b, _ := json.Marshal(songs)
	return string(b)
}

// SubmitScore 提交成绩（resultJSON 为 Score 的 JSON 字符串）
func (d *DataManager) SubmitScore(chartID string, songID string, resultJSON string) error {
	var result model.Score
	if err := json.Unmarshal([]byte(resultJSON), &result); err != nil {
		return err
	}
	result.ChartID = chartID
	result.SongID = songID
	return d.svc.SubmitScore(chartID, &result)
}

// GetLocalLeaderboard 获取本地排行榜（返回 JSON）
func (d *DataManager) GetLocalLeaderboard(chartID string, limit int) string {
	rows, err := d.db.Query(
		`SELECT perfect, good, miss, max_combo, accuracy, score, is_full_combo, is_all_perfect, played_at FROM scores WHERE chart_id = ? ORDER BY score DESC LIMIT ?`,
		chartID, limit,
	)
	if err != nil {
		return "[]"
	}
	defer rows.Close()

	type entry struct {
		Perfect      int     `json:"perfect"`
		Good         int     `json:"good"`
		Miss         int     `json:"miss"`
		MaxCombo     int     `json:"max_combo"`
		Accuracy     float64 `json:"accuracy"`
		Score        int     `json:"score"`
		IsFullCombo  bool    `json:"is_full_combo"`
		IsAllPerfect bool    `json:"is_all_perfect"`
		PlayedAt     int64   `json:"played_at"`
	}

	var results []entry
	for rows.Next() {
		var e entry
		var fc, ap int
		if err := rows.Scan(&e.Perfect, &e.Good, &e.Miss, &e.MaxCombo, &e.Accuracy, &e.Score, &fc, &ap, &e.PlayedAt); err != nil {
			continue
		}
		e.IsFullCombo = fc != 0
		e.IsAllPerfect = ap != 0
		results = append(results, e)
	}
	b, _ := json.Marshal(results)
	return string(b)
}

// GetChartPath 返回指定谱面的本地绝对路径（简单实现，实际应查数据库）
func (d *DataManager) GetChartPath(songID string, difficulty string) string {
	// 约定路径格式
	return filepath.Join("songs", songID, "chart_"+difficulty+".chart")
}
