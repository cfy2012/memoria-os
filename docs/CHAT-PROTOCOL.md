# 畅聊 CHAT 协议 v2（掌机端 ↔ changliao 服务器）

> 版本：v2 · 2026-10-06 · 作者：豆包-2 · 服务器实现：`changliao/server/api.php`（已落盘）
> 用途：chat.bas v3 图形化客户端（GUI 落地后写）与服务器端的对接依据；GLM 加 GUI 原语时按本协议设计列表/输入控件。

---

## 1. 概览

| 项 | v1（旧） | v2（本协议） |
|---|---|---|
| 认证 | 每次请求带 `u&p`（密码明文进 URL） | login 返回 **token**，后续请求带 `u&t`；密码只在登录那一次（POST body） |
| 发言 | GET `m=` 明文进 URL | **POST body** = 消息内容 |
| 房间 | 客户端传 `room=`，服务器无视（全在一个池子） | **服务器按 room 隔离**（cl_msgs.room 列） |
| 房间列表 | 无 | `?a=rooms` |
| 在线名单 | 无 | `?a=online`（get/任意认证请求 = 心跳，60s 在线窗口） |
| 分页 | `after` 增量（全房间） | `after` 增量 + **按房间**，LIMIT $max_fetch_rows |
| 语音 | POST body=WAV，无房间 | 同 + 按房间 |

所有响应 `Content-Type: text/plain; charset=utf-8`，行分隔，字段用 `|` 分隔（服务器滤掉内容里的 `|`）。

## 2. 认证（会话令牌）

- `POST ?a=login&u=<名>`，body = 密码 → `ok <token>`（token = 32 位 hex）
- 之后所有认证接口：`?a=send&u=<名>&t=<token>&room=<房>`（密码不再出现）
- token 存服务器 cl_users.token 列；重新 login 会换新 token（旧 token 失效）
- 兼容：老客户端继续用 `u&p`（GET/POST 密码）也能认证

## 3. 接口

### ping（部署验活）
```
GET ?a=ping → pong changliao v2
```

### reg（注册，POST body=密码）
```
POST ?a=reg&u=<名>   body=密码
→ ok / err taken（用户名被占）/ err bad（用户名或密码不合规）
```
用户名规则：`[a-zA-Z0-9_]{2,20}`；密码 4~32 位。

### login（登录，POST body=密码）
```
POST ?a=login&u=<名>  body=密码
→ ok <token> / err
```

### send（发文字，POST body=内容）
```
POST ?a=send&u=<名>&t=<token>&room=<房>  body=消息内容（≤200B）
→ ok <mid> / err auth / err bad（空）/ err long（超长）
```

### get（拉消息，轮询 + 心跳）
```
GET ?a=get&u=<名>&t=<token>&room=<房>&after=<mid>
→ 每行：mid|sender|mtype|body（mtype 0=文 1=语音）/ none
```
- `after` 增量：只取 mid > after，服务器按 mid ASC，最多 $max_fetch_rows 行
- 语音消息 body = 语音文件 URL（`data/v<mid>.wav`）
- 每次认证请求都会刷新 last_active（即轮询即心跳，60s 内算在线）

### voice（发语音，POST body = WAV 原始字节）
```
POST ?a=voice&u=<名>&t=<token>&room=<房>  body=WAV
→ ok <url> / err auth / err bad（太小/非 RIFF）/ err big（>512KB）/ err io
```

### rooms（房间列表）
```
GET ?a=rooms&u=<名>&t=<token>
→ 每行一个房间名 / none
```

### online（在线名单，本房间）
```
GET ?a=online&u=<名>&t=<token>&room=<房>
→ 每行一个用户名 / none
```
口径：最近 60s 有动作（登录/发言/轮询）的用户，且在本房间发过言。

## 4. 消息格式（get 返回）

```
<mid>|<sender>|<mtype>|<body>
```

| 字段 | 说明 |
|---|---|
| mid | 消息号（递增，做 after 游标） |
| sender | 发送者用户名 |
| mtype | 0 = 文字，body 即内容；1 = 语音，body 即 `data/v<mid>.wav` 相对 URL |
| body | 内容/语音 URL；服务器已滤 `|` 和换行 |

## 5. 客户端状态机建议（chat.bas v3）

```
启动 → ping（不通则图形化提示"服务器不通"）
  → 登录/注册（图形化表单，输入复用 IME）
  → 进房间（默认 lobby，可输房间名 / 调 rooms 列表）
  → 主循环：
      ├─ 定时 get(after=lm) → 新消息入列表，滚动显示，lm 更新
      ├─ [发言] → 输入框 → POST send → 立即 get 刷新
      ├─ [听语音] → httpdl 语音 URL → play
      ├─ [发语音] → record 5s → POST voice（httpup）
      ├─ [在线] → GET online → 状态栏显示在线人数
      └─ [退出] → end
```

- 消息列表本地保留最近 ~30 条（滚动窗口），防字符串背包爆掉
- 新消息到达：若自己在滚动底部则自动滚，否则状态栏提示"N 条新消息"

## 6. GUI 原语需求（给 GLM，落地顺序建议）

| 原语 | 用途 |
|---|---|
| 面板/窗口 `win x,y,w,h,标题` | 消息区、输入区、状态栏分区 |
| 文本列表 `list x,y,w,h` | 消息滚动列表（自动换行截断、行高） |
| 输入框（复用 input/IME） | 房间名、发言内容、登录表单 |
| 按钮/菜单 `btn x,y,w,h,文字` | [1]发言 [2]刷新 [3]听 [4]发语音 [0]退出 |
| 局部刷新 | 只重绘变化区，不整屏闪 |

## 7. 部署说明

- `api.php` + `config.php` + `config.local.php`（真实凭据，不入仓库）三件套重新打 zip 上传虚拟主机
- 首次访问自动建表 + 自动 ALTER 补列（cl_msgs.room / cl_users.last_active / cl_users.token），无需手动 SQL
- 上传后 `?a=ping` 应返回 `pong changliao v2`
- 本地无 PHP 环境，语法验证在部署机 `php -l api.php` 执行

---

*协议以 `changliao/server/api.php` 实现为准；客户端 v3 待 GUI 原语落地后按本协议编写。*
