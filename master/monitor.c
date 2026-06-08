#include "monitor.h"
#include "logger.h"
#include "config.h"
#include "database.h"
#include "openvpn.h"
#include <pthread.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <time.h>

// 前向声明
int stop_openvpn(int config_id);

typedef struct {
    int config_id;
    int running;
    pthread_t thread;
} monitor_ctx_t;

static monitor_ctx_t *monitors = NULL;
static int monitors_count = 0;
static pthread_mutex_t monitors_mutex = PTHREAD_MUTEX_INITIALIZER;

static int running_configs[32];
static int running_count = 0;
static pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;

static monitor_ctx_t* get_monitor_ctx(int config_id) {
    pthread_mutex_lock(&monitors_mutex);
    for (int i = 0; i < monitors_count; i++) {
        if (monitors[i].config_id == config_id) {
            pthread_mutex_unlock(&monitors_mutex);
            return &monitors[i];
        }
    }
    monitors = realloc(monitors, (monitors_count + 1) * sizeof(monitor_ctx_t));
    monitor_ctx_t *ctx = &monitors[monitors_count];
    memset(ctx, 0, sizeof(monitor_ctx_t));
    ctx->config_id = config_id;
    ctx->running = 0;
    monitors_count++;
    pthread_mutex_unlock(&monitors_mutex);
    return ctx;
}

// 将一行按逗号分割，返回字段数组（fields 指向 copy 内的位置），返回字段数
static int split_line(char *line, char *fields[], int max_fields) {
    int count = 0;
    char *p = line;
    char *start = p;
    while (*p) {
        if (*p == ',') {
            *p = '\0';
            fields[count++] = start;
            if (count >= max_fields) break;
            start = p + 1;
        }
        p++;
    }
    if (count < max_fields && *start) {
        fields[count++] = start;
    }
    return count;
}

static void update_sessions(PGconn *conn, int config_id, const char *status_buf) {
    const char *p = status_buf;
    char line[4096];
    int online_profiles[256];  // 存储本次在线的 profile_id
    int online_count = 0;

    while (*p) {
        const char *end = strchr(p, '\n');
        if (!end) end = p + strlen(p);
        size_t line_len = end - p;
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        strncpy(line, p, line_len);
        line[line_len] = '\0';
        p = (*end == '\n') ? end + 1 : end;

        if (strncmp(line, "CLIENT_LIST,", 12) != 0) {
            continue;
        }

        // 复制一行并分割字段
        char *copy = strdup(line + 12);
        if (!copy) continue;

        char *fields[20];
        int field_count = split_line(copy, fields, 20);

        // 需要至少 7 个字段 (0:common_name, 1:real_addr, 2:virtual_ip, 4:bytes_recv, 5:bytes_sent, 6:time_str)
        if (field_count < 7) {
            log_message(LOG_DEBUG, "客户端行字段不足: %s", line);
            free(copy);
            continue;
        }

        char *common_name = fields[0];
        char *real_addr   = fields[1];
        char *virtual_ip  = fields[2];
        char *bytes_recv_str = fields[4];
        char *bytes_sent_str = fields[5];
        char *time_str       = fields[6];

        unsigned long long bytes_recv = strtoull(bytes_recv_str, NULL, 10);
        unsigned long long bytes_sent = strtoull(bytes_sent_str, NULL, 10);
        time_t connected_since = time(NULL);
        struct tm tm = {0};

        if (strptime(time_str, "%Y-%m-%d %H:%M:%S", &tm) != NULL) {
            connected_since = mktime(&tm);
        }

        // 剥离 IP 端口
        char ip_only[64];
        strncpy(ip_only, real_addr, sizeof(ip_only)-1);
        ip_only[sizeof(ip_only)-1] = '\0';
        char *colon = strchr(ip_only, ':');
        if (colon) *colon = '\0';

        // 查询数据库获取 user_id 和 profile_id - 使用参数化查询防止SQL注入
        const char *params_user[2];
        params_user[0] = common_name;
        char config_id_str[16];
        snprintf(config_id_str, sizeof(config_id_str), "%d", config_id);
        params_user[1] = config_id_str;
        
        PGresult *res = PQexecParams(conn,
            "SELECT u.id, cp.id FROM vpn_users u "
            "JOIN vpn_client_profiles cp ON u.id = cp.user_id "
            "WHERE u.username = $1 AND cp.config_id = $2",
            2, NULL, params_user, NULL, NULL, 0);
        
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            int user_id = atoi(PQgetvalue(res, 0, 0));
            int profile_id = atoi(PQgetvalue(res, 0, 1));
            PQclear(res);

            // 将 profile_id 加入在线列表
            if (online_count < 256) {
                online_profiles[online_count++] = profile_id;
            }

            // 检查是否已有在线会话
            char sql[2048];
            snprintf(sql, sizeof(sql),
                     "SELECT id FROM vpn_sessions "
                     "WHERE client_profile_id = %d AND disconnected_at IS NULL",
                     profile_id);
            res = PQexec(conn, sql);
            int has_online = (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0);
            
            if (has_online) {
                int session_id = atoi(PQgetvalue(res, 0, 0));
                PQclear(res);
                snprintf(sql, sizeof(sql),
                         "UPDATE vpn_sessions SET bytes_sent = %llu, bytes_received = %llu, last_update = NOW() "
                         "WHERE id = %d", bytes_sent, bytes_recv, session_id);
                PGresult *res2 = PQexec(conn, sql);
                if (PQresultStatus(res2) != PGRES_COMMAND_OK) {
                    log_message(LOG_ERR, "更新会话失败: %s", PQerrorMessage(conn));
                }
                PQclear(res2);
            } else {
                PQclear(res);
                // 插入新会话 - 使用参数化查询防止SQL注入
                const char *params_insert[8];
                char profile_id_str[16], user_id_str[16], config_id_str2[16];
                char bytes_sent_str2[32], bytes_recv_str2[32];
                char time_buf[64];
                
                snprintf(profile_id_str, sizeof(profile_id_str), "%d", profile_id);
                snprintf(user_id_str, sizeof(user_id_str), "%d", user_id);
                snprintf(config_id_str2, sizeof(config_id_str2), "%d", config_id);
                snprintf(bytes_sent_str2, sizeof(bytes_sent_str2), "%llu", bytes_sent);
                snprintf(bytes_recv_str2, sizeof(bytes_recv_str2), "%llu", bytes_recv);
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&connected_since));
                
                params_insert[0] = profile_id_str;
                params_insert[1] = user_id_str;
                params_insert[2] = config_id_str2;
                params_insert[3] = virtual_ip ? virtual_ip : "";
                params_insert[4] = ip_only;
                params_insert[5] = time_buf;
                params_insert[6] = bytes_sent_str2;
                params_insert[7] = bytes_recv_str2;
                
                PGresult *res_insert = PQexecParams(conn,
                    "INSERT INTO vpn_sessions "
                    "(client_profile_id, user_id, config_id, virtual_ip, real_ip, connected_since, bytes_sent, bytes_received, last_update) "
                    "VALUES ($1::integer, $2::integer, $3::integer, $4::inet, $5::inet, $6::timestamp, $7::bigint, $8::bigint, NOW())",
                    8, NULL, params_insert, NULL, NULL, 0);
                
                if (PQresultStatus(res_insert) != PGRES_COMMAND_OK) {
                    log_message(LOG_ERR, "插入会话失败: %s", PQerrorMessage(conn));
                }
                PQclear(res_insert);
            }
        } else {
            PQclear(res);
            log_message(LOG_DEBUG, "未知客户端 common_name=%s, 跳过", common_name);
        }

        free(copy);
    }

    // 标记断开的客户端
    if (online_count > 0) {
        // 构建 profile_id 列表字符串
        char id_list[1024] = "";
        for (int i = 0; i < online_count; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", online_profiles[i]);
            if (i > 0) strcat(id_list, ",");
            strcat(id_list, buf);
        }
        char sql[2048];
        snprintf(sql, sizeof(sql),
                 "UPDATE vpn_sessions SET disconnected_at = NOW() "
                 "WHERE config_id = %d AND disconnected_at IS NULL "
                 "AND client_profile_id NOT IN (%s)",
                 config_id, id_list);
        PGresult *res = PQexec(conn, sql);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            log_message(LOG_ERR, "标记断开会话失败: %s", PQerrorMessage(conn));
        }
        PQclear(res);
    } else {
        // 没有在线客户端，将所有在线会话标记为断开
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "UPDATE vpn_sessions SET disconnected_at = NOW() "
                 "WHERE config_id = %d AND disconnected_at IS NULL", config_id);
        PGresult *res = PQexec(conn, sql);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            log_message(LOG_ERR, "标记断开会话失败: %s", PQerrorMessage(conn));
        }
        PQclear(res);
    }
}

static void* monitor_thread(void *arg) {
    monitor_ctx_t *ctx = (monitor_ctx_t*)arg;
    int config_id = ctx->config_id;

    char sock_path[256];
    snprintf(sock_path, sizeof(sock_path), "/var/run/openvpn/management-%d.sock", config_id);
    
    log_message(LOG_INFO, "monitor[%d]: 监控线程启动, socket路径=%s", config_id, sock_path);

    while (ctx->running) {
        // 先检查 OpenVPN 进程是否还在运行，增加重试机制
        pid_t pid = -1;
        for (int retry = 0; retry < 5; retry++) {
            pid = get_openvpn_pid(config_id);
            if (pid > 0 && kill(pid, 0) == 0) {
                break;  // 进程存在
            }
            usleep(200000); // 等待 200ms，最多 1 秒
        }
        
        if (pid <= 0 || kill(pid, 0) != 0) {
            log_message(LOG_ERR, "monitor[%d]: OpenVPN 进程不存在或已退出", config_id);
            // 更新数据库状态为 stopped
            if (g_conn) {
                char sql[256];
                snprintf(sql, sizeof(sql), "UPDATE vpn_config SET status = 'stopped' WHERE id = %d", config_id);
                PGresult *res = PQexec(g_conn, sql);
                PQclear(res);
            }
            break;
        }

        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            log_message(LOG_ERR, "monitor[%d]: 创建 socket 失败: %s", config_id, strerror(errno));
            sleep(5);
            continue;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path)-1);

        if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            log_message(LOG_ERR, "monitor[%d]: 连接 management socket 失败: %s", config_id, strerror(errno));
            close(sock_fd);
            sleep(5);
            continue;
        }

        char welcome[256];
        int n = recv(sock_fd, welcome, sizeof(welcome)-1, 0);
        if (n > 0) {
            welcome[n] = '\0';
            log_message(LOG_DEBUG, "monitor[%d]: 欢迎信息: %s", config_id, welcome);
        }

        const char *cmd = "status 2\n";
        if (send(sock_fd, cmd, strlen(cmd), 0) < 0) {
            log_message(LOG_ERR, "monitor[%d]: 发送命令失败", config_id);
            close(sock_fd);
            sleep(5);
            continue;
        }

        char buffer[16384];
        int total = 0;
        while (1) {
            n = recv(sock_fd, buffer + total, sizeof(buffer) - total - 1, 0);
            if (n <= 0) break;
            total += n;
            buffer[total] = '\0';
            if (strstr(buffer, "\nEND\r\n") || strstr(buffer, "\nEND\n")) {
                break;
            }
        }

        close(sock_fd);

        if (total > 0) {
            if (g_conn) {
                update_sessions(g_conn, config_id, buffer);
            } else {
                log_message(LOG_ERR, "monitor[%d]: 数据库连接已断开", config_id);
            }
        }

        sleep(3);
    }

    log_message(LOG_INFO, "monitor[%d]: 监控线程退出", config_id);
    return NULL;
}

int monitor_start(int config_id) {
    monitor_ctx_t *ctx = get_monitor_ctx(config_id);
    if (ctx->running) {
        log_message(LOG_WARNING, "monitor[%d]: 监控线程已在运行", config_id);
        return 0;
    }
    ctx->running = 1;
    if (pthread_create(&ctx->thread, NULL, monitor_thread, ctx) != 0) {
        log_message(LOG_ERR, "monitor[%d]: 创建线程失败", config_id);
        ctx->running = 0;
        return -1;
    }
    pthread_mutex_lock(&running_mutex);
    if (running_count < 32) {
        running_configs[running_count++] = config_id;
    }
    pthread_mutex_unlock(&running_mutex);
    log_message(LOG_INFO, "monitor[%d]: 监控线程已启动", config_id);
    return 0;
}

void monitor_stop(int config_id) {
    monitor_ctx_t *ctx = get_monitor_ctx(config_id);
    if (ctx->running) {
        ctx->running = 0;
        pthread_join(ctx->thread, NULL);
        // 从运行列表中移除
        pthread_mutex_lock(&running_mutex);
        for (int i = 0; i < running_count; i++) {
            if (running_configs[i] == config_id) {
                running_configs[i] = running_configs[--running_count];
                break;
            }
        }
        pthread_mutex_unlock(&running_mutex);
        log_message(LOG_INFO, "monitor[%d]: 监控线程已停止", config_id);
    }
}

void monitor_stop_all(void) {
    pthread_mutex_lock(&running_mutex);
    int configs[32];
    int count = running_count;
    memcpy(configs, running_configs, count * sizeof(int));
    pthread_mutex_unlock(&running_mutex);

    // 先停止所有 OpenVPN 进程
    for (int i = 0; i < count; i++) {
        stop_openvpn(configs[i]);
    }
    
    // 再停止监控线程
    for (int i = 0; i < count; i++) {
        monitor_stop(configs[i]);
    }
}

int kick_client(int config_id, const char *target) {
    char sock_path[256];
    snprintf(sock_path, sizeof(sock_path), "/var/run/openvpn/management-%d.sock", config_id);

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        log_message(LOG_ERR, "kick[%d]: 创建 socket 失败: %s", config_id, strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path)-1);

    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_message(LOG_ERR, "kick[%d]: 连接 management socket 失败: %s", config_id, strerror(errno));
        close(sock_fd);
        return -1;
    }

    // 读取并丢弃欢迎信息
    char welcome[256];
    int n = recv(sock_fd, welcome, sizeof(welcome)-1, 0);
    if (n > 0) {
        welcome[n] = '\0';
        log_message(LOG_DEBUG, "kick[%d]: 欢迎信息: %s", config_id, welcome);
    }

    // 使用 kill 命令（适用于 Common Name 或 IP:port）
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "kill %s\n", target);
    if (send(sock_fd, cmd, strlen(cmd), 0) < 0) {
        log_message(LOG_ERR, "kick[%d]: 发送命令失败", config_id);
        close(sock_fd);
        return -1;
    }

    char resp[256];
    n = recv(sock_fd, resp, sizeof(resp)-1, 0);
    if (n > 0) {
        resp[n] = '\0';
        log_message(LOG_INFO, "kick[%d]: 响应: %s", config_id, resp);
    }

    close(sock_fd);
    return 0;
}
