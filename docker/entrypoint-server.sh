#!/bin/bash
set -e

# =====================================================
# VPN Server 容器入口脚本
# Core + OpenVPN 在同一容器中运行
# =====================================================

CONFIG_FILE="/etc/vpn-manager/core.yaml"

# 如果 core.yaml 不存在，从环境变量生成
if [ ! -f "$CONFIG_FILE" ]; then
    echo "[entrypoint] 生成 core.yaml ..."

    DB_HOST="${DB_HOST:-postgres-service}"
    DB_PORT="${DB_PORT:-5432}"
    DB_NAME="${DB_NAME:-vpn_db}"
    DB_USER="${DB_USER:-vpn_core}"
    DB_PASSWORD="${DB_PASSWORD:-}"
    ENCRYPTION_KEY="${ENCRYPTION_KEY:-}"
    LISTEN_PORT="${LISTEN_PORT:-9000}"
    LOG_LEVEL="${LOG_LEVEL:-info}"

    cat > "$CONFIG_FILE" << EOF
database:
  host: ${DB_HOST}
  port: ${DB_PORT}
  name: ${DB_NAME}
  user: ${DB_USER}
  password: "${DB_PASSWORD}"

listen_host: "0.0.0.0"
listen_port: ${LISTEN_PORT}

encryption_key: "${ENCRYPTION_KEY}"

log:
  level: ${LOG_LEVEL}
  file: /dev/stdout

openvpn:
  binary: /usr/sbin/openvpn
  config_dir: /etc/openvpn
  management_socket: /var/run/openvpn/management.sock

pki:
  ca_key: /var/lib/vpn-manager/pki/ca.key
EOF

    chmod 600 "$CONFIG_FILE"
    echo "[entrypoint] core.yaml 已生成 (listen_port=${LISTEN_PORT})"
fi

# 确保 TUN 设备存在
if [ ! -c /dev/net/tun ]; then
    echo "[entrypoint] 创建 /dev/net/tun ..."
    mkdir -p /dev/net
    mknod /dev/net/tun c 10 200
    chmod 600 /dev/net/tun
fi

# 启用 IP 转发（VPN 路由需要）
echo 1 > /proc/sys/net/ipv4/ip_forward 2>/dev/null || true

echo "[entrypoint] 启动 VPN Core ..."
exec /usr/local/bin/vpn-core
