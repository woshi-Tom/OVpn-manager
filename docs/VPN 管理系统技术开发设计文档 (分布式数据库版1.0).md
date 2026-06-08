**版本**：8.2  
**核心变更**：

- 支持远程 PostgreSQL 数据库，通信安全加固。
    
- 细化核心与 Web 交互协议，完善 Unix Socket 接口。
    
- 所有私钥加密存储，公钥证书明文存储。
    
- 新增客户端配置文件自动生成功能。
    
- 优化数据库表结构与初始化流程。
    

---

## 一、系统架构与技术选型

### 1.1 总体架构

采用“双进程 + 远程数据库”模式：

- **核心引擎**（C语言，root运行）：证书签发、OpenVPN进程管理、网桥/TUN设备操作、实时状态监控。
    
- **Web服务**（Python + Flask）：提供管理界面、处理HTTP请求、展示实时数据。
    
- **数据库**（PostgreSQL）：独立服务器，存储所有配置、证书（私钥加密）、客户端档案、会话日志。
    

### 1.2 进程间通信（Unix Socket）

Web服务与核心引擎通过 **Unix Domain Socket** 通信，接口采用 **JSON 格式** 文本行。  
Socket 路径：`/var/run/vpn-manager/core.sock`，权限 `660`，属主 `root:www-data`。

#### 接口指令集（完整版）

| 指令类型                     | 方向         | 参数示例                                                                                                 | 说明                            |
| ------------------------ | ---------- | ---------------------------------------------------------------------------------------------------- | ----------------------------- |
| `apply_config`           | Web → Core | `{"action": "apply_config", "config_id": 1}`                                                         | 应用指定ID的配置                     |
| `start_vpn`              | Web → Core | `{"action": "start_vpn"}`                                                                            | 启动OpenVPN                     |
| `stop_vpn`               | Web → Core | `{"action": "stop_vpn"}`                                                                             | 停止OpenVPN                     |
| `status`                 | Web → Core | `{"action": "status"}`                                                                               | 获取当前运行状态                      |
| `restart`                | Web → Core | `{"action": "restart"}`                                                                              | 重启OpenVPN                     |
| `gen_client_cert`        | Web → Core | `{"action": "gen_client_cert", "user_id": 5, "password": "user_pwd"}`                                | 生成客户端证书并用用户密码加密后存储，返回未加密的配置文件 |
| `generate_client_config` | Web → Core | `{"action": "generate_client_config", "client_profile_id": 123, "password": "user_pwd"}`             | 生成客户端配置文件（需要密码解密私钥）           |
| `change_client_password` | Web → Core | `{"action": "change_client_password", "client_profile_id": 123, "old_pwd": "...", "new_pwd": "..."}` | 修改客户端证书私钥的加密密码                |
| `response`               | Core → Web | `{"status": "ok", "data": {...}}` 或 `{"status": "error", "message": "..."}`                          | 通用响应格式                        |

### 1.3 核心引擎与 OpenVPN 管理接口

- OpenVPN 启动参数：`--management /var/run/openvpn/management.sock unix`
    
- 核心引擎连接该Socket获取实时客户端状态，每3秒更新数据库会话表。
    

### 1.4 配置文件格式（YAML）

#### 核心引擎配置 `/etc/vpn-manager/core.yaml`


```yaml
# VPN 核心引擎配置文件
# 路径：/etc/vpn-manager/core.yaml

database:
  host: 192.168.1.100          # PostgreSQL 服务器 IP
  port: 5432                    # 端口
  name: vpn_db                  # 数据库名
  user: vpn_core                 # 数据库用户
  password: "EncryptedBase64String"   # 加密后的密码（由安装脚本生成）

master_key_file: /etc/vpn-manager/master.key   # 主密钥文件路径

log:
  level: info                    # 日志级别：debug, info, warn, error
  file: /var/log/vpn-manager/core.log

openvpn:
  binary: /usr/sbin/openvpn       # OpenVPN 可执行文件路径
  config_dir: /etc/openvpn         # OpenVPN 配置文件目录
  management_socket: /var/run/openvpn/management.sock  # 管理接口 socket

pki:
  ca_key: /var/lib/vpn-manager/pki/ca.key   # CA 私钥文件路径
```


#### Web服务配置 `/etc/vpn-manager/web.yaml`

yaml

database:
  host: 192.168.1.100
  port: 5432
  name: vpn_db
  user: vpn_web
  password: EncryptedBase64String
flask:
  secret_key: "随机字符串"
  debug: false
socket:
  core_socket: /var/run/vpn-manager/core.sock
log:
  file: /var/log/vpn-manager/web.log

---

## 二、数据库详细设计

### 2.1 数据库与用户

- 数据库名：`vpn_db`
    
- 用户：
    
    - `vpn_core`：所有表读写权限，可执行DDL（初始化时）。
        
    - `vpn_web`：仅对 `vpn_config`、`vpn_client_profiles`、`vpn_users`、`vpn_sessions` 拥有SELECT权限，对 `system_logs` 可INSERT，其他表不可写。
        
- 连接方式：用户名+密码：端口，强制SSL（`sslmode=require`）。
    

### 2.2 证书加密策略

|证书/私钥|存储位置|加密方式|说明|
|---|---|---|---|
|CA证书（公钥）|`vpn_config.ca_cert`（TEXT）|明文|全局唯一，用于客户端验证服务器|
|CA私钥|文件系统 `/var/lib/vpn-manager/pki/ca.key`|明文文件，权限600|仅核心引擎可读，用于签发客户端证书|
|服务端证书（公钥）|`vpn_config.server_cert`（TEXT）|明文||
|服务端私钥|`vpn_config.server_key`（TEXT）|AES-256-GCM 加密，密钥来自 master.key|加密后存入数据库|
|客户端证书（公钥）|`vpn_client_profiles.client_cert`（TEXT）|明文||
|客户端私钥|`vpn_client_profiles.client_key`（TEXT）|AES-256-GCM 加密，密钥为用户提供的密码|每个客户端独立密码，加密后存入数据库|

- **主密钥**：首次安装时随机生成，保存于 `/etc/vpn-manager/master.key`（权限600），仅核心引擎可读。用于加密服务端私钥及其他需要核心自动解密的敏感数据。
    
- **用户密码**：Web界面创建客户端时由管理员输入，用于加密该客户端的私钥；下载配置文件或修改密码时需提供。
    

### 2.3 表结构（含约束与注释）

#### 用户表 `vpn_users`

|字段|类型|约束|说明|
|---|---|---|---|
|id|SERIAL|PRIMARY KEY||
|username|VARCHAR(64)|UNIQUE NOT NULL|OpenVPN认证用户名|
|email|VARCHAR(128)|||
|created_at|TIMESTAMP|DEFAULT NOW()||
|disabled|BOOLEAN|DEFAULT FALSE||
|description|TEXT|||

#### 主配置表 `vpn_config`

|字段|类型|约束|说明|
|---|---|---|---|
|id|SERIAL|PRIMARY KEY||
|config_name|VARCHAR(64)|NOT NULL||
|mode|VARCHAR(3)|CHECK (mode IN ('tun','tap')) NOT NULL||
|proto|VARCHAR(10)|DEFAULT 'udp'||
|port|INTEGER|CHECK (port BETWEEN 1 AND 65535) NOT NULL||
|ca_cert|TEXT|NOT NULL|CA证书（PEM，明文）|
|server_cert|TEXT|NOT NULL|服务端证书（PEM，明文）|
|server_key|TEXT|NOT NULL|服务端私钥（PEM，加密后存储）|
|created_at|TIMESTAMP|DEFAULT NOW()||
|updated_at|TIMESTAMP|DEFAULT NOW()||

#### TUN模式扩展表 `vpn_config_tun`

| 字段                    | 类型      | 约束                                                      | 说明                                                                                                |
| --------------------- | ------- | ------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| config_id             | INTEGER | PRIMARY KEY REFERENCES vpn_config(id) ON DELETE CASCADE |                                                                                                   |
| server_ip             | INET    | NOT NULL                                                |                                                                                                   |
| subnet_mask           | CIDR    | NOT NULL                                                |                                                                                                   |
| push_dns              | INET[]  |                                                         |                                                                                                   |
| enable_nat            | BOOLEAN | DEFAULT TRUE                                            | 服务端是否开启 iptables NAT 规则                                                                           |
| push_redirect_gateway | BOOLEAN |                                                         | 是否推送 `redirect-gateway def1`，让客户端所有流量通过 VPN（默认 FALSE）                                             |
| push_routes           | TEXT[]  |                                                         | 需要推送的附加路由数组，每个元素为 CIDR 格式，例如 `192.168.1.0/24`；程序生成配置时转换为 `push "route 192.168.1.0 255.255.255.0"` |

#### TAP模式扩展表 `vpn_config_tap`

|字段|类型|约束|说明|
|---|---|---|---|
|config_id|INTEGER|PRIMARY KEY REFERENCES vpn_config(id) ON DELETE CASCADE||
|bridge_name|VARCHAR(15)|NOT NULL||
|physical_if|VARCHAR(15)|NOT NULL||
|dhcp_mode|VARCHAR(10)|CHECK (dhcp_mode IN ('server','relay','none'))||

#### 客户端配置档案表 `vpn_client_profiles`

|字段|类型|约束|说明|
|---|---|---|---|
|id|SERIAL|PRIMARY KEY||
|user_id|INTEGER|REFERENCES vpn_users(id) ON DELETE CASCADE||
|config_id|INTEGER|REFERENCES vpn_config(id) ON DELETE CASCADE||
|assigned_ip|INET||静态IP|
|use_static_ip|BOOLEAN|DEFAULT FALSE||
|client_cert|TEXT|NOT NULL|客户端证书（PEM，明文）|
|client_key|TEXT|NOT NULL|客户端私钥（PEM，用用户密码加密）|
|created_at|TIMESTAMP|DEFAULT NOW()||

#### 会话表 `vpn_sessions`

| 字段                | 类型        | 约束                                                    | 说明       |
| ----------------- | --------- | ----------------------------------------------------- | -------- |
| id                | BIGSERIAL | PRIMARY KEY                                           |          |
| client_profile_id | INTEGER   | REFERENCES vpn_client_profiles(id) ON DELETE SET NULL |          |
| user_id           | INTEGER   | REFERENCES vpn_users(id) ON DELETE SET NULL           |          |
| config_id         | INTEGER   | REFERENCES vpn_config(id) ON DELETE SET NULL          |          |
| virtual_ip        | INET      |                                                       |          |
| real_ip           | INET      |                                                       |          |
| connected_since   | TIMESTAMP | NOT NULL                                              |          |
| disconnected_at   | TIMESTAMP |                                                       | NULL表示在线 |
| bytes_sent        | BIGINT    | DEFAULT 0                                             |          |
| bytes_received    | BIGINT    | DEFAULT 0                                             |          |
| last_update       | TIMESTAMP | DEFAULT NOW()                                         |          |

#### 系统日志表 `system_logs`

|字段|类型|约束|说明|
|---|---|---|---|
|id|BIGSERIAL|PRIMARY KEY||
|timestamp|TIMESTAMP|DEFAULT NOW()||
|level|VARCHAR(10)|CHECK (level IN ('debug','info','warn','error'))||
|source|VARCHAR(20)||core/web|
|message|TEXT|||

### 2.4 索引建议

- `vpn_sessions` 表：`(disconnected_at)` 部分索引（仅在线会话），`(user_id, connected_since)`
    
- `vpn_client_profiles` 表：`(user_id, config_id)` 唯一索引
    
- `system_logs` 表：`(timestamp)` 索引
    

---

## 三、核心功能实现细节

### 3.1 OpenVPN 配置生成逻辑（服务端）

核心引擎收到 `apply_config` 指令后：

1. 查询 `vpn_config` 及对应子表，获取所有参数。
    
2. 使用 master.key 解密 `server_key`，得到明文私钥。
    
3. 生成 OpenVPN 配置文件（`/etc/openvpn/vpn-{config_id}.conf`），采用**内联证书**方式，避免额外文件：
    
    text
    
    port 1194
    proto udp
    dev tun
    <ca>
    {ca_cert}
    </ca>
    <cert>
    {server_cert}
    </cert>
    <key>
    {解密后的server_key}
    </key>
    dh /var/lib/vpn-manager/pki/dh.pem   # DH参数仍使用文件，可预生成
    server 10.8.0.0 255.255.255.0
    push "dhcp-option DNS 8.8.8.8"
    keepalive 10 120
    persist-key
    persist-tun
    status /var/log/openvpn/status.log
    log-append /var/log/openvpn/openvpn.log
    verb 3
    management /var/run/openvpn/management.sock unix
    
    对于TAP模式，类似生成网桥配置。
    
4. 配置文件权限设为 `600`，属主 `root`。
    
5. 停止当前OpenVPN进程（如有），启动新进程。
    
6. 更新 `vpn_config.updated_at`。
    

### 3.2 客户端证书签发与存储

Web请求 `gen_client_cert` 时：

1. 核心引擎使用 CA 私钥（文件）为指定用户签发客户端证书，同时生成私钥。
    
2. 使用参数 `password` 对客户端私钥进行 AES-256-GCM 加密。
    
3. 将客户端证书（明文）和加密后的私钥存入 `vpn_client_profiles` 表。
    
4. 同时，生成一份包含明文私钥的客户端配置文件（内联证书），直接通过响应返回给Web（内存传输），不落盘。
    
5. Web 将配置文件通过 HTTPS 推送给管理员下载。
    

### 3.3 客户端配置文件生成

Web请求 `generate_client_config` 时：

1. 核心引擎根据 `client_profile_id` 查询 `vpn_client_profiles` 和关联的 `vpn_config`。
    
2. 使用请求中的 `password` 解密 `client_key`（若密码错误则返回错误）。
    
3. 从 `vpn_config` 获取 `ca_cert` 和服务器地址信息（需额外字段？当前表无服务器地址，应在 `vpn_config` 增加 `remote` 字段或从配置名推断。建议在 `vpn_config` 增加 `remote` 字段存储服务器域名/IP，便于客户端配置。）  
    **补充字段**：在 `vpn_config` 表中增加 `remote` VARCHAR(255) NOT NULL，存储服务器地址。
    
4. 组装客户端配置文件（`.ovpn` 格式）：
    
    text
    
    client
    dev tun
    proto udp
    remote {vpn_config.remote} {vpn_config.port}
    resolv-retry infinite
    nobind
    persist-key
    persist-tun
    remote-cert-tls server
    verb 3
    <ca>
    {ca_cert}
    </ca>
    <cert>
    {client_cert}
    </cert>
    <key>
    {解密后的client_key}
    </key>
    
5. 返回配置文件内容（文本）给Web，Web提供下载。
    

### 3.4 修改客户端私钥密码

Web请求 `change_client_password` 时，核心引擎用 `old_pwd` 解密 `client_key`，再用 `new_pwd` 加密后更新数据库。

### 3.5 实时状态监控

- 核心引擎每3秒通过 Management Socket 获取在线客户端列表，与 `vpn_sessions` 比对，更新会话记录（新增、更新流量、标记离线）。
    

---

## 四、安全与权限控制

### 4.1 数据库访问控制

- PostgreSQL `pg_hba.conf` 配置强制SSL：
    
    text
    
    hostssl vpn_db vpn_core 192.168.1.0/24 md5
    hostssl vpn_db vpn_web  192.168.1.0/24 md5
    

### 4.2 本地Socket安全

- Socket 文件权限 `660`，属主 `root:www-data`，确保只有Web服务用户和root可访问。
    

### 4.3 敏感信息存储

- 主密钥文件 `/etc/vpn-manager/master.key` 权限 `600`，仅root可读。
    
- 数据库密码在配置文件中加密存储，解密密钥来自主密钥。
    
- CA私钥文件 `/var/lib/vpn-manager/pki/ca.key` 权限 `600`，仅核心引擎可读。
    
- 所有私钥在内存中使用后及时清零（使用 `explicit_bzero` 等安全函数）。
    

---

## 五、目录结构规划


```text
/etc/vpn-manager/
├── core.yaml          # 核心引擎配置
├── web.yaml           # Web服务配置
├── master.key         # 主密钥（仅root可读）
/var/lib/vpn-manager/
├── pki/               # 证书颁发机构目录
│   ├── ca.crt         # CA证书（冗余，也可从数据库读取）
│   ├── ca.key         # CA私钥（仅核心可读）
│   ├── dh.pem         # Diffie-Hellman参数
│   └── issued/        # 已签发客户端证书（可选备份）
├── ccd/               # 客户端配置目录（OpenVPN ccd）
└── tmp/               # 临时文件
/var/log/vpn-manager/
├── core.log
├── web.log
└── openvpn/           # OpenVPN自身日志
/var/run/vpn-manager/
├── core.sock          # 核心与Web通信Socket
└── openvpn/           # OpenVPN管理Socket
/usr/sbin/vpn-core     # 核心引擎二进制
/usr/share/vpn-manager/web/   # Flask Web应用
├── app.py
├── templates/
└── static/
```

```
core开发目录
core/
├── core.c          # 主程序
├── socket_server.c # Socket 服务端实现
├── socket_server.h
├── handler.c       # 指令处理函数
├── handler.h
├── logger.c        # 日志模块
├── logger.h
├── config.c        # 配置解析
├── config.h
├── Makefile
└── cJSON/          # 如果使用 cJSON 库，可以放在子目录
```
---

## 六、初始化与部署

### 6.1 首次安装流程

1. 安装 PostgreSQL，创建空数据库。
    
2. 在核心服务器运行安装脚本 `vpn-install`：
    
    - 生成主密钥 `master.key`。
        
    - 连接数据库（需提供PostgreSQL root密码），创建用户 `vpn_core`、`vpn_web` 并授权。
        
    - 执行 `schema.sql` 创建表。
        
    - 生成 CA 证书和私钥（`ca.crt`, `ca.key`），将 `ca.crt` 存入 `vpn_config` 的 `ca_cert` 字段（初始记录）。
        
    - 生成服务端证书和私钥，用主密钥加密私钥后存入 `vpn_config`。
        
    - 生成 DH 参数文件 `dh.pem`。
        
    - 写入配置文件 `core.yaml` 和 `web.yaml`（数据库密码加密后）。
        
    - 创建 systemd 服务并启动。
        

### 6.2 数据库初始化 SQL 脚本

提供完整 `schema.sql`，包含所有表、索引、外键。
```sql
-- =====================================================
-- VPN 管理系统数据库初始化脚本 (PostgreSQL)
-- 版本: 8.2
-- 说明: 执行前请先创建数据库和用户，并授予相应权限
-- 建议在 psql 中运行: \i schema.sql
-- =====================================================

-- 扩展: 支持 UUID 等（如果需要）
-- CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- ----------------------------
-- 用户表 vpn_users
-- ----------------------------
CREATE TABLE vpn_users (
    id          SERIAL PRIMARY KEY,
    username    VARCHAR(64) NOT NULL UNIQUE,
    email       VARCHAR(128),
    created_at  TIMESTAMP NOT NULL DEFAULT NOW(),
    disabled    BOOLEAN NOT NULL DEFAULT FALSE,
    description TEXT
);

COMMENT ON TABLE vpn_users IS 'OpenVPN 认证用户';
COMMENT ON COLUMN vpn_users.username IS '用户名，用于客户端认证';
COMMENT ON COLUMN vpn_users.disabled IS '是否禁用，禁用后无法连接';

-- ----------------------------
-- 主配置表 vpn_config
-- ----------------------------
CREATE TABLE vpn_config (
    id           SERIAL PRIMARY KEY,
    config_name  VARCHAR(64) NOT NULL,
    mode         VARCHAR(3) NOT NULL CHECK (mode IN ('tun', 'tap')),
    proto        VARCHAR(10) NOT NULL DEFAULT 'udp',
    port         INTEGER NOT NULL CHECK (port BETWEEN 1 AND 65535),
    remote       VARCHAR(255) NOT NULL,   -- 服务器域名或IP，用于客户端配置
    ca_cert      TEXT NOT NULL,            -- CA 证书 (PEM, 明文)
    server_cert  TEXT NOT NULL,            -- 服务端证书 (PEM, 明文)
    server_key   TEXT NOT NULL,            -- 服务端私钥 (PEM, 加密后存储)
    created_at   TIMESTAMP NOT NULL DEFAULT NOW(),
    updated_at   TIMESTAMP NOT NULL DEFAULT NOW()
);

COMMENT ON TABLE vpn_config IS 'VPN 服务端配置主表';
COMMENT ON COLUMN vpn_config.remote IS '客户端连接时使用的服务器地址';
COMMENT ON COLUMN vpn_config.server_key IS '使用主密钥加密后的私钥';

-- ----------------------------
-- TUN 模式扩展表 vpn_config_tun
-- ----------------------------
CREATE TABLE vpn_config_tun (
    config_id    INTEGER PRIMARY KEY REFERENCES vpn_config(id) ON DELETE CASCADE,
    server_ip    INET NOT NULL,
    subnet_mask  CIDR NOT NULL,
    push_dns     INET[],
    push_redirect_gateway   BOOLEAN NOT NULL DEFAULT FALSE,
    push_routes TEXT[] DEFAULT '{}',
    enable_nat   BOOLEAN NOT NULL DEFAULT TRUE
);

COMMENT ON TABLE vpn_config_tun IS 'TUN 模式特有配置';
COMMENT ON COLUMN vpn_config_tun.server_ip IS '虚拟网段IP，如 10.8.0.0';
COMMENT ON COLUMN vpn_config_tun.subnet_mask IS '子网掩码，如 255.255.255.0 或 10.8.0.0/24';
COMMENT ON COLUMN vpn_config_tun.push_dns IS '要推送的DNS服务器列表';
COMMENT ON COLUMN vpn_config_tun.push_redirect_gateway IS '是否推送 redirect-gateway def1，让客户端所有流量通过 VPN（默认 FALSE）';
COMMENT ON COLUMN vpn_config_tun.push_routes IS '推送的附加路由数组，每个元素为 CIDR 格式';


-- ----------------------------
-- TAP 模式扩展表 vpn_config_tap
-- ----------------------------
CREATE TABLE vpn_config_tap (
    config_id    INTEGER PRIMARY KEY REFERENCES vpn_config(id) ON DELETE CASCADE,
    bridge_name  VARCHAR(15) NOT NULL,
    physical_if  VARCHAR(15) NOT NULL,
    dhcp_mode    VARCHAR(10) NOT NULL CHECK (dhcp_mode IN ('server', 'relay', 'none'))
);

COMMENT ON TABLE vpn_config_tap IS 'TAP 模式特有配置';
COMMENT ON COLUMN vpn_config_tap.bridge_name IS '网桥名称，如 vpnbr0';
COMMENT ON COLUMN vpn_config_tap.physical_if IS '物理网卡名，如 eth0';

-- ----------------------------
-- 客户端配置档案表 vpn_client_profiles
-- ----------------------------
CREATE TABLE vpn_client_profiles (
    id             SERIAL PRIMARY KEY,
    user_id        INTEGER REFERENCES vpn_users(id) ON DELETE CASCADE,
    config_id      INTEGER REFERENCES vpn_config(id) ON DELETE CASCADE,
    assigned_ip    INET,
    use_static_ip  BOOLEAN NOT NULL DEFAULT FALSE,
    client_cert    TEXT NOT NULL,   -- 客户端证书 (PEM, 明文)
    client_key     TEXT NOT NULL,   -- 客户端私钥 (PEM, 用用户密码加密)
    created_at     TIMESTAMP NOT NULL DEFAULT NOW(),
    UNIQUE (user_id, config_id)
);

COMMENT ON TABLE vpn_client_profiles IS '客户端配置档案，关联用户和配置';
COMMENT ON COLUMN vpn_client_profiles.client_key IS '使用用户提供的密码加密后的私钥';

-- ----------------------------
-- 会话表 vpn_sessions
-- ----------------------------
CREATE TABLE vpn_sessions (
    id                BIGSERIAL PRIMARY KEY,
    client_profile_id INTEGER REFERENCES vpn_client_profiles(id) ON DELETE SET NULL,
    user_id           INTEGER REFERENCES vpn_users(id) ON DELETE SET NULL,
    config_id         INTEGER REFERENCES vpn_config(id) ON DELETE SET NULL,
    virtual_ip        INET,
    real_ip           INET,
    connected_since   TIMESTAMP NOT NULL,
    disconnected_at   TIMESTAMP,   -- NULL 表示在线
    bytes_sent        BIGINT NOT NULL DEFAULT 0,
    bytes_received    BIGINT NOT NULL DEFAULT 0,
    last_update       TIMESTAMP NOT NULL DEFAULT NOW()
);

COMMENT ON TABLE vpn_sessions IS '客户端会话历史及实时在线记录';
COMMENT ON COLUMN vpn_sessions.disconnected_at IS '断开时间，NULL表示仍在在线';

-- 部分索引：只索引在线会话，提高查询性能
CREATE INDEX idx_vpn_sessions_online ON vpn_sessions (disconnected_at) WHERE disconnected_at IS NULL;

-- 普通索引
CREATE INDEX idx_vpn_sessions_user_id ON vpn_sessions (user_id);
CREATE INDEX idx_vpn_sessions_connected_since ON vpn_sessions (connected_since);

-- ----------------------------
-- 系统日志表 system_logs
-- ----------------------------
CREATE TABLE system_logs (
    id          BIGSERIAL PRIMARY KEY,
    timestamp   TIMESTAMP NOT NULL DEFAULT NOW(),
    level       VARCHAR(10) NOT NULL CHECK (level IN ('debug', 'info', 'warn', 'error')),
    source      VARCHAR(20) NOT NULL,   -- 'core' 或 'web'
    message     TEXT NOT NULL
);

CREATE INDEX idx_system_logs_timestamp ON system_logs (timestamp);
CREATE INDEX idx_system_logs_level ON system_logs (level);

-- ----------------------------
-- 可选：初始数据示例（根据需要启用）
-- ----------------------------
/*
-- 插入一个示例 VPN 配置（TUN 模式）
-- 注意：server_key 需要替换为实际加密后的私钥
INSERT INTO vpn_config (config_name, mode, proto, port, remote, ca_cert, server_cert, server_key)
VALUES ('Default TUN', 'tun', 'udp', 1194, 'vpn.example.com', 
'-----BEGIN CERTIFICATE-----
... CA证书内容 ...
-----END CERTIFICATE-----',
'-----BEGIN CERTIFICATE-----
... 服务端证书内容 ...
-----END CERTIFICATE-----',
'-----BEGIN ENCRYPTED PRIVATE KEY-----
... 加密后的服务端私钥 ...
-----END ENCRYPTED PRIVATE KEY-----');

INSERT INTO vpn_config_tun (config_id, server_ip, subnet_mask, push_dns)
VALUES (currval('vpn_config_id_seq'), '10.8.0.0', '10.8.0.0/24', ARRAY['8.8.8.8', '8.8.4.4']);

-- 插入一个示例用户
INSERT INTO vpn_users (username, email) VALUES ('testuser', 'test@example.com');

-- 插入一个客户端配置档案（client_key 需用用户密码加密）
INSERT INTO vpn_client_profiles (user_id, config_id, client_cert, client_key)
VALUES (1, 1,
'-----BEGIN CERTIFICATE-----
... 客户端证书 ...
-----END CERTIFICATE-----',
'-----BEGIN ENCRYPTED PRIVATE KEY-----
... 加密后的客户端私钥 ...
-----END ENCRYPTED PRIVATE KEY-----');
*/

-- ----------------------------
-- 完成
-- ----------------------------
```

### 6.3 错误处理

- 数据库连接失败：重试3次，间隔3秒，失败则退出（systemd重启）。
    
- OpenVPN进程崩溃：监控线程检测到后重启，最多3次，失败则标记故障并记录日志。
    
- 网络操作失败：尝试恢复，持续失败则停止VPN并报警。
    

### 6.4 性能

- Web服务使用 SQLAlchemy 连接池（`pool_size=5, max_overflow=10`）。
    
- 会话更新频率3秒，对数据库压力可控。
    

---

## 七、依赖环境

- 操作系统：Debian 13 (或更新)
    
- PostgreSQL：16.x
    
- OpenVPN：2.6.x
    
- Python：3.12+
    
- Python库：Flask 3.x, psycopg2-binary, PyYAML, cryptography, sqlalchemy
    
- 编译工具：gcc, make, libpq-dev

```
apt install libyaml-dev libpq-dev libcjson-dev 
```