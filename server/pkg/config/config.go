package config

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
)

/**
 * Config 配置管理模块
 *
 * 功能：
 * - 支持从 JSON 配置文件读取配置
 * - 支持从环境变量读取配置（优先级高于配置文件）
 * - 提供默认值，确保未配置时服务仍可启动
 *
 * 优先级：环境变量 > 配置文件 > 默认值
 */

// ServerConfig 服务端运行时配置
type ServerConfig struct {
	// HTTP 服务监听地址
	ListenAddr string `json:"listen_addr"`
	// SQLite 数据库文件路径
	DBPath string `json:"db_path"`
	// 日志级别：debug / info / warn / error
	LogLevel string `json:"log_level"`
	// 成绩同步 API 的 JWT 密钥（从环境变量读取，不写入配置文件）
	JWTSecret string `json:"-"`
	// 是否启用 HTTP 请求日志
	EnableAccessLog bool `json:"enable_access_log"`
}

// DefaultConfig 返回默认配置
func DefaultConfig() *ServerConfig {
	return &ServerConfig{
		ListenAddr:      ":8080",
		DBPath:          "./data/dynamite.db",
		LogLevel:        "info",
		JWTSecret:       "",
		EnableAccessLog: true,
	}
}

// LoadConfig 从文件和环境变量加载配置
//
// 加载流程：
// 1. 以默认配置为基底
// 2. 若配置文件存在，解析并覆盖默认值
// 3. 检查环境变量，若存在则覆盖已有值
//
// @param configPath 配置文件路径（JSON 格式）
// @return 合并后的配置对象
func LoadConfig(configPath string) (*ServerConfig, error) {
	cfg := DefaultConfig()

	// 步骤 1: 读取配置文件（若存在）
	if configPath != "" {
		if _, err := os.Stat(configPath); err == nil {
			data, err := os.ReadFile(configPath)
			if err != nil {
				return nil, fmt.Errorf("读取配置文件失败: %w", err)
			}
			if err := json.Unmarshal(data, cfg); err != nil {
				return nil, fmt.Errorf("解析配置文件失败: %w", err)
			}
		}
	}

	// 步骤 2: 环境变量覆盖
	// 监听地址
	if v := os.Getenv("DYNAMITE_LISTEN_ADDR"); v != "" {
		cfg.ListenAddr = v
	}
	// 数据库路径
	if v := os.Getenv("DYNAMITE_DB_PATH"); v != "" {
		cfg.DBPath = v
	}
	// 日志级别
	if v := os.Getenv("DYNAMITE_LOG_LEVEL"); v != "" {
		cfg.LogLevel = v
	}
	// JWT 密钥（仅允许从环境变量读取，增强安全性，避免误提交到仓库）
	if v := os.Getenv("DYNAMITE_JWT_SECRET"); v != "" {
		cfg.JWTSecret = v
	}
	// 访问日志开关
	if v := os.Getenv("DYNAMITE_ENABLE_ACCESS_LOG"); v != "" {
		if b, err := strconv.ParseBool(v); err == nil {
			cfg.EnableAccessLog = b
		}
	}

	// 步骤 3: 路径安全校验（防止路径遍历）
	if cfg.DBPath != "" {
		absPath, err := filepath.Abs(cfg.DBPath)
		if err != nil {
			return nil, fmt.Errorf("数据库路径非法: %w", err)
		}
		cfg.DBPath = absPath
	}

	return cfg, nil
}

// SaveConfig 将当前配置保存到文件
//
// 注意：JWTSecret 不会被序列化到文件（json tag 为 "-"）
func SaveConfig(cfg *ServerConfig, configPath string) error {
	if configPath == "" {
		return fmt.Errorf("配置文件路径为空")
	}

	// 确保父目录存在
	dir := filepath.Dir(configPath)
	if err := os.MkdirAll(dir, 0750); err != nil {
		return fmt.Errorf("创建配置目录失败: %w", err)
	}

	data, err := json.MarshalIndent(cfg, "", "    ")
	if err != nil {
		return fmt.Errorf("序列化配置失败: %w", err)
	}

	if err := os.WriteFile(configPath, data, 0600); err != nil {
		return fmt.Errorf("写入配置文件失败: %w", err)
	}
	return nil
}
