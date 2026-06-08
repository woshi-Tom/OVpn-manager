# 系统架构

## 整体架构

VPN Manager 采用分层架构设计：

```
┌─────────────────────────────────────────┐
│           Web 管理界面                   │
│         (Python Flask)                  │
└─────────────────┬───────────────────────┘
                  │ HTTP / Socket
┌─────────────────▼───────────────────────┐
│          Core 引擎                       │
│            (C 语言)                      │
└─────────────────┬───────────────────────┘
                  │ OpenVPN
┌─────────────────▼───────────────────────┐
│         OpenVPN 服务器                   │
└─────────────────────────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│         PostgreSQL 数据库                 │
└─────────────────────────────────────────┘
```

## 模块说明

### 1. Core (C 语言引擎)

核心管理模块，负责：
- OpenVPN 进程管理（启动/停止/监控）
- 证书生成（CA、服务器证书、客户端证书）
- 配置生成
- 会话管理

主要模块：

| 模块 | 文件 | 功能 |
|------|------|------|
| main | main.c | 主入口 |
| socket_server | socket_server.c | Web 通信 |
| openvpn | openvpn.c | VPN 进程管理 |
| monitor | monitor.c | 会话监控 |
| cert_utils | cert_utils.c | 证书工具 |
| ca_handler | ca_handler.c | CA 管理 |
| client_handler | client_handler.c | 客户端管理 |
| database | database.c | 数据库操作 |
| logger | logger.c | 日志管理 |
| config | config.c | 配置解析 |

### 2. Web (Python Flask)

管理界面模块，提供：
- 用户认证
- 配置管理
- 客户端管理
- 实时监控面板
- 系统日志查看

主要模块：

| 模块 | 文件 | 功能 |
|------|------|------|
| app | app.py | Flask 主应用 |
| routes/index | index.py | 首页/Dashboard |
| routes/configs | configs.py | 配置管理 |
| routes/clients | clients.py | 客户端管理 |
| routes/sessions | sessions.py | 会话管理 |
| routes/auth | auth.py | 认证 |
| core_client | core_client.py | Core 通信 |

### 3. PostgreSQL 数据库

数据存储层，表结构：

| 表名 | 说明 |
|------|------|
| vpn_users | 用户信息 |
| vpn_config | VPN 配置 |
| vpn_config_tun | TUN 模式配置 |
| vpn_config_tap | TAP 模式配置 |
| vpn_client_profiles | 客户端档案 |
| vpn_sessions | 会话记录 |
| vpn_admins | 管理员 |
| vpn_ca | CA 证书 |
| vpn_revoked_certs | 证书撤销列表 |
| system_logs | 系统日志 |

## 通信机制

### Web 与 Core 通信

通过 Unix Socket 进行进程间通信：

```python
# Web 端
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
client.connect("/var/run/vpn-manager/core.sock")
client.send(json.dumps(request).encode())
```

请求格式：
```json
{
  "action": "apply_config",
  "params": {
    "config_id": 1
  }
}
```

响应格式：
```json
{
  "status": "ok",
  "data": "..."
}
```

### Core 与 OpenVPN 通信

通过 Management Socket 进行通信：

```bash
# 连接 Management Socket
telnet /var/run/openvpn/management.sock

# 查看在线客户端
client-list

# 踢出客户端
kill user_name
```

## 安全设计

### 1. 数据库权限分离

| 用户 | 权限 | 用途 |
|------|------|------|
| vpn_core | ALL | Core 使用 |
| vpn_web | SELECT + 部分 INSERT/UPDATE | Web 使用 |

### 2. 私钥加密

- 服务端私钥：使用 encryption_key 加密存储
- 客户端私钥：使用用户密码加密存储

### 3. 日志脱敏

- debug 模式：显示完整信息
- 非 debug 模式：敏感信息（密码、IP）脱敏

## 目录结构

```
/etc/vpn-manager/          # 配置目录
├── core.yaml              # Core 配置
├── web.yaml               # Web 配置
├── schema.sql             # 数据库初始化
└── web-db-auth           # Web 数据库认证

/var/log/vpn-manager/      # 日志目录
├── core.log
└── web.log

/var/lib/vpn-manager/pki/  # 证书目录
└── ca.key                 # CA 私钥
```
