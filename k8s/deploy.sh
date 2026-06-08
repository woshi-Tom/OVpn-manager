#!/bin/bash
# VPN Manager k3s 部署脚本
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "    VPN Manager k3s 部署"
echo "========================================"

# 1. 构建镜像
echo "[1/4] 构建 Docker 镜像..."
docker build -t vpn-core:latest -f "$SCRIPT_DIR/../docker/Dockerfile.core" "$PROJECT_DIR"
docker build -t vpn-web:latest -f "$SCRIPT_DIR/../docker/Dockerfile.web" "$PROJECT_DIR"

# 如果使用 k3s 的 containerd，需要导入镜像
if command -v k3s &> /dev/null; then
    echo "[1.5/4] 导入镜像到 k3s..."
    docker save vpn-core:latest | k3s ctr images import -
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

# 等待 PostgreSQL 就绪
echo "等待 PostgreSQL 启动..."
kubectl -n vpn-manager wait --for=condition=ready pod -l app=postgres --timeout=120s

# 4. 部署 Core 和 Web
echo "[4/4] 部署 Core 和 Web..."
kubectl apply -f "$SCRIPT_DIR/core.yaml"
kubectl apply -f "$SCRIPT_DIR/web.yaml"

echo ""
echo "========================================"
echo "    部署完成！"
echo "========================================"
echo ""
echo "查看状态:"
echo "  kubectl -n vpn-manager get pods"
echo "  kubectl -n vpn-manager get svc"
echo ""
echo "访问 Web:"
WEB_PORT=$(kubectl -n vpn-manager get svc vpn-web-service -o jsonpath='{.spec.ports[0].nodePort}' 2>/dev/null)
if [ -n "$WEB_PORT" ]; then
    echo "  http://<节点IP>:$WEB_PORT"
else
    echo "  kubectl -n vpn-manager port-forward svc/vpn-web-service 5000:5000"
    echo "  http://localhost:5000"
fi
echo ""
echo "初始化数据库:"
echo "  kubectl -n vpn-manager exec -it <postgres-pod> -- psql -U vpn_core -d vpn_db -f /dev/stdin < schema.sql"
