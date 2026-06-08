#!/bin/bash
# =====================================================
# VPN Manager - 卸载脚本
# =====================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_ok() { echo -e "${GREEN}✓ $1${NC}"; }
print_err() { echo -e "${RED}✗ $1${NC}"; }
print_info() { echo -e "${YELLOW}ℹ $1${NC}"; }

if [[ $EUID -ne 0 ]]; then
    print_err "此脚本需要 root 权限运行"
    exit 1
fi

echo "========================================"
echo "       VPN Manager 卸载脚本"
echo "========================================"
echo ""
print_info "此操作将："
echo "  1. 停止所有服务"
echo "  2. 删除 systemd 服务"
echo "  3. 删除配置文件"
echo "  4. 删除管理脚本"
echo "  5. (可选) 删除数据库"
echo ""
read -p "确认卸载？[y/N]: " confirm
if [[ "$confirm" != "y" && "$confirm" != "Y" ]]; then
    echo "已取消"
    exit 0
fi

echo ""
print_info "开始卸载..."

# 1. 停止服务
print_info "1. 停止服务..."
systemctl stop vpn-core vpn-web 2>/dev/null || true
systemctl disable vpn-core vpn-web 2>/dev/null || true
print_ok "服务已停止"

# 2. 删除 systemd 服务
print_info "2. 删除 systemd 服务..."
rm -f /etc/systemd/system/vpn-core.service
rm -f /etc/systemd/system/vpn-web.service
systemctl daemon-reload
print_ok "systemd 服务已删除"

# 3. 删除管理脚本
print_info "3. 删除管理脚本..."
rm -f /usr/local/bin/vpn-manager
print_ok "管理脚本已删除"

# 4. 删除配置文件
print_info "4. 删除配置文件..."
read -p "删除配置文件？[y/N]: " config_confirm
if [[ "$config_confirm" == "y" || "$config_confirm" == "Y" ]]; then
    rm -rf /etc/vpn-manager
    rm -rf /var/log/vpn-manager
    print_ok "配置文件已删除"
fi

# 5. 删除数据库
print_info "5. 删除数据库..."
read -p "删除数据库？(危险！) [y/N]: " db_confirm
if [[ "$db_confirm" == "y" || "$db_confirm" == "Y" ]]; then
    print_err "数据库删除功能已禁用，请手动执行："
    echo "  drop database vpn_db;"
    echo "  drop user vpn_core;"
fi

# 6. 删除 Web 目录
print_info "6. 删除 Web 目录..."
rm -rf /workspace/web
rm -rf /opt/vpn-web-venv
print_ok "Web 目录已删除"

# 7. 删除 Core 二进制
print_info "7. 删除 Core 二进制..."
rm -f /usr/local/bin/vpn-core
print_ok "Core 已删除"

echo ""
echo "========================================"
print_ok "卸载完成！"
echo "========================================"
