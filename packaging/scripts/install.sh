#!/bin/bash
# =====================================================
# VPN Manager - 一键安装脚本
# =====================================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_ok() { echo -e "${GREEN}✓ $1${NC}"; }
print_err() { echo -e "${RED}✗ $1${NC}"; }
print_info() { echo -e "${YELLOW}ℹ $1${NC}"; }

# 检查 root 权限
if [[ $EUID -ne 0 ]]; then
    print_err "此脚本需要 root 权限运行"
    exit 1
fi

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "       VPN Manager 安装脚本"
echo "========================================"
echo ""

# 配置变量
WEB_DIR="/workspace/web"
CORE_DIR="$PROJECT_DIR/master"
CONFIG_DIR="/etc/vpn-manager"
LOG_DIR="/var/log/vpn-manager"
PKI_DIR="/var/lib/vpn-manager/pki"

# 交互式配置
print_info "请配置数据库连接信息："
read -p "数据库主机 [localhost]: " DB_HOST
DB_HOST=${DB_HOST:-localhost}

read -p "数据库端口 [5432]: " DB_PORT
DB_PORT=${DB_PORT:-5432}

read -p "数据库名称 [vpn_db]: " DB_NAME
DB_NAME=${DB_NAME:-vpn_db}

read -p "数据库用户密码: " DB_PASSWORD
if [[ -z "$DB_PASSWORD" ]]; then
    print_err "数据库密码不能为空"
    exit 1
fi

read -p "Flask secret_key (用于 session 加密): " FLASK_SECRET
if [[ -z "$FLASK_SECRET" ]]; then
    FLASK_SECRET=$(python3 -c "import secrets; print(secrets.token_hex(32))")
    print_info "已自动生成 Flask secret_key"
fi

read -p "加密密钥 (用于加密私钥) [随机生成]: " ENCRYPTION_KEY
if [[ -z "$ENCRYPTION_KEY" ]]; then
    ENCRYPTION_KEY=$(python3 -c "import secrets; print(secrets.token_hex(16))")
    print_info "已自动生成加密密钥"
fi

read -p "日志等级 [info/debug/warn/error] [info]: " LOG_LEVEL
LOG_LEVEL=${LOG_LEVEL:-info}

echo ""
print_info "开始安装..."
echo ""

# 1. 安装系统依赖
print_info "1. 安装系统依赖..."
apt-get update
apt-get install -y postgresql-client openvpn openssl gcc make libpq-dev python3 python3-pip python3-venv
print_ok "系统依赖安装完成"

# 2. 创建目录
print_info "2. 创建目录..."
mkdir -p "$CONFIG_DIR" "$LOG_DIR" "$PKI_DIR" "$WEB_DIR"
chmod 755 "$CONFIG_DIR" "$LOG_DIR"
print_ok "目录创建完成"

# 3. 复制配置文件
print_info "3. 配置数据库..."

# 生成 Core 配置
cat > "$CONFIG_DIR/core.yaml" << EOF
database:
  host: $DB_HOST
  port: $DB_PORT
  name: $DB_NAME
  user: vpn_core
  password: $DB_PASSWORD

master_key_file: /etc/vpn-manager/master.key
encryption_key: "$ENCRYPTION_KEY"

log:
  level: $LOG_LEVEL
  file: $LOG_DIR/core.log

openvpn:
  binary: /usr/sbin/openvpn
  config_dir: /etc/openvpn
  management_socket: /var/run/openvpn/management.sock

pki:
  ca_key: $PKI_DIR/ca.key
EOF

# 生成 Web 配置
cat > "$CONFIG_DIR/web.yaml" << EOF
flask:
  secret_key: $FLASK_SECRET
  debug: false
  host: 0.0.0.0
  port: 5000

socket:
  core_socket: /var/run/vpn-manager/core.sock

log:
  level: $LOG_LEVEL
  file: $LOG_DIR/web.log
EOF

print_ok "配置文件已生成"

# 4. 复制 schema.sql
print_info "4. 复制数据库初始化脚本..."
cp "$PROJECT_DIR/schema.sql" "$CONFIG_DIR/schema.sql"
chmod 644 "$CONFIG_DIR/schema.sql"
print_ok "数据库脚本已复制"

# 5. 编译 Core
print_info "5. 编译 Core..."
cd "$CORE_DIR"
make clean
make
if [[ -f "vpn-core" ]]; then
    cp vpn-core /usr/local/bin/
    chmod +x /usr/local/bin/vpn-core
    print_ok "Core 编译完成"
else
    print_err "Core 编译失败"
    exit 1
fi

# 6. 安装 Web 依赖
print_info "6. 安装 Web 依赖..."
cd "$WEB_DIR"
cp -r ..

# 创建虚拟环境
python3 -m venv /opt/vpn-web-venv
source /opt/vpn-web-venv/bin/activate

# 安装 Python 依赖
pip install --upgrade pip
pip install flask psycopg2-binary pyyaml python-socketio eventlet gunicorn
deactivate

print_ok "Web 依赖安装完成"

# 7. 复制 systemd 服务
print_info "7. 安装 systemd 服务..."
sed "s|__WEB_DIR__|/workspace/web|g" "$SCRIPT_DIR/services/vpn-web.service" > /etc/systemd/system/vpn-web.service
cp "$SCRIPT_DIR/services/vpn-core.service" /etc/systemd/system/
systemctl daemon-reload
print_ok "systemd 服务已安装"

# 8. 安装管理脚本
print_info "8. 安装管理脚本..."
cp "$SCRIPT_DIR/scripts/vpn-manager" /usr/local/bin/
chmod +x /usr/local/bin/vpn-manager
print_ok "管理脚本已安装"

# 9. 设置权限
print_info "9. 设置权限..."
chmod 640 "$CONFIG_DIR/core.yaml"
chmod 640 "$CONFIG_DIR/web.yaml"
chown root:root "$CONFIG_DIR/core.yaml" "$CONFIG_DIR/web.yaml"
chown root:www-data "$CONFIG_DIR/web-db-auth" 2>/dev/null || true
print_ok "权限设置完成"

echo ""
echo "========================================"
print_ok "安装完成！"
echo "========================================"
echo ""
print_info "后续步骤："
echo "  1. 启动数据库服务"
echo "  2. 运行: vpn-manager init   (初始化数据库)"
echo "  3. 运行: vpn-manager start  (启动服务)"
echo "  4. 访问 Web: http://<your-ip>:5000"
echo ""
print_info "管理命令："
echo "  vpn-manager start     - 启动"
echo "  vpn-manager stop      - 停止"
echo "  vpn-manager restart   - 重启"
echo "  vpn-manager status    - 状态"
echo "  vpn-manager logs      - 日志"
echo "  vpn-manager enable    - 开机自启"
echo ""
