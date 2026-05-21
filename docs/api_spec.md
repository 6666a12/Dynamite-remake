# REST API 规范

> 本文档定义 Dynamite 重构项目后端提供的 REST API，基于《Dynamite 社区生态后端兼容性规格书》中的 REST 简化层设计。

---

## 1. 概述

重构项目采用 **GraphQL + REST 双协议** 架构：
- **GraphQL**：用于兼容 Dynamite 官方客户端及社区私服协议。
- **REST**：用于 C++ GameCore 与 Go DataLayer 的内部通信，降低开发复杂度。

所有 REST 接口返回 JSON，时间戳统一使用毫秒级 Unix 时间戳（`int64`）。

---

## 2. 认证机制

### 2.1 Token 传递

在请求 Header 中携带 JWT：

```http
Authorization: Bearer <JWT>
```

- Token 有效期：7 天
- 支持 Refresh Token 机制
- 离线模式：无 Token 时以 `guest` 身份访问，成绩仅本地存储

### 2.2 离线降级

当客户端无网络或 Token 失效时，Go DataLayer 自动降级为 **SQLite 本地模式**，除联机对战和全球排行外，所有功能正常可用。

---

## 3. 路由定义

### 3.1 认证模块

#### `POST /api/v1/auth/register`

用户注册。

**请求体：**
```json
{
  "username": "string",
  "password": "string"
}
```

**响应体：**
```json
{
  "token": "JWT",
  "user": {
    "id": "ObjectId",
    "username": "string",
    "rValue": 0.0,
    "currency": { "diamond": 0, "leaf": 0, "badge": 0, "crystal": 0 }
  }
}
```

#### `POST /api/v1/auth/login`

用户登录。

**请求体：** 同 `register`。

**响应体：** 同 `register`。

---

### 3.2 用户模块

#### `GET /api/v1/me`

获取当前登录用户信息。

**响应体：**
```json
{
  "id": "ObjectId",
  "username": "string",
  "avatarUrl": "string",
  "rValue": 2450.3,
  "currency": {
    "diamond": 0,
    "leaf": 10,
    "badge": 27734,
    "crystal": 292000
  },
  "settings": {
    "speed": 5.5,
    "offset": 15,
    "mirror": false,
    "bleed": false,
    "auto": false
  }
}
```

#### `GET /api/v1/users/:id`

获取指定用户信息。

**响应体：** 同 `GET /api/v1/me`（不含敏感字段如 `settings`）。

#### `PUT /api/v1/settings`

更新用户设置。

**请求体：**
```json
{
  "speed": 5.5,
  "offset": 15,
  "mirror": false,
  "bleed": false,
  "auto": false
}
```

#### `POST /api/v1/avatar`

上传头像（`multipart/form-data`）。

---

### 3.3 歌曲 / 谱面模块

#### `GET /api/v1/chartsets`

获取歌曲集列表。

**查询参数：**
| 参数 | 类型 | 说明 |
|------|------|------|
| `search` | `string` | 按曲名/作曲家搜索（可选） |
| `difficulty` | `string` | 按难度过滤（可选） |
| `ranked` | `boolean` | 仅显示 Ranked 曲目（可选） |
| `limit` | `int` | 分页大小，默认 50 |
| `offset` | `int` | 分页偏移，默认 0 |

**响应体：**
```json
[
  {
    "id": "ObjectId",
    "musicName": "きらきらタイム☆",
    "musicComposer": "さわわ",
    "coverUrl": "/download/cover/encoded/{setId}",
    "previewUrl": "/download/preview/encoded/{setId}",
    "coverSmallUrl": "/download/cover/480x270_jpg/{setId}",
    "noterName": "string",
    "difficulty": { "casual": 5, "normal": 8, "hard": 11, "mega": 14, "giga": 15 },
    "ranked": false,
    "published": true,
    "charts": [
      { "id": "ObjectId", "difficulty": "hard", "constant": 10.5 }
    ],
    "isOwned": true
  }
]
```

#### `GET /api/v1/chartsets/:id`

获取单个歌曲集详情。

#### `GET /api/v1/charts/:id`

获取单个谱面详情。

**响应体：**
```json
{
  "id": "ObjectId",
  "setId": "ObjectId",
  "difficulty": "hard",
  "constant": 10.5,
  "chartUrl": "/download/chart/encoded/{chartId}",
  "musicUrl": "/download/music/encoded/{setId}",
  "playCount": 1234,
  "passCount": 987
}
```

#### `GET /api/v1/charts?setId=:id`

获取指定歌曲集下的所有谱面。

---

### 3.4 成绩模块

#### `POST /api/v1/scores`

提交游玩成绩。

**请求体：**
```json
{
  "chartId": "ObjectId",
  "perfect": 1099,
  "good": 7,
  "miss": 10,
  "maxCombo": 291,
  "accuracy": 97.85,
  "score": 930308,
  "mods": { "mirror": false, "bleed": false, "auto": false }
}
```

**响应体：**
```json
{
  "id": "ObjectId",
  "score": 930308,
  "accuracy": 97.85,
  "rating": "S",
  "rValueContribution": 12.5
}
```

#### `GET /api/v1/scores/me`

获取当前用户的成绩历史。

**查询参数：**
| 参数 | 类型 | 说明 |
|------|------|------|
| `chartId` | `string` | 按谱面过滤（可选） |

#### `GET /api/v1/leaderboard/:chartId`

获取指定谱面的排行榜。

**查询参数：**
| 参数 | 类型 | 说明 |
|------|------|------|
| `limit` | `int` | 默认 50 |

**响应体：**
```json
[
  {
    "id": "ObjectId",
    "user": { "id": "ObjectId", "username": "string", "avatarUrl": "string" },
    "chart": { "id": "ObjectId", "difficulty": "hard" },
    "perfect": 1099,
    "good": 7,
    "miss": 10,
    "maxCombo": 291,
    "accuracy": 97.85,
    "score": 930308,
    "rating": "S",
    "isFullCombo": false,
    "isAllPerfect": false,
    "mods": { "mirror": false, "bleed": false, "auto": false },
    "playedAt": 1716192000000
  }
]
```

#### `GET /api/v1/ranking/global`

获取全球 R 值排行。

**查询参数：**
| 参数 | 类型 | 说明 |
|------|------|------|
| `limit` | `int` | 默认 50 |
| `offset` | `int` | 默认 0 |

---

### 3.5 商店模块

#### `POST /api/v1/purchase/:setId`

购买歌曲集。

**响应体：**
```json
{
  "success": true,
  "message": "Purchase successful",
  "user": { ... }
}
```

---

### 3.6 资源下载

资源下载路由与 Dynamite 官方协议 100% 兼容：

| 路由 | 说明 |
|------|------|
| `GET /download/music/encoded/{setId}` | 完整音频（加密） |
| `GET /download/cover/encoded/{setId}` | 封面（加密） |
| `GET /download/preview/encoded/{setId}` | 预览音频（加密） |
| `GET /download/chart/encoded/{chartId}` | 谱面二进制（加密） |
| `GET /download/avatar/256x256_jpg/{userId}` | 用户头像 |
| `GET /download/cover/480x270_jpg/{setId}` | 商店列表封面 |

同时支持明文路径（无 `/encoded/` 段），优先返回本地文件系统（`.explode_data`）中的明文资源。

---

## 4. 数据模型

### 4.1 ChartSet（歌曲集）

```json
{
  "_id": "ObjectId",
  "musicName": "string",
  "musicComposer": "string",
  "introduction": "",
  "price": 0,
  "cover": { "type": "Buffer", "size": 123456 },
  "coverCompressed": { "type": "Buffer" },
  "noterName": "string",
  "noterUserId": "ObjectId",
  "difficulty": { "casual": 5, "normal": 8, "hard": 11, "mega": 14, "giga": 15, "tera": null },
  "fileName": "string",
  "ranked": false,
  "published": true
}
```

### 4.2 Chart（单谱面）

```json
{
  "_id": "ObjectId",
  "setId": "ObjectId",
  "difficulty": "hard",
  "constant": 10.5,
  "chart": { "type": "Buffer" },
  "playCount": 1234,
  "passCount": 987
}
```

### 4.3 Score（成绩）

```json
{
  "_id": "ObjectId",
  "userId": "ObjectId",
  "chartId": "ObjectId",
  "setId": "ObjectId",
  "perfect": 1099,
  "good": 7,
  "miss": 10,
  "maxCombo": 291,
  "accuracy": 97.85,
  "score": 930308,
  "rating": "S",
  "isFullCombo": false,
  "isAllPerfect": false,
  "mods": { "mirror": false, "bleed": false, "auto": false },
  "playedAt": 1716192000000
}
```

---

## 5. 转服码支持

重构项目支持社区标准的**转服码**（Base64 JSON），用于动态切换服务器。

**JSON 格式：**
```json
{
  "ServerId": "MyRebuild",
  "ServerGraphqlBackend": "http://localhost:10443/graphql",
  "ServerResourceDownloadBase": "http://localhost:10443",
  "ServerPhotonMasterAddress": null,
  "ServerRESTBase": "http://localhost:10443/api/v1"
}
```

**编码流程：**
1. JSON → UTF-8 字节
2. 标准 Base64 编码（无换行）
3. 通过二维码 / 剪贴板 / 手动输入导入客户端

---

## 6. 错误响应

所有错误统一返回以下格式：

```json
{
  "error": "ERROR_CODE",
  "message": "Human-readable description"
}
```

**常见错误码：**

| 状态码 | error | 说明 |
|--------|-------|------|
| 400 | `BAD_REQUEST` | 请求参数错误 |
| 401 | `UNAUTHORIZED` | Token 缺失或无效 |
| 403 | `FORBIDDEN` | 权限不足（如未购买曲目） |
| 404 | `NOT_FOUND` | 资源不存在 |
| 409 | `CONFLICT` | 数据冲突（如用户名已存在） |
| 500 | `INTERNAL_ERROR` | 服务器内部错误 |

---

## 7. 本地开发默认配置

```json
{
  "ServerId": "LocalDev",
  "ServerGraphqlBackend": "http://127.0.0.1:8080/graphql",
  "ServerResourceDownloadBase": "http://127.0.0.1:8080",
  "ServerRESTBase": "http://127.0.0.1:8080/api/v1"
}
```

---

*文档版本: v1.0 | 生成日期: 2026-05-20*
