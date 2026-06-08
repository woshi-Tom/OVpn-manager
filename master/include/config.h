#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

typedef struct {
    char db_host[256];
    int db_port;
    char db_name[256];
    char db_user[256];
    char db_password[256];
    char master_key_file[256];
    char encryption_key[64];  // 用于加密私钥的密钥
    struct {
        char level[16];
        char file[256];
        int level_value;  // 0=debug, 1=info, 2=warn, 3=error
    } log;
    struct {
        char binary[256];
        char config_dir[256];
        char management_socket[256];
    } openvpn;
    struct {
        char ca_key[256];
    } pki;
} core_config_t;

extern core_config_t g_config;  // 全局配置变量

int parse_config(const char *path);
void safe_strcpy(char *dest, const char *src, size_t dest_size);
void generate_random_key(char *key, size_t len);

#endif