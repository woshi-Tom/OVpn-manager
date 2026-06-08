# 安装指南

## 环境要求

### 硬件要求
- CPU: 1 核心及以上
- 内存: 2GB 及以上
- 磁盘: 10GB 及以上

### 软件要求
- 操作系统: Ubuntu 20.04+ / Debian 11+
- PostgreSQL: 14+
- Python: 3.8+
- OpenVPN
- GCC, Make

## 安装步骤

### 1. 准备数据库

创建 PostgreSQL 数据库和用户：

```bash
# 连接到 PostgreSQL
sudo -u postgres psql

# 创建数据库用户
CREATE USER vpn_core WITH PASSWORD 'your_password';

# 创建数据库
CREATE DATABASE vpn_db OWNER vpn_core;

# 退出
\q
```

### 2. 安装依赖

```bash
apt-get update
apt-get install -y \
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

### 3. 安装 VPN Manager

```bash
# 克隆仓库
git clone https://github.com/woshi-Tom/OVpn-manager.git
cd OVpn-manager

# 添加执行权限
chmod +x packaging/scripts/*.sh

# 运行安装脚本（需要 root 权限）
sudo ./packaging/scripts/install.sh
```

安装脚本会：
- 安装系统依赖
- 编译 Core
- 安装 Python 依赖
- 配置 systemd 服务
- 生成配置文件

### 4. 配置数据库连接

安装过程中会提示输入：
- 数据库主机（默认 localhost）
- 数据库端口（默认 5432）
- 数据库名称（默认 vpn_db）
- 数据库密码
- Flask secret_key
- 加密密钥
- 日志等级

### 5. 初始化并启动

```bash
# 初始化数据库（首次使用）
vpn-manager init

# 启动服务
vpn-manager start

# 设置开机自启
vpn-manager enable
```

### 6. 访问管理界面

浏览器访问：`http://<服务器IP>:5000`

默认管理员账号：
- 用户名：`admin`
- 密码：`admin123`

> ⚠️ **安全提示：首次登录后请立即修改默认密码！** 默认密码仅用于初始配置，不修改将存在安全风险。

## 目录说明

| 目录 | 说明 |
|------|------|
| `/etc/vpn-manager/` | 配置文件目录 |
| `/var/log/vpn-manager/` | 日志目录 |
| `/var/lib/vpn-manager/pki/` | 证书存储目录 |
| `/workspace/web/` | Web 应用目录 |

## 卸载

```bash
cd /packaging
chmod +x scripts/uninstall.sh
./scripts/uninstall.sh
```

## 常见问题

### 1. 数据库连接失败

检查 `core.yaml` 中的数据库配置是否正确：

```bash
cat /etc/vpn-manager/core.yaml
```

### 2. Web 无法连接 Core

检查 Core 是否正常运行：

```bash
vpn-manager status
```

查看 Core 日志：

```bash
vpn-manager logs
```

### 3. 端口被占用

默认 Web 端口 5000 被占用，可修改 `/etc/vpn-manager/web.yaml`：

```yaml
flask:
  port: 8080
```

修改后重启服务：

```bash
vpn-manager restart
```
