#include "database.h"
#include "config.h"
#include "logger.h"
#include <libpq-fe.h>
#include <openssl/rand.h>
#include <sys/stat.h>

PGconn *g_conn = NULL;

static int init_web_user(void);
static int clear_config_password(void);

int connect_db(void) {
    char conninfo[4096];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%d dbname=%s user=%s password=%s sslmode=prefer",
             g_config.db_host, g_config.db_port, g_config.db_name,
             g_config.db_user, g_config.db_password);

    g_conn = PQconnectdb(conninfo);
    if (PQstatus(g_conn) != CONNECTION_OK) {
        log_message(LOG_ERR, "数据库连接失败: %s", PQerrorMessage(g_conn));
        PQfinish(g_conn);
        g_conn = NULL;
        return -1;
    }
    log_message(LOG_INFO, "数据库连接成功");
    log_message(LOG_DEBUG, "数据库详情: dbname=%s, host=%s, port=%d, user=%s", 
                g_config.db_name, g_config.db_host, g_config.db_port, g_config.db_user);
    return 0;
}

void disconnect_db(void) {
    if (g_conn) {
        PQfinish(g_conn);
        g_conn = NULL;
    }
}

int check_db_initialized(void) {
    const char *sql = "SELECT 1 FROM pg_tables WHERE tablename = 'vpn_config'";
    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        log_message(LOG_ERR, "检查数据库初始化状态失败: %s", PQerrorMessage(g_conn));
        PQclear(res);
        return -1;
    }
    int initialized = (PQntuples(res) > 0);
    PQclear(res);
    return initialized;
}

int init_database(const char *schema_path) {
    FILE *sql_fh = fopen(schema_path, "r");
    if (!sql_fh) {
        log_message(LOG_ERR, "无法打开 SQL 脚本文件 %s: %s", schema_path, strerror(errno));
        return -1;
    }

    fseek(sql_fh, 0, SEEK_END);
    long len = ftell(sql_fh);
    fseek(sql_fh, 0, SEEK_SET);
    char *sql_buf = (char*)malloc(len + 1);
    if (!sql_buf) {
        log_message(LOG_ERR, "内存不足");
        fclose(sql_fh);
        return -1;
    }
    fread(sql_buf, 1, len, sql_fh);
    fclose(sql_fh);
    sql_buf[len] = '\0';

    PGresult *res = PQexec(g_conn, "BEGIN");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        log_message(LOG_ERR, "开始事务失败: %s", PQerrorMessage(g_conn));
        PQclear(res);
        free(sql_buf);
        return -1;
    }
    PQclear(res);

    res = PQexec(g_conn, sql_buf);
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        log_message(LOG_ERR, "执行 SQL 脚本失败: %s", PQerrorMessage(g_conn));
        log_message(LOG_ERR, "SQL错误: %s", PQresultErrorMessage(res));
        PQclear(res);
        PQexec(g_conn, "ROLLBACK");
        free(sql_buf);
        return -1;
    }
    PQclear(res);

    res = PQexec(g_conn, "COMMIT");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        log_message(LOG_ERR, "提交事务失败: %s", PQerrorMessage(g_conn));
        PQclear(res);
        PQexec(g_conn, "ROLLBACK");
        free(sql_buf);
        return -1;
    }
    PQclear(res);

    free(sql_buf);
    log_message(LOG_INFO, "数据库初始化成功");
    
    return init_web_user();
}

static int init_web_user(void) {
    char web_password[33];
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    unsigned char random_bytes[32];
    
    if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1) {
        log_message(LOG_WARNING, "生成 Web 用户密码失败，跳过创建");
        return 0;
    }
    
    for (int i = 0; i < 32; i++) {
        web_password[i] = charset[random_bytes[i] % (sizeof(charset) - 1)];
    }
    web_password[32] = '\0';
    
    char sql[4096];
    snprintf(sql, sizeof(sql),
        "CREATE USER vpn_web WITH PASSWORD '%s';"
        "REVOKE ALL ON ALL TABLES IN SCHEMA public FROM vpn_web;"
        "REVOKE ALL ON ALL SEQUENCES IN SCHEMA public FROM vpn_web;"
        "GRANT SELECT ON vpn_config TO vpn_web;"
        "GRANT SELECT ON vpn_config_tun TO vpn_web;"
        "GRANT SELECT ON vpn_config_tap TO vpn_web;"
        "GRANT SELECT ON vpn_users TO vpn_web;"
        "GRANT SELECT ON vpn_client_profiles TO vpn_web;"
        "GRANT SELECT ON vpn_sessions TO vpn_web;"
        "GRANT SELECT ON vpn_ca TO vpn_web;"
        "GRANT SELECT ON vpn_admins TO vpn_web;"
        "GRANT UPDATE ON vpn_admins TO vpn_web;"
        "GRANT SELECT ON vpn_revoked_certs TO vpn_web;"
        "GRANT USAGE ON SCHEMA public TO vpn_web;"
        "GRANT SELECT ON system_logs TO vpn_web;"
        "GRANT INSERT ON system_logs TO vpn_web;"
        "GRANT INSERT ON vpn_config TO vpn_web;"
        "GRANT UPDATE ON vpn_config TO vpn_web;"
        "GRANT DELETE ON vpn_config TO vpn_web;"
        "GRANT INSERT ON vpn_config_tun TO vpn_web;"
        "GRANT UPDATE ON vpn_config_tun TO vpn_web;"
        "GRANT DELETE ON vpn_config_tun TO vpn_web;"
        "GRANT INSERT ON vpn_config_tap TO vpn_web;"
        "GRANT UPDATE ON vpn_config_tap TO vpn_web;"
        "GRANT DELETE ON vpn_config_tap TO vpn_web;"
        "GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO vpn_web;",
        web_password);
    
    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const char *err = PQerrorMessage(g_conn);
        if (strstr(err, "already exists") || strstr(err, "DuplicateObject")) {
            log_message(LOG_INFO, "Web 用户已存在，跳过创建");
            PQclear(res);
        } else {
            log_message(LOG_ERR, "创建 Web 用户失败: %s", err);
            PQclear(res);
            return -1;
        }
    } else {
        log_message(LOG_INFO, "Web 用户创建成功");
    }
    PQclear(res);
    
    const char *auth_dir = "/etc/vpn-manager";
    mkdir(auth_dir, 0755);
    
    char auth_file[256];
    snprintf(auth_file, sizeof(auth_file), "%s/web-db-auth", auth_dir);
    
    FILE *fp = fopen(auth_file, "w");
    if (!fp) {
        log_message(LOG_ERR, "无法创建认证文件 %s: %s", auth_file, strerror(errno));
        return -1;
    }
    
    fprintf(fp, "host=%s\n", g_config.db_host);
    fprintf(fp, "port=%d\n", g_config.db_port);
    fprintf(fp, "dbname=%s\n", g_config.db_name);
    fprintf(fp, "user=vpn_web\n");
    fprintf(fp, "password=%s\n", web_password);
    fclose(fp);
    
    chmod(auth_file, 0640);
    chown(auth_file, 0, getgid());
    
    log_message(LOG_INFO, "Web 认证文件已生成: %s", auth_file);
    
    // 数据库初始化完成后，清除配置文件中的数据库密码
    clear_config_password();
    
    return 0;
}

// 清除配置文件中的数据库密码 - 初始化完成后调用
static int clear_config_password(void) {
    const char *config_path = "/etc/vpn-manager/core.yaml";
    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        log_message(LOG_WARNING, "无法打开配置文件清除密码");
        return -1;
    }
    
    // 读取所有行
    char lines[100][512];
    int line_count = 0;
    char line[512];
    
    while (fgets(line, sizeof(line), fp) && line_count < 100) {
        strncpy(lines[line_count], line, sizeof(lines[line_count]) - 1);
        lines[line_count][sizeof(lines[line_count]) - 1] = '\0';
        line_count++;
    }
    fclose(fp);
    
    // 修改包含 password 的行
    for (int i = 0; i < line_count; i++) {
        if (strstr(lines[i], "password")) {
            char *p = strchr(lines[i], ':');
            if (p) {
                *++p = ' ';
                const char *comment = "  # cleared, auth via web-db-auth\n";
                size_t remaining = sizeof(lines[i]) - (p - lines[i]);
                strncpy(p, comment, remaining - 1);
                lines[i][sizeof(lines[i]) - 1] = '\0';
            }
        }
    }
    
    // 写回文件
    fp = fopen(config_path, "w");
    if (!fp) {
        log_message(LOG_ERR, "无法写入配置文件");
        return -1;
    }
    
    for (int i = 0; i < line_count; i++) {
        fputs(lines[i], fp);
    }
    fclose(fp);
    
    // 确保文件权限安全
    chmod(config_path, 0600);
    
    log_message(LOG_INFO, "已清除配置文件中的数据库密码");
    
    return 0;
}

// 记录系统日志 - 使用参数化查询防止SQL注入
void log_system_event(const char *level, const char *source, const char *msg) {
    if (!g_conn || !level || !source || !msg) return;
    
    const char *params[3];
    params[0] = level;
    params[1] = source;
    params[2] = msg;
    
    PGresult *res = PQexecParams(g_conn,
        "INSERT INTO system_logs (level, source, message) VALUES ($1, $2, $3)",
        3, NULL, params, NULL, NULL, 0);
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        log_message(LOG_ERR, "写入 system_logs 失败: %s", PQerrorMessage(g_conn));
    }
    PQclear(res);
}
