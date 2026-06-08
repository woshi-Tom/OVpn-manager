# =====================================================
# VPN Manager - Web 配置文件模板
# =====================================================
# 请将文件复制到 /etc/vpn-manager/web.yaml 并修改配置

# 数据库配置（Web 使用 vpn_web 用户，密码由 Core 自动生成）
# 注意：Web 不需要在此配置密码，密码从 /etc/vpn-manager/web-db-auth 自动读取

# Flask 配置
flask:
  secret_key: __FLASK_SECRET_KEY__
  debug: false
  host: 0.0.0.0
  port: 5000

# Core Socket 通信配置
socket:
  core_socket: /var/run/vpn-manager/core.sock

# 日志配置
log:
  level: __LOG_LEVEL__
  file: /var/log/vpn-manager/web.log
