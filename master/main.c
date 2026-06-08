#include "common.h"
#include "logger.h"
#include "config.h"
#include "database.h"
#include "socket_server.h"
#include <signal.h>
#include "monitor.h"

#define SCHEMA_PATH "/etc/vpn-manager/schema.sql"

void sig_handler(int sig) {
    log_message(LOG_INFO, "收到信号 %d，正在清理...", sig);
    monitor_stop_all();
    exit(0);
}


int main(int argc, char *argv[]) {

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    (void)argc; (void)argv;

    if (parse_config(CONFIG_PATH) != 0) {
        exit(EXIT_FAILURE);
    }

    if (g_config.encryption_key[0]) {
        log_message(LOG_INFO, "使用配置文件中的加密密钥");
    } else {
        generate_random_key(g_config.encryption_key, sizeof(g_config.encryption_key));
        log_message(LOG_WARNING, "未配置加密密钥，已生成随机密钥（重启后将无法解密已有私钥）");
    }

    if (connect_db() != 0) {
        log_message(LOG_ERR, "数据库连接失败，退出");
        exit(EXIT_FAILURE);
    }

    if (check_db_initialized() == 0) {
        log_message(LOG_INFO, "数据库未初始化，正在初始化...");
        if (init_database(SCHEMA_PATH) != 0) {
            log_message(LOG_ERR, "数据库初始化失败，退出");
            disconnect_db();
            exit(EXIT_FAILURE);
        }
    } else {
        log_message(LOG_INFO, "数据库已初始化");
    }

    mkdir("/var/run/openvpn", 0755);
    mkdir("/var/log/vpn-manager", 0755);
    mkdir("/var/log/openvpn", 0755);

    start_socket_server();

    disconnect_db();
    return 0;
}