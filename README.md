# VPN Manager / VPN 管理平台

> 基于 OpenVPN 的轻量级自托管 VPN 管理平台，提供 Web 管理界面，支持客户端证书管理、实时会话监控、多配置管理。
>
> A lightweight, self-hosted VPN management platform built on top of OpenVPN, featuring a Web UI for client certificate management, real-time session monitoring, and multi-configuration support.

**[English](#english) | 中文**

---

## ⚠️ 安全提示

> **部署后请立即修改默认管理员密码！**
> 默认账号：`admin` / `admin123`

---

## 功能特性

| 功能 | 说明 |
|------|------|
| 🖥️ Web 管理界面 | 直观的仪表盘，无需命令行操作 |
| 📜 客户端证书管理 | 一键创建、下载、撤销客户端证书 |
| 🔐 CA 证书管理 | 生成和管理 CA 证书 |
| ✍️ 服务器证书签发 | 通过 Web 界面签发服务器证书 |
| 👥 实时会话监控 | 查看在线用户、流量统计、连接历史 |
| ⚙️ 多配置管理 | 同时运行多个 VPN 配置（TUN/TAP） |
| 🌐 TUN & TAP 模式 | 支持路由模式（TUN）和桥接模式（TAP） |
| 📊 系统日志 | 操作审计，可配置日志级别 |

## 系统架构

```
┌─────────────────┐   Unix Socket (JSON)   ┌──────────────────┐
│    Web UI       │ ◄────────────────────► │   Core Engine    │
│  (Python/Flask) │                        │      (C)         │
└────────┬────────┘                        └────────┬─────────┘
         │                                          │
         ▼                                          ▼
┌─────────────────┐                        ┌──────────────────┐
│   PostgreSQL    │ ◄─────────────────────►│     OpenVPN      │
│    数据库       │                        │   (子进程管理)    │
└─────────────────┘                        └──────────────────┘
```

- **Core (C 语言)** — 以 root 权限运行，管理 OpenVPN 进程、处理 PKI 操作、通过 management socket 监控会话
- **Web (Python/Flask)** — 以非特权用户运行，提供 HTTP 界面（端口 5000），通过 Unix Socket 与 Core 通信
- **PostgreSQL** — 存储所有配置、用户数据、加密证书、会话日志

## 技术栈

| 层级 | 技术 |
|------|------|
| 核心引擎 | C (GCC), OpenSSL, libpq, libyaml, cJSON, pthreads |
| Web 界面 | Python 3, Flask, Jinja2, psycopg2 |
| 数据库 | PostgreSQL 14+ |
| VPN 服务 | OpenVPN（子进程管理） |
| 进程通信 | Unix Domain Socket + JSON 协议 |
| 加密 | RSA 密钥生成、X.509 证书、AES-256-CBC 私钥加密 |
| 部署 | systemd 服务、Bash 安装脚本 |

---

## 环境要求

### 硬件要求

| 项目 | 最低要求 |
|------|---------|
| CPU | 1 核 |
| 内存 | 2 GB |
| 磁盘 | 10 GB |

### 软件要求

- **操作系统**: Ubuntu 20.04+ / Debian 11+
- **PostgreSQL**: 14+
- **Python**: 3.8+
- **OpenVPN**: 2.5+
- **GCC + Make**: 编译 Core 需要

---

## 快速开始

### 方式一：一键安装（推荐）

```bash
# 1. 克隆仓库
git clone https://github.com/woshi-Tom/OVpn-manager.git
cd OVpn-manager

# 2. 准备 PostgreSQL 数据库
sudo -u postgres psql
CREATE USER vpn_core WITH PASSWORD 'your_password';
CREATE DATABASE vpn_db OWNER vpn_core;
\q

# 3. 运行安装脚本（需要 root 权限）
sudo chmod +x packaging/scripts/install.sh
sudo ./packaging/scripts/install.sh
```

安装脚本会交互式询问以下配置：
- 数据库连接信息（主机、端口、数据库名、密码）
- Flask session 密钥（可自动生成）
- 私钥加密密钥（可自动生成）
- 日志级别

### 方式二：手动构建

如果需要手动编译和部署，请按以下步骤操作：

#### 第一步：安装系统依赖

```bash
sudo apt-get update
sudo apt-get install -y \
    postgresql-client \
    openvpn \
    openssl \
    gcc \
    make \
    libpq-dev \
    libyaml-dev \
    libcjson-dev \
    libssl-dev \
    python3 \
    python3-pip \
    python3-venv
```

#### 第二步：编译 Core 引擎

```bash
cd master

# 编译
make clean
make

# 安装到系统路径
sudo cp vpn_core /usr/local/bin/vpn-core
sudo chmod +x /usr/local/bin/vpn-core
```

编译产物：`master/vpn_core`

依赖库：`libpq`（PostgreSQL 客户端）、`libyaml`、`cJSON`、`OpenSSL`、`pthreads`

如果编译报错找不到头文件，确认已安装 `-dev` 包：

```bash
# Ubuntu/Debian
sudo apt-get install libpq-dev libyaml-dev libcjson-dev libssl-dev
```

#### 第三步：部署 Web 应用

```bash
# 创建部署目录
sudo mkdir -p /workspace/web

# 复制 Web 文件
sudo cp -r web/* /workspace/web/

# 创建 Python 虚拟环境
sudo python3 -m venv /opt/vpn-web-venv
source /opt/vpn-web-venv/bin/activate

# 安装 Python 依赖
pip install --upgrade pip
pip install -r /workspace/web/requirements.txt

deactivate
```

#### 第四步：配置文件

```bash
# 创建配置目录
sudo mkdir -p /etc/vpn-manager

# 复制并编辑配置文件
sudo cp core.yaml.example /etc/vpn-manager/core.yaml
sudo cp web.yaml.example /etc/vpn-manager/web.yaml
sudo cp schema.sql /etc/vpn-manager/schema.sql

# 编辑配置（修改数据库连接、密钥等）
sudo nano /etc/vpn-manager/core.yaml
sudo nano /etc/vpn-manager/web.yaml

# 设置权限（配置文件包含敏感信息）
sudo chmod 640 /etc/vpn-manager/core.yaml
sudo chmod 640 /etc/vpn-manager/web.yaml
sudo chown root:root /etc/vpn-manager/core.yaml
sudo chown root:root /etc/vpn-manager/web.yaml
```

**配置文件说明：**

`/etc/vpn-manager/core.yaml` — Core 引擎配置：
```yaml
database:
  host: localhost          # 数据库主机
  port: 5432               # 数据库端口
  name: vpn_db             # 数据库名称
  user: vpn_core           # 数据库用户
  password: "your_password" # 数据库密码

encryption_key: "your-random-key"  # 私钥加密密钥（重要！丢失后无法解密证书）

log:
  level: info              # 日志级别: debug/info/warn/error
  file: /var/log/vpn-manager/core.log

openvpn:
  binary: /usr/sbin/openvpn  # OpenVPN 可执行文件路径
```

`/etc/vpn-manager/web.yaml` — Web 界面配置：
```yaml
flask:
  secret_key: "your-random-string"  # Flask session 密钥
  debug: false
  host: 0.0.0.0
  port: 5000               # Web 界面端口

socket:
  core_socket: /var/run/vpn-manager/core.sock  # Core 通信 socket
```

#### 第五步：配置 systemd 服务

```bash
# 复制服务文件
sudo cp packaging/services/vpn-core.service /etc/systemd/system/

# 复制并修改 Web 服务（替换路径）
sudo sed "s|__WEB_DIR__|/workspace/web|g" \
    packaging/services/vpn-web.service > /etc/systemd/system/vpn-web.service

# 重新加载 systemd
sudo systemctl daemon-reload
```

#### 第六步：安装管理脚本

```bash
sudo cp packaging/scripts/vpn-manager /usr/local/bin/
sudo chmod +x /usr/local/bin/vpn-manager
```

#### 第七步：初始化并启动

```bash
# 初始化数据库（创建表结构和默认管理员）
sudo vpn-manager init

# 启动服务
sudo vpn-manager start

# 设置开机自启
sudo vpn-manager enable
```

---

## 使用说明

### 管理命令

```bash
sudo vpn-manager start      # 启动所有服务
sudo vpn-manager stop       # 停止所有服务
sudo vpn-manager restart    # 重启所有服务
sudo vpn-manager status     # 查看服务状态
sudo vpn-manager logs       # 查看最近 50 条日志
sudo vpn-manager logs-f     # 实时查看日志（Ctrl+C 退出）
sudo vpn-manager init       # 初始化数据库（首次使用）
sudo vpn-manager enable     # 设置开机自启
sudo vpn-manager disable    # 取消开机自启
```

### 访问 Web 管理界面

浏览器访问：`http://<服务器IP>:5000`

默认管理员账号：
- 用户名：`admin`
- 密码：`admin123`

### 基本使用流程

```
1. 登录 Web 界面
   └─► 修改默认密码（强烈建议）

2. 生成 CA 证书
   └─► CA 管理 → 生成 CA 证书

3. 创建 VPN 配置
   └─► 配置管理 → 添加配置（选择 TUN/TAP 模式、端口、协议）

4. 签发服务器证书
   └─► CA 管理 → 签发服务器证书（关联到配置）

5. 启动 VPN 配置
   └─► 配置管理 → 启动

6. 创建客户端
   └─► 客户端管理 → 添加客户端（选择关联的配置）

7. 下载客户端配置
   └─► 客户端管理 → 下载配置（.ovpn 文件）

8. 连接 VPN
   └─► 使用 OpenVPN 客户端导入 .ovpn 文件连接
```

---

## 目录结构

### 项目源码

```
OVpn-manager/
├── master/                     # Core 引擎源码（C 语言）
│   ├── main.c                  # 入口点
│   ├── config.c                # YAML 配置解析
│   ├── database.c              # PostgreSQL 数据库操作
│   ├── socket_server.c         # Unix Socket IPC 服务
│   ├── openvpn.c               # OpenVPN 进程管理
│   ├── monitor.c               # 实时会话监控
│   ├── cert_utils.c            # OpenSSL 证书工具
│   ├── client_handler.c        # 客户端证书处理
│   ├── ca_handler.c            # CA 证书处理
│   ├── logger.c                # 日志（敏感数据脱敏）
│   ├── include/                # 头文件
│   └── makefile                # 构建脚本
├── web/                        # Web 管理界面（Python/Flask）
│   ├── app.py                  # Flask 应用入口
│   ├── config.py               # 配置加载
│   ├── core_client.py          # Core Socket 客户端
│   ├── db.py                   # 数据库连接
│   ├── routes/                 # 路由处理
│   │   ├── api.py              # REST API
│   │   ├── auth.py             # 登录认证
│   │   ├── clients.py          # 客户端管理
│   │   ├── configs.py          # 配置管理
│   │   ├── index.py            # 首页仪表盘
│   │   └── sessions.py         # 会话管理
│   ├── templates/              # HTML 模板
│   ├── static/                 # 静态资源
│   ├── utils/                  # 工具模块
│   └── requirements.txt        # Python 依赖
├── packaging/                  # 部署相关
│   ├── scripts/
│   │   ├── install.sh          # 一键安装脚本
│   │   ├── uninstall.sh        # 卸载脚本
│   │   └── vpn-manager         # CLI 管理工具
│   ├── services/               # systemd 服务文件
│   ├── config/                 # 配置模板
│   ├── ARCHITECTURE.md         # 架构文档
│   ├── INSTALL.md              # 安装指南
│   ├── CONFIG.md               # 配置说明
│   └── LICENSE                 # MIT 许可证
├── docs/                       # 文档
├── schema.sql                  # 数据库初始化脚本
├── permissions.sql             # 数据库权限设置
├── core.yaml.example           # Core 配置示例
├── web.yaml.example            # Web 配置示例
└── README.md                   # 本文件
```

### 运行时目录

| 目录 | 说明 |
|------|------|
| `/etc/vpn-manager/` | 配置文件目录 |
| `/var/log/vpn-manager/` | Core 和 Web 日志 |
| `/var/log/openvpn/` | OpenVPN 日志 |
| `/var/run/openvpn/` | PID 文件和 management socket |
| `/var/run/vpn-manager/` | Core IPC socket |
| `/var/lib/vpn-manager/pki/` | PKI 证书存储 |
| `/workspace/web/` | Web 应用部署目录 |

---

## 安全说明

- 私钥使用 **AES-256-CBC** 加密后存储到数据库
- Web 进程以非特权用户运行，数据库权限受限
- 日志自动脱敏（密码、私钥内容会被替换为 `[REDACTED]`）
- **部署后务必修改默认管理员密码**
- 生产环境建议使用 Nginx 反向代理并启用 HTTPS

---

## 常见问题

### 数据库连接失败

检查 `/etc/vpn-manager/core.yaml` 中的数据库配置：

```bash
cat /etc/vpn-manager/core.yaml
# 确认 host、port、user、password 正确
```

### Web 无法连接 Core

```bash
# 检查 Core 是否运行
sudo vpn-manager status

# 查看 Core 日志
sudo vpn-manager logs
```

### 端口被占用

修改 `/etc/vpn-manager/web.yaml` 中的端口：

```yaml
flask:
  port: 8080  # 改为其他端口
```

然后重启：`sudo vpn-manager restart`

### 编译报错找不到库

安装开发依赖：

```bash
sudo apt-get install libpq-dev libyaml-dev libcjson-dev libssl-dev
```

### 加密密钥丢失

> ⚠️ **`encryption_key` 丢失后，所有已加密的私钥将无法解密！**

请妥善备份 `/etc/vpn-manager/core.yaml` 中的 `encryption_key`。

---

## 卸载

```bash
sudo chmod +x packaging/scripts/uninstall.sh
sudo ./packaging/scripts/uninstall.sh
```

---

## 许可证

[MIT License](packaging/LICENSE)

---

## Contributing

欢迎提交 Issue 和 Pull Request。详见 [CONTRIBUTING.md](packaging/CONTRIBUTING.md)。
另外感谢`linux.do`论坛(https://linux.do/)的开源推广

---

<div id="english"></div>

## English

### Quick Start

```bash
# Clone
git clone https://github.com/woshi-Tom/OVpn-manager.git
cd OVpn-manager

# Prepare PostgreSQL
sudo -u postgres psql
CREATE USER vpn_core WITH PASSWORD 'your_password';
CREATE DATABASE vpn_db OWNER vpn_core;
\q

# Install
sudo chmod +x packaging/scripts/install.sh
sudo ./packaging/scripts/install.sh

# Initialize and start
sudo vpn-manager init
sudo vpn-manager start
```

Access the Web UI at `http://<server-ip>:5000` (default: `admin` / `admin123`).

### Manual Build

```bash
# Install dependencies
sudo apt-get install -y postgresql-client openvpn openssl gcc make \
    libpq-dev libyaml-dev libcjson-dev libssl-dev python3 python3-pip python3-venv

# Build Core
cd master && make && sudo cp vpn_core /usr/local/bin/vpn-core

# Deploy Web
sudo cp -r web/* /workspace/web/
sudo python3 -m venv /opt/vpn-web-venv
source /opt/vpn-web-venv/bin/activate
pip install -r /workspace/web/requirements.txt

# Configure
sudo cp core.yaml.example /etc/vpn-manager/core.yaml
sudo cp web.yaml.example /etc/vpn-manager/web.yaml
# Edit configs with your database credentials and keys

# Start
sudo vpn-manager init && sudo vpn-manager start
```

### Management Commands

| Command | Description |
|---------|-------------|
| `vpn-manager start` | Start all services |
| `vpn-manager stop` | Stop all services |
| `vpn-manager restart` | Restart all services |
| `vpn-manager status` | Show service status |
| `vpn-manager logs` | View recent logs |
| `vpn-manager init` | Initialize database |
| `vpn-manager enable` | Enable auto-start on boot |
