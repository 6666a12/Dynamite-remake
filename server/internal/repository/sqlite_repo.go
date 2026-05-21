package repository

import (
	"database/sql"
	"fmt"
	"strings"
	"time"

	"github.com/dynamite-rebuild/server/internal/model"
)

// ---------------------------------------------------------------------------
// 接口定义
// ---------------------------------------------------------------------------

// SongRepo 歌曲数据访问接口
type SongRepo interface {
	InsertSong(song *model.Song) error
	GetSongByID(id string) (*model.Song, error)
	ListSongs() ([]model.Song, error)
}

// ChartRepo 谱面数据访问接口
type ChartRepo interface {
	InsertChart(chart *model.Chart) error
	GetChartByID(id string) (*model.Chart, error)
	GetChartsBySongID(songID string) ([]model.Chart, error)
}

// ScoreRepo 成绩数据访问接口
type ScoreRepo interface {
	InsertScore(score *model.Score) error
	GetScoresBySongID(songID string) ([]model.Score, error)
	GetRecentScores(limit int) ([]model.Score, error)
}

// 编译期检查：确保 SQLiteRepo 实现了所有接口
var (
	_ SongRepo  = (*SQLiteRepo)(nil)
	_ ChartRepo = (*SQLiteRepo)(nil)
	_ ScoreRepo = (*SQLiteRepo)(nil)
)

// ---------------------------------------------------------------------------
// SQLiteRepo 实现
// ---------------------------------------------------------------------------

// SQLiteRepo SQLite 仓储实现
// 封装歌曲、谱面、成绩的持久化 CRUD 操作
// 所有查询均使用参数化语句，杜绝 SQL 注入
type SQLiteRepo struct {
	db *sql.DB
}

// NewSQLiteRepo 创建 SQLite 仓储实例
func NewSQLiteRepo(db *sql.DB) *SQLiteRepo {
	return &SQLiteRepo{db: db}
}

// ---------------------------------------------------------------------------
// 歌曲相关操作
// ---------------------------------------------------------------------------

// validateSong 校验歌曲输入的合法性
func validateSong(song *model.Song) error {
	if song == nil {
		return fmt.Errorf("歌曲对象不能为空")
	}
	if strings.TrimSpace(song.ID) == "" {
		return fmt.Errorf("歌曲 ID 不能为空")
	}
	if strings.TrimSpace(song.Title) == "" {
		return fmt.Errorf("歌曲标题不能为空")
	}
	if song.BPM < 0 {
		return fmt.Errorf("BPM 不能为负数")
	}
	if song.DurationSec < 0 {
		return fmt.Errorf("歌曲时长不能为负数")
	}
	return nil
}

// InsertSong 插入一首歌曲记录
// 使用参数化查询，防止 song.ID 等字段被注入
func (r *SQLiteRepo) InsertSong(song *model.Song) error {
	if err := validateSong(song); err != nil {
		return fmt.Errorf("输入校验失败: %w", err)
	}
	_, err := r.db.Exec(
		`INSERT INTO songs (id, title, artist, bpm, duration_sec, cover_path, audio_path, chart_constant)
		 VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
		song.ID, song.Title, song.Artist, song.BPM, song.DurationSec,
		song.CoverPath, song.AudioPath, song.ChartConstant,
	)
	if err != nil {
		return fmt.Errorf("插入歌曲失败: %w", err)
	}
	return nil
}

// GetSongByID 根据 ID 查询歌曲
func (r *SQLiteRepo) GetSongByID(id string) (*model.Song, error) {
	if strings.TrimSpace(id) == "" {
		return nil, fmt.Errorf("歌曲 ID 不能为空")
	}
	row := r.db.QueryRow(
		`SELECT id, title, artist, bpm, duration_sec, cover_path, audio_path, chart_constant FROM songs WHERE id = ?`,
		id,
	)
	var s model.Song
	err := row.Scan(&s.ID, &s.Title, &s.Artist, &s.BPM, &s.DurationSec, &s.CoverPath, &s.AudioPath, &s.ChartConstant)
	if err != nil {
		if err == sql.ErrNoRows {
			return nil, fmt.Errorf("歌曲不存在: %s", id)
		}
		return nil, fmt.Errorf("查询歌曲失败: %w", err)
	}
	return &s, nil
}

// ListSongs 查询所有歌曲列表
func (r *SQLiteRepo) ListSongs() ([]model.Song, error) {
	rows, err := r.db.Query(
		`SELECT id, title, artist, bpm, duration_sec, cover_path, audio_path, chart_constant FROM songs`,
	)
	if err != nil {
		return nil, fmt.Errorf("查询歌曲列表失败: %w", err)
	}
	defer rows.Close()

	var songs []model.Song
	for rows.Next() {
		var s model.Song
		if err := rows.Scan(&s.ID, &s.Title, &s.Artist, &s.BPM, &s.DurationSec, &s.CoverPath, &s.AudioPath, &s.ChartConstant); err != nil {
			continue // 单条解析失败不影响整体列表
		}
		songs = append(songs, s)
	}
	return songs, rows.Err()
}

// ---------------------------------------------------------------------------
// 谱面相关操作
// ---------------------------------------------------------------------------

// validateChart 校验谱面输入的合法性
func validateChart(chart *model.Chart) error {
	if chart == nil {
		return fmt.Errorf("谱面对象不能为空")
	}
	if strings.TrimSpace(chart.ID) == "" {
		return fmt.Errorf("谱面 ID 不能为空")
	}
	if strings.TrimSpace(chart.SongID) == "" {
		return fmt.Errorf("歌曲 ID 不能为空")
	}
	if strings.TrimSpace(chart.Difficulty) == "" {
		return fmt.Errorf("难度不能为空")
	}
	if chart.NoteCount < 0 {
		return fmt.Errorf("物量不能为负数")
	}
	if chart.Constant < 0 {
		return fmt.Errorf("定数不能为负数")
	}
	return nil
}

// InsertChart 插入谱面记录
func (r *SQLiteRepo) InsertChart(chart *model.Chart) error {
	if err := validateChart(chart); err != nil {
		return fmt.Errorf("输入校验失败: %w", err)
	}
	_, err := r.db.Exec(
		`INSERT INTO charts (id, song_id, difficulty, file_path, note_count, constant)
		 VALUES (?, ?, ?, ?, ?, ?)`,
		chart.ID, chart.SongID, chart.Difficulty, chart.FilePath, chart.NoteCount, chart.Constant,
	)
	if err != nil {
		return fmt.Errorf("插入谱面失败: %w", err)
	}
	return nil
}

// GetChartByID 根据 ID 查询单条谱面
func (r *SQLiteRepo) GetChartByID(id string) (*model.Chart, error) {
	if strings.TrimSpace(id) == "" {
		return nil, fmt.Errorf("谱面 ID 不能为空")
	}
	row := r.db.QueryRow(
		`SELECT id, song_id, difficulty, file_path, note_count, constant FROM charts WHERE id = ?`,
		id,
	)
	var c model.Chart
	err := row.Scan(&c.ID, &c.SongID, &c.Difficulty, &c.FilePath, &c.NoteCount, &c.Constant)
	if err != nil {
		if err == sql.ErrNoRows {
			return nil, fmt.Errorf("谱面不存在: %s", id)
		}
		return nil, fmt.Errorf("查询谱面失败: %w", err)
	}
	return &c, nil
}

// GetChartsBySongID 获取某首歌曲的所有谱面
func (r *SQLiteRepo) GetChartsBySongID(songID string) ([]model.Chart, error) {
	if strings.TrimSpace(songID) == "" {
		return nil, fmt.Errorf("歌曲 ID 不能为空")
	}
	rows, err := r.db.Query(
		`SELECT id, song_id, difficulty, file_path, note_count, constant FROM charts WHERE song_id = ?`,
		songID,
	)
	if err != nil {
		return nil, fmt.Errorf("查询谱面失败: %w", err)
	}
	defer rows.Close()

	var charts []model.Chart
	for rows.Next() {
		var c model.Chart
		if err := rows.Scan(&c.ID, &c.SongID, &c.Difficulty, &c.FilePath, &c.NoteCount, &c.Constant); err != nil {
			continue
		}
		charts = append(charts, c)
	}
	return charts, rows.Err()
}

// ---------------------------------------------------------------------------
// 成绩相关操作
// ---------------------------------------------------------------------------

// validateScore 校验成绩输入的合法性
func validateScore(score *model.Score) error {
	if score == nil {
		return fmt.Errorf("成绩对象不能为空")
	}
	if strings.TrimSpace(score.ChartID) == "" {
		return fmt.Errorf("谱面 ID 不能为空")
	}
	if strings.TrimSpace(score.SongID) == "" {
		return fmt.Errorf("歌曲 ID 不能为空")
	}
	if score.Perfect < 0 || score.Good < 0 || score.Miss < 0 {
		return fmt.Errorf("判定计数不能为负数")
	}
	if score.MaxCombo < 0 {
		return fmt.Errorf("最大连击不能为负数")
	}
	if score.Accuracy < 0 || score.Accuracy > 100 {
		return fmt.Errorf("准确率必须在 [0, 100] 范围内")
	}
	return nil
}

// InsertScore 插入一条成绩记录
func (r *SQLiteRepo) InsertScore(score *model.Score) error {
	if err := validateScore(score); err != nil {
		return fmt.Errorf("输入校验失败: %w", err)
	}
	if score.PlayedAt == 0 {
		score.PlayedAt = time.Now().UnixMilli()
	}
	_, err := r.db.Exec(
		`INSERT INTO scores (chart_id, song_id, perfect, good, miss, max_combo, accuracy, score, is_full_combo, is_all_perfect, mods, played_at)
		 VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
		score.ChartID, score.SongID, score.Perfect, score.Good, score.Miss,
		score.MaxCombo, score.Accuracy, score.Score,
		boolToInt(score.IsFullCombo), boolToInt(score.IsAllPerfect),
		score.Mods, score.PlayedAt,
	)
	if err != nil {
		return fmt.Errorf("插入成绩失败: %w", err)
	}
	return nil
}

// GetScoresBySongID 获取某首歌曲的所有历史成绩
func (r *SQLiteRepo) GetScoresBySongID(songID string) ([]model.Score, error) {
	if strings.TrimSpace(songID) == "" {
		return nil, fmt.Errorf("歌曲 ID 不能为空")
	}
	rows, err := r.db.Query(
		`SELECT id, chart_id, song_id, perfect, good, miss, max_combo, accuracy, score, is_full_combo, is_all_perfect, mods, played_at
		 FROM scores WHERE song_id = ? ORDER BY played_at DESC`,
		songID,
	)
	if err != nil {
		return nil, fmt.Errorf("查询成绩失败: %w", err)
	}
	defer rows.Close()

	var scores []model.Score
	for rows.Next() {
		var sc model.Score
		var fc, ap int
		if err := rows.Scan(&sc.ID, &sc.ChartID, &sc.SongID, &sc.Perfect, &sc.Good, &sc.Miss,
			&sc.MaxCombo, &sc.Accuracy, &sc.Score, &fc, &ap, &sc.Mods, &sc.PlayedAt); err != nil {
			continue
		}
		sc.IsFullCombo = fc != 0
		sc.IsAllPerfect = ap != 0
		scores = append(scores, sc)
	}
	return scores, rows.Err()
}

// GetRecentScores 获取最近 N 条成绩（用于首页展示或本地缓存同步）
func (r *SQLiteRepo) GetRecentScores(limit int) ([]model.Score, error) {
	// limit 必须为正值，防止异常查询
	if limit <= 0 {
		limit = 10
	}
	rows, err := r.db.Query(
		`SELECT id, chart_id, song_id, perfect, good, miss, max_combo, accuracy, score, is_full_combo, is_all_perfect, mods, played_at
		 FROM scores ORDER BY played_at DESC LIMIT ?`,
		limit,
	)
	if err != nil {
		return nil, fmt.Errorf("查询最近成绩失败: %w", err)
	}
	defer rows.Close()

	var scores []model.Score
	for rows.Next() {
		var sc model.Score
		var fc, ap int
		if err := rows.Scan(&sc.ID, &sc.ChartID, &sc.SongID, &sc.Perfect, &sc.Good, &sc.Miss,
			&sc.MaxCombo, &sc.Accuracy, &sc.Score, &fc, &ap, &sc.Mods, &sc.PlayedAt); err != nil {
			continue
		}
		sc.IsFullCombo = fc != 0
		sc.IsAllPerfect = ap != 0
		scores = append(scores, sc)
	}
	return scores, rows.Err()
}

// ---------------------------------------------------------------------------
// 辅助函数
// ---------------------------------------------------------------------------

func boolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}
