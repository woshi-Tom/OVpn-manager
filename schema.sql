-- =====================================================
-- VPN 管理系统数据库初始化脚本 (PostgreSQL)
-- 版本: 8.3
-- =====================================================

-- =====================================================
-- 用户表 - 存储 VPN 客户端用户信息
-- =====================================================
CREATE TABLE vpn_users (
    id          SERIAL PRIMARY KEY,
    username    VARCHAR(64) NOT NULL UNIQUE,
    client_name VARCHAR(128),
    email       VARCHAR(128),
    created_at  TIMESTAMP NOT NULL DEFAULT NOW(),
    disabled    BOOLEAN NOT NULL DEFAULT FALSE,
    description TEXT
);

-- 用户表注释
COMMENT ON TABLE vpn_users IS 'VPN用户表，存储客户端用户信息';
COMMENT ON COLUMN vpn_users.id IS '自增主键';
COMMENT ON COLUMN vpn_users.username IS '用户名，用于OpenVPN认证，必须唯一';
COMMENT ON COLUMN vpn_users.client_name IS '客户端显示名称（可选）';
COMMENT ON COLUMN vpn_users.email IS '用户邮箱（可选）';
COMMENT ON COLUMN vpn_users.created_at IS '创建时间';
COMMENT ON COLUMN vpn_users.disabled IS '是否禁用，TRUE为禁用';
COMMENT ON COLUMN vpn_users.description IS '备注描述';

-- =====================================================
-- 主配置表 - 存储 OpenVPN 服务器配置和证书
-- =====================================================
CREATE TABLE vpn_config (
    id           SERIAL PRIMARY KEY,
    config_name  VARCHAR(64) NOT NULL,
    mode         VARCHAR(3) NOT NULL CHECK (mode IN ('tun', 'tap')),
    proto        VARCHAR(10) NOT NULL DEFAULT 'udp',
    port         INTEGER NOT NULL CHECK (port BETWEEN 1 AND 65535),
    remote       VARCHAR(255) NOT NULL,
    ca_cert      TEXT,
    ca_fingerprint TEXT,
    server_cert  TEXT,
    server_key   TEXT,
    status       VARCHAR(20) DEFAULT 'stopped',
    created_at   TIMESTAMP NOT NULL DEFAULT NOW(),
    updated_at   TIMESTAMP NOT NULL DEFAULT NOW()
);

-- 主配置表注释
COMMENT ON TABLE vpn_config IS 'VPN配置主表，存储OpenVPN服务器配置和证书信息';
COMMENT ON COLUMN vpn_config.id IS '自增主键';
COMMENT ON COLUMN vpn_config.config_name IS '配置名称，用于标识不同的VPN配置';
COMMENT ON COLUMN vpn_config.mode IS '模式：tun（路由模式）或tap（桥接模式）';
COMMENT ON COLUMN vpn_config.proto IS '协议：udp或tcp';
COMMENT ON COLUMN vpn_config.port IS 'OpenVPN监听端口';
COMMENT ON COLUMN vpn_config.remote IS 'VPN服务器地址（IP或域名），客户端连接用';
COMMENT ON COLUMN vpn_config.ca_cert IS 'CA证书（PEM格式），用于验证客户端证书';
COMMENT ON COLUMN vpn_config.ca_fingerprint IS 'CA证书指纹，用于关联CA';
COMMENT ON COLUMN vpn_config.server_cert IS '服务器证书（PEM格式）';
COMMENT ON COLUMN vpn_config.server_key IS '服务器私钥（PEM格式，已加密存储）';
COMMENT ON COLUMN vpn_config.status IS '运行状态：stopped/running';
COMMENT ON COLUMN vpn_config.created_at IS '创建时间';
COMMENT ON COLUMN vpn_config.updated_at IS '最后更新时间';

-- =====================================================
-- TUN模式扩展表 - 路由模式配置
-- =====================================================
CREATE TABLE vpn_config_tun (
    config_id    INTEGER PRIMARY KEY REFERENCES vpn_config(id) ON DELETE CASCADE,
    server_ip   INET NOT NULL,
    subnet_mask  CIDR NOT NULL,
    push_dns    INET[],
    enable_nat  BOOLEAN NOT NULL DEFAULT TRUE,
    push_redirect_gateway BOOLEAN NOT NULL DEFAULT FALSE,
    push_routes TEXT[] DEFAULT '{}'
);

-- TUN模式配置表注释
COMMENT ON TABLE vpn_config_tun IS 'TUN模式（路由模式）扩展配置表';
COMMENT ON COLUMN vpn_config_tun.config_id IS '关联的VPN配置ID';
COMMENT ON COLUMN vpn_config_tun.server_ip IS 'VPN服务器分配的起始IP（如10.8.0.1）';
COMMENT ON COLUMN vpn_config_tun.subnet_mask IS '子网掩码（CIDR格式，如10.8.0.0/24）';
COMMENT ON COLUMN vpn_config_tun.push_dns IS '推送给客户端的DNS服务器列表';
COMMENT ON COLUMN vpn_config_tun.enable_nat IS '是否启用NAT（iptables MASQUERADE）';
COMMENT ON COLUMN vpn_config_tun.push_redirect_gateway IS '是否推送redirect-gateway（强制所有流量走VPN）';
COMMENT ON COLUMN vpn_config_tun.push_routes IS '推送给客户端的额外路由';

-- =====================================================
-- TAP模式扩展表 - 桥接模式配置
-- =====================================================
CREATE TABLE vpn_config_tap (
    config_id    INTEGER PRIMARY KEY REFERENCES vpn_config(id) ON DELETE CASCADE,
    bridge_name  VARCHAR(15),
    physical_if  VARCHAR(15),
    dhcp_mode    VARCHAR(10) CHECK (dhcp_mode IN ('server', 'relay', 'none')),
    server_ip    INET,
    subnet_mask  INET
);

-- TAP模式配置表注释
COMMENT ON TABLE vpn_config_tap IS 'TAP模式（桥接模式）扩展配置表';
COMMENT ON COLUMN vpn_config_tap.config_id IS '关联的VPN配置ID';
COMMENT ON COLUMN vpn_config_tap.bridge_name IS '网桥名称（如br0）';
COMMENT ON COLUMN vpn_config_tap.physical_if IS '物理网卡名称（如eth0），用于桥接';
COMMENT ON COLUMN vpn_config_tap.dhcp_mode IS 'DHCP模式：server（VPN提供DHCP）、relay（DHCP中继）、none（静态IP）';
COMMENT ON COLUMN vpn_config_tap.server_ip IS '服务器IP（用于TAP模式的ifconfig）';
COMMENT ON COLUMN vpn_config_tap.subnet_mask IS '子网掩码（用于TAP模式的ifconfig）';

-- =====================================================
-- 客户端配置档案表 - 存储客户端证书和配置
-- =====================================================
CREATE TABLE vpn_client_profiles (
    id             SERIAL PRIMARY KEY,
    user_id        INTEGER REFERENCES vpn_users(id) ON DELETE CASCADE,
    config_id      INTEGER REFERENCES vpn_config(id) ON DELETE CASCADE,
    assigned_ip    INET,
    use_static_ip  BOOLEAN NOT NULL DEFAULT FALSE,
    client_cert    TEXT NOT NULL,
    client_key     TEXT NOT NULL,
    revoked        BOOLEAN NOT NULL DEFAULT FALSE,
    created_at     TIMESTAMP NOT NULL DEFAULT NOW(),
    UNIQUE (user_id, config_id)
);

-- 客户端档案表注释
COMMENT ON TABLE vpn_client_profiles IS '客户端配置档案表，存储客户端证书和配置';
COMMENT ON COLUMN vpn_client_profiles.id IS '自增主键';
COMMENT ON COLUMN vpn_client_profiles.user_id IS '关联的用户ID';
COMMENT ON COLUMN vpn_client_profiles.config_id IS '关联的VPN配置ID';
COMMENT ON COLUMN vpn_client_profiles.assigned_ip IS '分配的静态IP地址（可选）';
COMMENT ON COLUMN vpn_client_profiles.use_static_ip IS '是否使用静态IP';
COMMENT ON COLUMN vpn_client_profiles.client_cert IS '客户端证书（PEM格式，明文存储）';
COMMENT ON COLUMN vpn_client_profiles.client_key IS '客户端私钥（PEM格式，用户密码加密存储）';
COMMENT ON COLUMN vpn_client_profiles.revoked IS '证书是否已撤销';
COMMENT ON COLUMN vpn_client_profiles.created_at IS '创建时间';

-- =====================================================
-- 会话表 - 存储客户端连接会话信息
-- =====================================================
CREATE TABLE vpn_sessions (
    id                BIGSERIAL PRIMARY KEY,
    client_profile_id INTEGER REFERENCES vpn_client_profiles(id) ON DELETE SET NULL,
    user_id           INTEGER REFERENCES vpn_users(id) ON DELETE SET NULL,
    config_id         INTEGER REFERENCES vpn_config(id) ON DELETE SET NULL,
    virtual_ip        INET,
    real_ip           INET,
    connected_since   TIMESTAMP NOT NULL,
    disconnected_at   TIMESTAMP,
    bytes_sent        BIGINT NOT NULL DEFAULT 0,
    bytes_received    BIGINT NOT NULL DEFAULT 0,
    last_update       TIMESTAMP NOT NULL DEFAULT NOW()
);

-- 会话表注释
COMMENT ON TABLE vpn_sessions IS '会话表，记录客户端连接会话信息';
COMMENT ON COLUMN vpn_sessions.id IS '自增主键';
COMMENT ON COLUMN vpn_sessions.client_profile_id IS '关联的客户端档案ID';
COMMENT ON COLUMN vpn_sessions.user_id IS '关联的用户ID';
COMMENT ON COLUMN vpn_sessions.config_id IS '关联的VPN配置ID';
COMMENT ON COLUMN vpn_sessions.virtual_ip IS '客户端在VPN内的虚拟IP（如10.8.0.2）';
COMMENT ON COLUMN vpn_sessions.real_ip IS '客户端真实公网IP';
COMMENT ON COLUMN vpn_sessions.connected_since IS '连接开始时间';
COMMENT ON COLUMN vpn_sessions.disconnected_at IS '连接断开时间，NULL表示在线';
COMMENT ON COLUMN vpn_sessions.bytes_sent IS '已发送字节数';
COMMENT ON COLUMN vpn_sessions.bytes_received IS '已接收字节数';
COMMENT ON COLUMN vpn_sessions.last_update IS '最后更新时间';

-- =====================================================
-- 系统日志表 - 记录系统和用户操作日志
-- =====================================================
CREATE TABLE system_logs (
    id          BIGSERIAL PRIMARY KEY,
    timestamp   TIMESTAMP NOT NULL DEFAULT NOW(),
    level       VARCHAR(10) NOT NULL CHECK (level IN ('debug', 'info', 'warn', 'error')),
    source      VARCHAR(20) NOT NULL,
    message     TEXT NOT NULL
);

-- 系统日志表注释
COMMENT ON TABLE system_logs IS '系统日志表，记录关键操作和系统事件';
COMMENT ON COLUMN system_logs.id IS '自增主键';
COMMENT ON COLUMN system_logs.timestamp IS '日志时间戳';
COMMENT ON COLUMN system_logs.level IS '日志级别：debug/info/warn/error';
COMMENT ON COLUMN system_logs.source IS '日志来源：core/web';
COMMENT ON COLUMN system_logs.message IS '日志消息内容';

-- =====================================================
-- 管理员表 - 存储Web管理后台账户
-- =====================================================
CREATE TABLE vpn_admins (
    id          SERIAL PRIMARY KEY,
    username    VARCHAR(64) NOT NULL UNIQUE,
    password    VARCHAR(255) NOT NULL,
    created_at  TIMESTAMP NOT NULL DEFAULT NOW(),
    last_login  TIMESTAMP
);

-- 管理员表注释
COMMENT ON TABLE vpn_admins IS '管理员表，存储Web管理后台账户信息';
COMMENT ON COLUMN vpn_admins.id IS '自增主键';
COMMENT ON COLUMN vpn_admins.username IS '管理员用户名，必须唯一';
COMMENT ON COLUMN vpn_admins.password IS '密码（SHA256哈希存储）';
COMMENT ON COLUMN vpn_admins.created_at IS '账户创建时间';
COMMENT ON COLUMN vpn_admins.last_login IS '最后登录时间';

-- =====================================================
-- 证书撤销列表 - 记录已撤销的客户端证书
-- =====================================================
CREATE TABLE vpn_revoked_certs (
    id              SERIAL PRIMARY KEY,
    client_profile_id INTEGER REFERENCES vpn_client_profiles(id) ON DELETE CASCADE,
    serial_number   VARCHAR(255) NOT NULL,
    revoked_at      TIMESTAMP NOT NULL DEFAULT NOW(),
    reason          VARCHAR(100)
);

-- 证书撤销列表注释
COMMENT ON TABLE vpn_revoked_certs IS '证书撤销列表，记录已撤销的客户端证书';
COMMENT ON COLUMN vpn_revoked_certs.id IS '自增主键';
COMMENT ON COLUMN vpn_revoked_certs.client_profile_id IS '关联的客户端档案ID';
COMMENT ON COLUMN vpn_revoked_certs.serial_number IS '被撤销证书的序列号';
COMMENT ON COLUMN vpn_revoked_certs.revoked_at IS '撤销时间';
COMMENT ON COLUMN vpn_revoked_certs.reason IS '撤销原因';

-- =====================================================
-- CA证书表 - 存储CA证书信息
-- =====================================================
CREATE TABLE vpn_ca (
    id              SERIAL PRIMARY KEY,
    common_name     VARCHAR(128) NOT NULL,
    ca_cert         TEXT NOT NULL,
    ca_key          TEXT NOT NULL,
    ca_fingerprint  TEXT,
    created_at      TIMESTAMP NOT NULL DEFAULT NOW()
);

-- CA证书表注释
COMMENT ON TABLE vpn_ca IS 'CA证书表，存储CA证书和私钥信息';
COMMENT ON COLUMN vpn_ca.id IS '自增主键';
COMMENT ON COLUMN vpn_ca.common_name IS 'CA通用名称';
COMMENT ON COLUMN vpn_ca.ca_cert IS 'CA证书（PEM格式）';
COMMENT ON COLUMN vpn_ca.ca_key IS 'CA私钥（PEM格式，加密存储）';
COMMENT ON COLUMN vpn_ca.ca_fingerprint IS 'CA证书SHA256指纹';
COMMENT ON COLUMN vpn_ca.created_at IS '创建时间';

-- =====================================================
-- 索引 - 提高查询性能
-- =====================================================
CREATE INDEX idx_vpn_sessions_online ON vpn_sessions (disconnected_at) WHERE disconnected_at IS NULL;
CREATE INDEX idx_vpn_sessions_user_id ON vpn_sessions (user_id);
CREATE INDEX idx_system_logs_timestamp ON system_logs (timestamp);
CREATE INDEX idx_system_logs_level ON system_logs (level);
CREATE INDEX idx_vpn_revoked_certs_serial ON vpn_revoked_certs (serial_number);

-- =====================================================
-- 默认管理员账户
-- 用户名: admin
-- 密码: admin123 (SHA256哈希)
-- =====================================================
INSERT INTO vpn_admins (username, password) VALUES ('admin', '240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9');

-- =====================================================
-- vpn_web 用户权限设置
-- 用于 Web 端只读访问数据库
-- =====================================================
-- 撤销所有现有权限
REVOKE ALL ON ALL TABLES IN SCHEMA public FROM vpn_web;
REVOKE ALL ON ALL SEQUENCES IN SCHEMA public FROM vpn_web;
REVOKE ALL ON ALL FUNCTIONS IN SCHEMA public FROM vpn_web;

-- 只读权限 (查询)
GRANT SELECT ON vpn_config TO vpn_web;
GRANT SELECT ON vpn_config_tun TO vpn_web;
GRANT SELECT ON vpn_config_tap TO vpn_web;
GRANT SELECT ON vpn_users TO vpn_web;
GRANT SELECT ON vpn_client_profiles TO vpn_web;
GRANT SELECT ON vpn_sessions TO vpn_web;
GRANT SELECT ON vpn_ca TO vpn_web;
GRANT SELECT ON vpn_admins TO vpn_web;
GRANT SELECT ON vpn_revoked_certs TO vpn_web;
GRANT SELECT ON system_logs TO vpn_web;

-- 写权限 (需要验证当前密码)
GRANT UPDATE ON vpn_admins TO vpn_web;

-- Schema 使用权限
GRANT USAGE ON SCHEMA public TO vpn_web;

-- 写权限 (系统日志)
GRANT SELECT ON system_logs TO vpn_web;
GRANT INSERT ON system_logs TO vpn_web;

-- VPN 配置写权限 (INSERT, UPDATE, DELETE)
GRANT INSERT ON vpn_config TO vpn_web;
GRANT UPDATE ON vpn_config TO vpn_web;
GRANT DELETE ON vpn_config TO vpn_web;
GRANT INSERT ON vpn_config_tun TO vpn_web;
GRANT UPDATE ON vpn_config_tun TO vpn_web;
GRANT DELETE ON vpn_config_tun TO vpn_web;
GRANT INSERT ON vpn_config_tap TO vpn_web;
GRANT UPDATE ON vpn_config_tap TO vpn_web;
GRANT DELETE ON vpn_config_tap TO vpn_web;

-- 序列权限 (用于 INSERT)
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO vpn_web;
