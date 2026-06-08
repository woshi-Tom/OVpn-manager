#!/bin/bash
# VPN Manager k3s 一键部署脚本
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "    VPN Manager k3s 部署"
echo "========================================"
echo ""

# 1. 构建镜像
echo "[1/4] 构建 Docker 镜像..."
docker build -t vpn-server:latest -f "$PROJECT_DIR/docker/Dockerfile.server" "$PROJECT_DIR"
docker build -t vpn-web:latest -f "$PROJECT_DIR/docker/Dockerfile.web" "$PROJECT_DIR"

# k3s 使用 containerd，需要导入镜像
if command -v k3s &> /dev/null; then
    echo "[1.5/4] 导入镜像到 k3s containerd..."
    docker save vpn-server:latest | k3s ctr images import -
    docker save vpn-web:latest | k3s ctr images import -
fi

# 2. 创建命名空间和配置
echo "[2/4] 创建 K8s 资源..."
kubectl apply -f "$SCRIPT_DIR/namespace.yaml"
kubectl apply -f "$SCRIPT_DIR/secret.yaml"
kubectl apply -f "$SCRIPT_DIR/configmap.yaml"

# 3. 部署 PostgreSQL
echo "[3/4] 部署 PostgreSQL..."
kubectl apply -f "$SCRIPT_DIR/postgres.yaml"
echo "等待 PostgreSQL 就绪..."
kubectl -n vpn-manager wait --for=condition=ready pod -l app=postgres --timeout=120s

# 4. 部署 VPN Server (Core + OpenVPN) 和 Web
echo "[4/4] 部署 VPN Server 和 Web..."
kubectl apply -f "$SCRIPT_DIR/server.yaml"
kubectl apply -f "$SCRIPT_DIR/web.yaml"

echo ""
echo "========================================"
echo "    部署完成！"
echo "========================================"
echo ""
echo "查看状态:"
echo "  kubectl -n vpn-manager get pods -o wide"
echo "  kubectl -n vpn-manager get svc"
echo ""
echo "查看日志:"
echo "  kubectl -n vpn-manager logs -l app=vpn-server -f"
echo "  kubectl -n vpn-manager logs -l app=vpn-web -f"
echo ""
WEB_PORT=$(kubectl -n vpn-manager get svc vpn-web-service -o jsonpath='{.spec.ports[0].nodePort}' 2>/dev/null)
echo "访问 Web: http://<节点IP>:${WEB_PORT:-5000}"
echo ""
echo "初始化数据库（首次部署）:"
echo "  kubectl -n vpn-manager exec -it deploy/vpn-web -- python3 -c \""
echo "    import db\""
