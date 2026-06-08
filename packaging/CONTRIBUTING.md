# 贡献指南

欢迎为 VPN Manager 项目贡献代码！

## 如何贡献

### 1. 报告问题

发现 Bug 或有新功能建议？请提交 Issue。

请包含：
- 清晰的问题描述
- 复现步骤
- 环境信息

### 2. 提交代码

1. Fork 本仓库
2. 创建功能分支 (`git checkout -b feature/xxx`)
3. 进行修改
4. 提交更改 (`git commit -m '描述你的修改'`)
5. 推送分支 (`git push origin feature/xxx`)
6. 创建 Pull Request

## 开发环境搭建

### 依赖

- Ubuntu 20.04+
- PostgreSQL 14+
- Python 3.8+
- GCC, Make
- OpenSSL

### 本地开发

```bash
# 1. 克隆仓库
git clone <your-fork-url>
cd vpn-manager

# 2. 安装依赖
apt-get install -y postgresql-client openvpn openssl gcc make libpq-dev libyaml-dev libcjson-dev libssl-dev python3 python3-pip python3-venv

# 3. 编译 Core
cd master
make

# 4. 安装 Python 依赖
cd ../web
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# 5. 配置数据库
# 创建测试数据库并配置 core.yaml

# 6. 运行
# 启动 Core
cd ../master
./vpn-core

# 启动 Web (新终端)
cd ../web
python3 app.py
```

## 代码规范

### C 语言 (Core)

- 使用 4 空格缩进
- 遵循 K&R 风格
- 变量命名：snake_case
- 函数命名：snake_case

### Python (Web)

- 遵循 PEP 8
- 使用 4 空格缩进
- 变量/函数命名：snake_case
- 类命名：PascalCase

## Pull Request 指南

1. 确保代码能正常编译/运行
2. 保持代码风格一致
3. 添加必要的注释
4. 更新相关文档

## 问题反馈

如有问题，请提交 Issue。
