package service

import (
	"database/sql"
	"fmt"
	"time"

	"github.com/dynamite-rebuild/server/internal/model"
)

// ScoreService 成绩业务服务
// 负责成绩的服务端校验与持久化
type ScoreService struct {
	db *sql.DB
}

// NewScoreService 创建成绩服务实例
func NewScoreService(db *sql.DB) *ScoreService {
	return &ScoreService{db: db}
}

// SubmitScore 提交成绩，执行严格的服务端校验后保存到数据库。
//
// 校验规则：
//  1. chart_id 与 song_id 非空
//  2. 物量一致性：perfect + good + miss == note_count（从数据库 charts 表查询）
//  3. 准确率范围：[0, 100]
//  4. max_combo 不超过物量
//  5. 时间戳合理性：不能是未来时间，不能早于 7 天前
//  6. 自动计算 IsFullCombo 与 IsAllPerfect
//  7. 所有写入操作使用参数化查询，防止 SQL 注入
func (s *ScoreService) SubmitScore(score *model.Score) error {
	if score == nil {
		return fmt.Errorf("成绩对象不能为空")
	}
	if score.ChartID == "" {
		return fmt.Errorf("chart_id 不能为空")
	}
	if score.SongID == "" {
		return fmt.Errorf("song_id 不能为空")
	}

	// -------------------------------------------------
	// 1. 查询谱面物量（参数化查询）
	// -------------------------------------------------
	var noteCount int
	err := s.db.QueryRow(
		`SELECT note_count FROM charts WHERE id = ?`,
		score.ChartID,
	).Scan(&noteCount)
	if err != nil {
		if err == sql.ErrNoRows {
			return fmt.Errorf("谱面不存在: %s", score.ChartID)
		}
		return fmt.Errorf("查询谱面物量失败: %w", err)
	}

	// -------------------------------------------------
	// 2. 物量一致性校验
	// -------------------------------------------------
	total := score.Perfect + score.Good + score.Miss
	if total != noteCount {
		return fmt.Errorf("物量不一致: perfect(%d)+good(%d)+miss(%d)=%d, 期望=%d",
			score.Perfect, score.Good, score.Miss, total, noteCount)
	}

	// -------------------------------------------------
	// 3. 准确率范围校验
	// -------------------------------------------------
	if score.Accuracy < 0 || score.Accuracy > 100 {
		return fmt.Errorf("准确率非法: %.2f, 必须在 [0, 100] 范围内", score.Accuracy)
	}

	// -------------------------------------------------
	// 4. MaxCombo 校验
	// -------------------------------------------------
	if score.MaxCombo > noteCount {
		return fmt.Errorf("max_combo(%d) 超过物量(%d)", score.MaxCombo, noteCount)
	}

	// -------------------------------------------------
	// 5. 时间戳合理性校验
	// -------------------------------------------------
	now := time.Now().UnixMilli()
	const maxAgeMs = 7 * 24 * 60 * 60 * 1000 // 7 天，单位毫秒
	if score.PlayedAt == 0 {
		score.PlayedAt = now
	} else {
		if score.PlayedAt > now {
			return fmt.Errorf("时间戳在未来: %d", score.PlayedAt)
		}
		if score.PlayedAt < now-maxAgeMs {
			return fmt.Errorf("时间戳过于久远: %d", score.PlayedAt)
		}
	}

	// -------------------------------------------------
	// 6. 自动计算派生字段
	// -------------------------------------------------
	score.IsFullCombo = score.Miss == 0 && score.MaxCombo == noteCount
	score.IsAllPerfect = score.Miss == 0 && score.Good == 0 && score.Perfect == noteCount

	// 处理 mods 字段，确保为合法 JSON
	if score.Mods == "" {
		score.Mods = "{}"
	}

	// -------------------------------------------------
	// 7. 参数化查询保存成绩
	// -------------------------------------------------
	_, err = s.db.Exec(
		`INSERT INTO scores (chart_id, song_id, perfect, good, miss, max_combo, accuracy, score, is_full_combo, is_all_perfect, mods, played_at)
		 VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
		score.ChartID, score.SongID, score.Perfect, score.Good, score.Miss,
		score.MaxCombo, score.Accuracy, score.Score,
		b2i(score.IsFullCombo), b2i(score.IsAllPerfect),
		score.Mods, score.PlayedAt,
	)
	if err != nil {
		return fmt.Errorf("保存成绩失败: %w", err)
	}

	return nil
}
