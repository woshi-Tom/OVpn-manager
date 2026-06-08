#include "socket_server.h"
#include "logger.h"
#include "openvpn.h"
#include "monitor.h"
#include "common.h"
#include "database.h"
#include "config.h"
#include "cert_utils.h"
#include "client_handler.h"
#include "ca_handler.h"

#include <pthread.h>
#include <libgen.h>

static cJSON* handle_ping(const cJSON *params);
static cJSON* handle_status(const cJSON *params);
static cJSON* handle_apply_config(const cJSON *params);
static cJSON* handle_stop_config(const cJSON *params);
static cJSON* handle_kick_client(const cJSON *params);
static cJSON* handle_update_admin_login(const cJSON *params);
static cJSON* dispatch_request(const cJSON *req);
static void* thread_handler(void *arg);

static cJSON* create_error_response(const char *message) {
    cJSON *err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "status", "error");
    cJSON_AddStringToObject(err, "message", message);
    return err;
}

static cJSON* handle_kick_client(const cJSON *params) {
    const cJSON *config_id = cJSON_GetObjectItem(params, "config_id");
    const cJSON *target = cJSON_GetObjectItem(params, "target");
    
    if (!config_id || !cJSON_IsNumber(config_id) || !target || !cJSON_IsString(target)) {
        return create_error_response("缺少 config_id 或 target");
    }
    
    int id = config_id->valueint;
    const char *t = target->valuestring;
    
    if (kick_client(id, t) == 0) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON_AddStringToObject(resp, "data", "踢出命令已发送");
        return resp;
    } else {
        return create_error_response("踢出失败");
    }
}

static void* thread_handler(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);
    char buffer[MAX_BUFFER];
    ssize_t n = read(client_fd, buffer, sizeof(buffer)-1);
    if (n <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[n] = '\0';
    log_message(LOG_DEBUG, "收到请求: %s", buffer);

    cJSON *req = cJSON_Parse(buffer);
    if (!req) {
        const char *errmsg = "{\"status\":\"error\",\"message\":\"JSON 解析失败\"}";
        write(client_fd, errmsg, strlen(errmsg));
        close(client_fd);
        return NULL;
    }

    cJSON *resp = dispatch_request(req);
    char *resp_str = cJSON_Print(resp);
    write(client_fd, resp_str, strlen(resp_str));
    free(resp_str);
    cJSON_Delete(resp);
    cJSON_Delete(req);
    close(client_fd);
    return NULL;
}

static cJSON* handle_ping(const cJSON *params) {
    (void)params;
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "data", "pong");
    return resp;
}

static cJSON* handle_status(const cJSON *params) {
    (void)params;
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "data", "running");
    return resp;
}

static cJSON* handle_apply_config(const cJSON *params) {
    const cJSON *config_id = cJSON_GetObjectItem(params, "config_id");
    if (!config_id || !cJSON_IsNumber(config_id)) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "status", "error");
        cJSON_AddStringToObject(err, "message", "缺少 config_id");
        return err;
    }
    int id = config_id->valueint;
    log_message(LOG_INFO, "收到 apply_config 请求，config_id=%d", id);

    if (start_openvpn(id) == 0) {
        char sql[256];
        snprintf(sql, sizeof(sql), "UPDATE vpn_config SET status = 'running' WHERE id = %d", id);
        PGresult *res = PQexec(g_conn, sql);
        PQclear(res);
        
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "应用配置成功，config_id=%d，VPN 已启动", id);
        log_system_event("info", "core", log_msg);
        
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON_AddStringToObject(resp, "data", "OpenVPN 启动成功");
        return resp;
    } else {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "应用配置失败，config_id=%d", id);
        log_system_event("error", "core", log_msg);
        
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "status", "error");
        cJSON_AddStringToObject(err, "message", "启动 OpenVPN 失败");
        return err;
    }
}

static cJSON* handle_stop_config(const cJSON *params) {
    const cJSON *config_id = cJSON_GetObjectItem(params, "config_id");
    if (!config_id || !cJSON_IsNumber(config_id)) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "status", "error");
        cJSON_AddStringToObject(err, "message", "缺少 config_id");
        return err;
    }
    int id = config_id->valueint;
    log_message(LOG_INFO, "收到 stop_config 请求，config_id=%d", id);

    if (stop_openvpn(id) == 0) {
        char sql[256];
        snprintf(sql, sizeof(sql), "UPDATE vpn_config SET status = 'stopped' WHERE id = %d", id);
        PGresult *res = PQexec(g_conn, sql);
        PQclear(res);
        
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "停止配置成功，config_id=%d，VPN 已停止", id);
        log_system_event("info", "core", log_msg);
        
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON_AddStringToObject(resp, "data", "OpenVPN 停止成功");
        return resp;
    } else {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "停止配置失败，config_id=%d", id);
        log_system_event("error", "core", log_msg);
        
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "status", "error");
        cJSON_AddStringToObject(err, "message", "停止 OpenVPN 失败");
        return err;
    }
}

static cJSON* handle_update_admin_login(const cJSON *params) {
    const cJSON *admin_id = cJSON_GetObjectItem(params, "admin_id");
    if (!admin_id || !cJSON_IsNumber(admin_id)) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "status", "error");
        cJSON_AddStringToObject(err, "message", "缺少 admin_id");
        return err;
    }
    
    int id = admin_id->valueint;
    char sql[256];
    snprintf(sql, sizeof(sql), "UPDATE vpn_admins SET last_login = NOW() WHERE id = %d", id);
    
    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        PQclear(res);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "status", "error");
        cJSON_AddStringToObject(err, "message", "更新登录时间失败");
        return err;
    }
    PQclear(res);
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    return resp;
}

static cJSON* dispatch_request(const cJSON *req) {
    const cJSON *action = cJSON_GetObjectItem(req, "action");
    if (!action || !cJSON_IsString(action)) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "status", "error");
        cJSON_AddStringToObject(err, "message", "请求缺少 action 字段");
        return err;
    }
    const char *action_str = action->valuestring;
    const cJSON *params = cJSON_GetObjectItem(req, "params");

    if (strcmp(action_str, "ping") == 0) {
        return handle_ping(params);
    } else if (strcmp(action_str, "status") == 0) {
        return handle_status(params);
    } else if (strcmp(action_str, "apply_config") == 0) {
        return handle_apply_config(params);
    } else if (strcmp(action_str, "kick_client") == 0) {
        return handle_kick_client(params);
    } else if (strcmp(action_str, "stop_config") == 0) {
        return handle_stop_config(params);
    } else if (strcmp(action_str, "gen_client_cert") == 0) {
        return handle_gen_client_cert(params);
    } else if (strcmp(action_str, "get_client_config") == 0) {
        return handle_get_client_config(params);
    } else if (strcmp(action_str, "change_client_password") == 0) {
        return handle_change_client_password(params);
    } else if (strcmp(action_str, "revoke_client_cert") == 0) {
        return handle_revoke_client_cert(params);
    } else if (strcmp(action_str, "generate_ca_cert") == 0) {
        return handle_generate_ca_cert(params);
    } else if (strcmp(action_str, "sign_server_cert") == 0) {
        return handle_sign_server_cert(params);
    } else if (strcmp(action_str, "get_ca_cert") == 0) {
        return handle_get_ca_cert(params);
    } else if (strcmp(action_str, "delete_ca_cert") == 0) {
        return handle_delete_ca_cert(params);
    } else if (strcmp(action_str, "update_admin_login") == 0) {
        return handle_update_admin_login(params);
    } else {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "status", "error");
        cJSON_AddStringToObject(err, "message", "未知 action");
        return err;
    } 
}

int start_socket_server(void) {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        log_message(LOG_ERR, "创建 socket 失败: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);

    unlink(SOCKET_PATH);

    char *sock_path_dup = strdup(SOCKET_PATH);
    char *sock_dir = dirname(sock_path_dup);
    struct stat st = {0};
    if (stat(sock_dir, &st) == -1) {
        if (mkdir(sock_dir, 0755) == -1) {
            log_message(LOG_ERR, "创建 socket 目录失败: %s", strerror(errno));
            free(sock_path_dup);
            close(sock_fd);
            return -1;
        }
    }
    free(sock_path_dup);

    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_message(LOG_ERR, "绑定 socket 失败: %s", strerror(errno));
        close(sock_fd);
        return -1;
    }

    if (chmod(SOCKET_PATH, 0660) < 0) {
        log_message(LOG_ERR, "设置 socket 权限失败: %s", strerror(errno));
    }

    if (listen(sock_fd, 5) < 0) {
        log_message(LOG_ERR, "监听 socket 失败: %s", strerror(errno));
        close(sock_fd);
        return -1;
    }

    log_message(LOG_INFO, "Unix Socket 服务启动，监听 %s", SOCKET_PATH);

    while (!is_shutdown_requested()) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            log_message(LOG_ERR, "内存分配失败");
            continue;
        }
        *client_fd = accept(sock_fd, (struct sockaddr*)&client_addr, &client_len);
        if (*client_fd < 0) {
            free(client_fd);
            if (is_shutdown_requested()) break;
            log_message(LOG_ERR, "accept 失败: %s", strerror(errno));
            continue;
        }
        pthread_t thread;
        if (pthread_create(&thread, NULL, thread_handler, client_fd) != 0) {
            log_message(LOG_ERR, "创建线程失败: %s", strerror(errno));
            close(*client_fd);
            free(client_fd);
        } else {
            pthread_detach(thread);
        }
    }

    log_message(LOG_INFO, "Unix Socket 服务正在关闭");
    close(sock_fd);
    unlink(SOCKET_PATH);
    return 0;
}
