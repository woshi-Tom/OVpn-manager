# =====================================================
# VPN Manager - Core 配置文件模板
# =====================================================
# 请将文件复制到 /etc/vpn-manager/core.yaml 并修改配置

database:
  # 数据库连接信息（Core 使用 vpn_core 用户连接）
  host: __DB_HOST__
  port: __DB_PORT__
  name: __DB_NAME__
  user: vpn_core
  password: __DB_PASSWORD__

# 加密密钥（用于加密存储的私钥）
# 生产环境请使用随机生成的密钥
master_key_file: /etc/vpn-manager/master.key
encryption_key: "__ENCRYPTION_KEY__"

# 日志配置
log:
  level: __LOG_LEVEL__
  file: /var/log/vpn-manager/core.log

# OpenVPN 配置
openvpn:
  binary: /usr/sbin/openvpn
  config_dir: /etc/openvpn
  management_socket: /var/run/openvpn/management.sock

# PKI 配置
pki:
  ca_key: /var/lib/vpn-manager/pki/ca.key
