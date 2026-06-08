# 配置说明

## 配置文件位置

- Core 配置：`/etc/vpn-manager/core.yaml`
- Web 配置：`/etc/vpn-manager/web.yaml`

## Core 配置 (core.yaml)

```yaml
database:
  host: localhost        # 数据库主机
  port: 5432            # 数据库端口
  name: vpn_db          # 数据库名称
  user: vpn_core        # 数据库用户
  password: xxx         # 数据库密码

# 加密密钥
master_key_file: /etc/vpn-manager/master.key
encryption_key: "xxx"  # 用于加密私钥

# 日志配置
log:
  level: info           # debug/info/warn/error
  file: /var/log/vpn-manager/core.log

# OpenVPN 配置
openvpn:
  binary: /usr/sbin/openvpn
  config_dir: /etc/openvpn
  management_socket: /var/run/openvpn/management.sock

# PKI 配置
pki:
  ca_key: /var/lib/vpn-manager/pki/ca.key
```

### 配置项说明

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| database.host | 数据库主机地址 | localhost |
| database.port | 数据库端口 | 5432 |
| database.name | 数据库名称 | vpn_db |
| database.user | 数据库用户名 | vpn_core |
| database.password | 数据库密码 | - |
| master_key_file | 主密钥文件路径 | /etc/vpn-manager/master.key |
| encryption_key | 私钥加密密钥 | - |
| log.level | 日志等级 | info |
| log.file | 日志文件路径 | /var/log/vpn-manager/core.log |
| openvpn.binary | OpenVPN 可执行文件路径 | /usr/sbin/openvpn |
| openvpn.config_dir | OpenVPN 配置目录 | /etc/openvpn |
| openvpn.management_socket | Management Socket 路径 | /var/run/openvpn/management.sock |
| pki.ca_key | CA 私钥存储路径 | /var/lib/vpn-manager/pki/ca.key |

## Web 配置 (web.yaml)

```yaml
# Flask 配置
flask:
  secret_key: xxx       # Session 加密密钥
  debug: false          # 调试模式
  host: 0.0.0.0       # 监听地址
  port: 5000           # 监听端口

# Core Socket 通信配置
socket:
  core_socket: /var/run/vpn-manager/core.sock

# 日志配置
log:
  level: info           # debug/info/warn/error
  file: /var/log/vpn-manager/web.log
```

### 配置项说明

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| flask.secret_key | Session 加密密钥 | - |
| flask.debug | 调试模式 | false |
| flask.host | 监听地址 | 0.0.0.0 |
| flask.port | 监听端口 | 5000 |
| socket.core_socket | Core Socket 路径 | /var/run/vpn-manager/core.sock |
| log.level | 日志等级 | info |
| log.file | 日志文件路径 | /var/log/vpn-manager/web.log |

## 修改配置后生效

修改配置后需要重启服务：

```bash
vpn-manager restart
```

## 安全建议

1. **修改默认密码**：首次部署后修改 admin 默认密码
2. **使用强密钥**：使用随机生成的 encryption_key
3. **限制端口**：只开放必要端口（5000、1194）
4. **配置防火墙**：使用 ufw 或 iptables 限制访问
