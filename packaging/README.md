# VPN Manager

一个简洁易用的 VPN 管理系统，基于 Flask Web 前端和 C 语言核心引擎开发。

## 功能特性

- **VPN 配置管理**：支持 TUN/TAP 模式，可配置 OpenVPN 服务器参数
- **客户端证书管理**：一键生成客户端证书，支持下载配置文件
- **实时会话监控**：实时查看在线用户、流量统计
- **负载趋势图表**：显示最近 3/9/15 分钟在线用户趋势
- **系统日志**：记录关键操作日志

## 技术架构

- **Core** (C 语言)：VPN 管理引擎，处理 OpenVPN 进程、证书生成
- **Web** (Python Flask)：管理界面，提供 Web 管理功能
- **PostgreSQL**：数据存储

## 系统要求

- Ubuntu 20.04+ / Debian 11+
- PostgreSQL 14+
- OpenVPN
- Python 3.8+
- GCC, Make

## 快速开始

### 1. 安装依赖

```bash
apt-get update
apt-get install -y postgresql-client openvpn openssl gcc make libpq-dev python3 python3-pip python3-venv
```

### 2. 安装 VPN Manager

```bash
# 复制安装包到服务器
cp -r packaging /

# 运行安装脚本
cd /packaging
chmod +x scripts/*.sh
./scripts/install.sh
```

### 3. 初始化并启动

```bash
# 初始化数据库
vpn-manager init

# 启动服务
vpn-manager start

# 设置开机自启
vpn-manager enable
```

### 4. 访问管理界面

浏览器访问：`http://<服务器IP>:5000`

默认管理员账号：`admin` / `admin123`

## 管理命令

| 命令 | 说明 |
|------|------|
| `vpn-manager start` | 启动服务 |
| `vpn-manager stop` | 停止服务 |
| `vpn-manager restart` | 重启服务 |
| `vpn-manager status` | 查看状态 |
| `vpn-manager logs` | 查看日志 |
| `vpn-manager enable` | 开机自启 |

## 目录结构

```
packaging/
├── scripts/          # 管理脚本
│   ├── vpn-manager
│   ├── install.sh
│   └── uninstall.sh
├── services/        # systemd 服务
└── config/          # 配置模板
```

## 截图

Dashboard 界面展示：
- 在线用户数量
- 负载趋势图（3/9/15分钟）
- 最近会话记录

## 安全说明

- 客户端私钥使用用户密码加密存储
- 服务端私钥使用系统密钥加密存储
- Web 数据库用户仅授予必要权限

## 开源协议

MIT License

## 联系方式

如有问题，请提交 Issue。
