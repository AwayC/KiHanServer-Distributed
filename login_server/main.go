package main

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/redis/go-redis/v9"
	"gorm.io/driver/mysql"
	"gorm.io/gorm"
)

// --- 1. 配置结构体 ---
type Config struct {
	Mysql struct {
		Host     string `json:"host"`
		Port     int    `json:"port"`
		User     string `json:"user"`
		Password string `json:"password"`
		Database string `json:"database"`
	} `json:"mysql"`
	Redis struct {
		Host     string `json:"host"`
		Port     int    `json:"port"`
		Password string `json:"password"`
	} `json:"redis"`
	Gateway struct {
		Ip   string `json:"ip"`
		Port int    `json:"port"`
	} `json:"gateway"`
	Version string `json:"version"` // 🚨 新增：服务器当前最新版本号 (例如 "1.1.5")
}

// LoginServer Errcode 1000 - 1999
const (
	ErrOk = 0 // ok = 0

	ErrApiBadReq        = -1000
	ErrApiInternalError = -1001

	ErrApiBadPassword = -1100
	ErrApiBadToken    = -1101

	ErrApiDbError = -1200

	ErrDbExists    = -1201
	ErrDbNotExists = -1202

	ErrInvalidVersion = -1300 // 🚨 版本校验失败错误码
)

// --- 3. 请求结构体 ---

// 🚨 新增：开机版本校验请求 Body
type CheckVersionRequest struct {
	Version string `json:"version" binding:"required"`
}

type RegisterRequest struct {
	Username string `json:"username" binding:"required"`
	Password string `json:"password" binding:"required"`
	Email    string `json:"email" binding:"required,email"`
}

type LoginRequest struct {
	Username string `json:"username" binding:"required"`
	Password string `json:"password" binding:"required"`
}

type UpdateEmailRequest struct {
	Email string `json:"email" binding:"required,email"`
	Token string `json:"token" binding:"required"`
}

type UpdatePasswordRequest struct {
	Password string `json:"password" binding:"required"`
	Token    string `json:"token" binding:"required"`
}

var (
	rdb *redis.Client
	db  *gorm.DB
	cfg *Config
	ctx = context.Background()
)

// --- 4. 核心工具函数 ---

func loadConfig() {
	data, err := os.ReadFile("../config.json")
	if err != nil {
		panic("读取 config.json 失败: " + err.Error())
	}
	cfg = &Config{}
	if err := json.Unmarshal(data, cfg); err != nil {
		panic("解析 config.json 失败: " + err.Error())
	}
}

// 🚨 新增：优雅的语义化版本号比对函数
// 如果 v1 > v2 返回 1；v1 < v2 返回 -1；v1 == v2 返回 0
func CompareVersion(v1, v2 string) int {
	v1Parts := strings.Split(v1, ".")
	v2Parts := strings.Split(v2, ".")
	for i := 0; i < len(v1Parts) || i < len(v2Parts); i++ {
		var x, y int
		if i < len(v1Parts) {
			x, _ = strconv.Atoi(v1Parts[i])
		}
		if i < len(v2Parts) {
			y, _ = strconv.Atoi(v2Parts[i])
		}
		if x > y {
			return 1
		}
		if x < y {
			return -1
		}
	}
	return 0
}

func generateSalt() string {
	b := make([]byte, 8)
	if _, err := rand.Read(b); err != nil {
		return "default_salt_123"
	}
	return hex.EncodeToString(b)
}

func hashPassword(clientMd5, salt string) string {
	hasher := sha256.New()
	hasher.Write([]byte(clientMd5 + salt))
	return hex.EncodeToString(hasher.Sum(nil))
}

func generateSecureToken(uid int) string {
	nanoTime := time.Now().UnixNano()
	b := make([]byte, 8)
	_, _ = rand.Read(b)
	randomStr := hex.EncodeToString(b)

	rawPayload := fmt.Sprintf("%d-%d-%s", uid, nanoTime, randomStr)

	hasher := sha256.New()
	hasher.Write([]byte(rawPayload))
	return hex.EncodeToString(hasher.Sum(nil))
}

func saveTokenToRedis(uid int, token string) error {
	uidStr := fmt.Sprintf("%d", uid)
	err := rdb.Set(ctx, "token:"+uidStr, token, 24*time.Hour).Err()
	if err != nil {
		return err
	}
	return rdb.Set(ctx, "auth:"+token, uidStr, 24*time.Hour).Err()
}

func getUidByToken(token string) (string, error) {
	return rdb.Get(ctx, "auth:"+token).Result()
}

// --- 5. 初始化组件与并发连接池 ---
func initDependencies() {
	loadConfig()

	rdb = redis.NewClient(&redis.Options{
		Addr:     fmt.Sprintf("%s:%d", cfg.Redis.Host, cfg.Redis.Port),
		Password: cfg.Redis.Password,
		DB:       0,
	})

	dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?charset=utf8mb4&parseTime=True&loc=Local",
		cfg.Mysql.User, cfg.Mysql.Password, cfg.Mysql.Host, cfg.Mysql.Port, cfg.Mysql.Database)
	var err error
	db, err = gorm.Open(mysql.Open(dsn), &gorm.Config{})
	if err != nil {
		panic("连接 MySQL 失败: " + err.Error())
	}

	sqlDB, err := db.DB()
	if err != nil {
		panic("获取底层 sql.DB 失败: " + err.Error())
	}
	sqlDB.SetMaxIdleConns(20)
	sqlDB.SetMaxOpenConns(100)
	sqlDB.SetConnMaxLifetime(time.Hour)

	fmt.Println("正在检查并同步数据库表结构...")

	err = db.Exec(`CREATE TABLE IF NOT EXISTS users (
		uid INT AUTO_INCREMENT PRIMARY KEY,
		username VARCHAR(64) NOT NULL,
		password VARCHAR(128) NOT NULL,
		salt VARCHAR(32) NOT NULL,
		email VARCHAR(128) NOT NULL,
		register_time DATETIME DEFAULT CURRENT_TIMESTAMP,
		last_login_time DATETIME,
		last_login_ip VARCHAR(64),
		UNIQUE (username)
	) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;`).Error
	if err != nil {
		panic("创建 users 表失败: " + err.Error())
	}

	fmt.Println("数据库初始化及高并发连接池调校全部完成！")
}

// --- 6. 主程序与业务路由 ---
func main() {
	initDependencies()

	r := gin.Default()

	api := r.Group("/api")
	{
		// 🚨 业务零：URL : /api/check_version (开机第一步，纯内存高并发拦截)
		api.POST("/check_version", func(c *gin.Context) {
			var req CheckVersionRequest
			if err := c.ShouldBindJSON(&req); err != nil {
				c.JSON(http.StatusBadRequest, gin.H{
					"code":    ErrApiBadReq,
					"msg":     "param error: " + err.Error(),
					"version": cfg.Version,
				})
				return
			}

			// 比对客户端版本与服务器配置的最新版本号
			// CompareVersion 返回 -1 说明客户端版本低了
			if CompareVersion(req.Version, cfg.Version) < 0 {
				c.JSON(http.StatusOK, gin.H{
					"code":    ErrInvalidVersion, // -1300
					"msg":     "version invalid, please update",
					"version": cfg.Version, // 回传最新的服务器版本给前端
				})
				return
			}

			// 版本正常放行
			c.JSON(http.StatusOK, gin.H{
				"code":    ErrOk, // 0
				"msg":     "success",
				"version": cfg.Version,
			})
		})

		// 业务一：URL : /api/register
		api.POST("/register", func(c *gin.Context) {
			var req RegisterRequest
			if err := c.ShouldBindJSON(&req); err != nil {
				fmt.Printf("收到注册请求内容: %+v\n", req)
				c.JSON(http.StatusBadRequest, gin.H{
					"code": ErrApiBadReq,
					"msg":  "参数验证失败: " + err.Error(),
				})
				return
			}

			salt := generateSalt()
			serverHashedPassword := hashPassword(req.Password, salt)

			sqlUser := `INSERT INTO users (username, password, salt, email) VALUES (?, ?, ?, ?)`
			result := db.Exec(sqlUser, req.Username, serverHashedPassword, salt, req.Email)
			if result.Error != nil {
				c.JSON(http.StatusConflict, gin.H{
					"code": ErrDbExists,
					"msg":  "already exit name",
				})
				return
			}

			var uid int
			db.Raw("SELECT LAST_INSERT_ID()").Scan(&uid)

			token := generateSecureToken(uid)
			if err := saveTokenToRedis(uid, token); err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"code": ErrApiInternalError,
					"msg":  "internal error",
				})
				return
			}

			c.JSON(http.StatusOK, gin.H{
				"code": ErrOk,
				"msg":  "success",
				"data": gin.H{"token": token},
			})
		})

		// 业务二：URL : /api/login
		api.POST("/login", func(c *gin.Context) {
			var req LoginRequest
			if err := c.ShouldBindJSON(&req); err != nil {
				c.JSON(http.StatusBadRequest, gin.H{
					"code": ErrApiBadReq,
					"msg":  "param error",
				})
				return
			}

			var user struct {
				Uid      int
				Password string
				Salt     string
			}
			err := db.Raw("SELECT uid, password, salt FROM users WHERE username = ?", req.Username).Scan(&user).Error
			if err != nil || user.Uid == 0 {
				c.JSON(http.StatusUnauthorized, gin.H{
					"code": ErrDbNotExists,
					"msg":  "not exit",
				})
				return
			}

			if hashPassword(req.Password, user.Salt) != user.Password {
				c.JSON(http.StatusUnauthorized, gin.H{
					"code": ErrApiBadPassword,
					"msg":  "password error",
				})
				return
			}

			clientIP := c.ClientIP()
			db.Exec("UPDATE users SET last_login_time = NOW(), last_login_ip = ? WHERE uid = ?", clientIP, user.Uid)

			token := generateSecureToken(user.Uid)
			if err := saveTokenToRedis(user.Uid, token); err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"code": ErrApiInternalError,
					"msg":  "internal error",
				})
				return
			}

			// 🚨 新增：顶号处理，通知 Gateway 踢掉旧连接
			kickMsg := fmt.Sprintf(`{"uid": "%d", "new_token": "%s"}`, user.Uid, token)
			rdb.Publish(ctx, "gateway:kick", kickMsg)

			c.JSON(http.StatusOK, gin.H{
				"code": ErrOk,
				"msg":  "success",
				"data": gin.H{"token": token},
			})
		})

		// 业务三：URL : /api/update_email
		api.POST("/update_email", func(c *gin.Context) {
			var req UpdateEmailRequest
			if err := c.ShouldBindJSON(&req); err != nil {
				c.JSON(http.StatusBadRequest, gin.H{
					"code": ErrApiBadReq,
					"msg":  "param error: " + err.Error(),
				})
				return
			}

			uidStr, err := getUidByToken(req.Token)
			if err == redis.Nil {
				c.JSON(http.StatusUnauthorized, gin.H{
					"code": ErrApiBadToken,
					"msg":  "invalid token",
				})
				return
			}

			sqlUpdate := `UPDATE users SET email = ? WHERE uid = ?`
			if err := db.Exec(sqlUpdate, req.Email, uidStr).Error; err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"code": ErrApiDbError,
					"msg":  "db error",
				})
				return
			}

			c.JSON(http.StatusOK, gin.H{
				"code": ErrOk,
				"msg":  "success",
			})
		})

		// 业务四：URL : /api/update_password
		api.POST("/update_password", func(c *gin.Context) {
			var req UpdatePasswordRequest
			if err := c.ShouldBindJSON(&req); err != nil {
				c.JSON(http.StatusBadRequest, gin.H{
					"code": ErrApiBadReq,
					"msg":  "param error: " + err.Error(),
				})
				return
			}

			uidStr, err := getUidByToken(req.Token)
			if err == redis.Nil {
				c.JSON(http.StatusUnauthorized, gin.H{
					"code": ErrApiBadToken,
					"msg":  "token invalid",
				})
				return
			}

			newSalt := generateSalt()
			newServerHashedPassword := hashPassword(req.Password, newSalt)

			sqlUpdate := `UPDATE users SET password = ?, salt = ? WHERE uid = ?`
			if err := db.Exec(sqlUpdate, newServerHashedPassword, newSalt, uidStr).Error; err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"code": ErrApiDbError,
					"msg":  "db error",
				})
				return
			}

			c.JSON(http.StatusOK, gin.H{
				"code": ErrOk,
				"msg":  "success",
			})
		})
	}

	r.Run(":8080")
}