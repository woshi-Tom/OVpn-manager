-- VPN Manager 数据库权限脚本
-- 用于初始化或更新 vpn_web 用户的权限

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
