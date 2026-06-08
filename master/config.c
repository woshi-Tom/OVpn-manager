#include "config.h"
#include "logger.h"
#include <yaml.h>
#include <openssl/rand.h>

core_config_t g_config;

void safe_strcpy(char *dest, const char *src, size_t dest_size) {
    if (dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

void generate_random_key(char *key, size_t len) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    unsigned char random_bytes[32];
    
    if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1) {
        // 如果随机数生成失败，使用时间戳作为后备
        snprintf(key, len, "fallback-key-%ld", time(NULL));
        return;
    }
    
    for (size_t i = 0; i < len - 1 && i < sizeof(random_bytes); i++) {
        key[i] = charset[random_bytes[i] % (sizeof(charset) - 1)];
    }
    key[len - 1] = '\0';
}

int parse_config(const char *path) {
    FILE *fh = fopen(path, "r");
    if (!fh) {
        log_message(LOG_ERR, "无法打开配置文件 %s: %s", path, strerror(errno));
        return -1;
    }

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        log_message(LOG_ERR, "初始化 YAML 解析器失败");
        fclose(fh);
        return -1;
    }

    yaml_parser_set_input_file(&parser, fh);

    // 状态变量
    int depth = 0;
    int in_section = 0;         // 0=不在任何感兴趣节, 1=database, 2=log, 3=openvpn, 4=pki
    int target_depth = 0;
    int expect_key = 0;
    char current_key[256] = {0};
    char current_section[64] = {0}; // 记录当前进入的顶级节名称

    // 设置默认值
    g_config.db_port = 5432;
    g_config.db_host[0] = g_config.db_name[0] = g_config.db_user[0] = g_config.db_password[0] = '\0';
    g_config.encryption_key[0] = '\0';
    safe_strcpy(g_config.log.level, "info", sizeof(g_config.log.level));
    g_config.log.level_value = 1;  // 默认 info
    safe_strcpy(g_config.log.file, "/var/log/vpn-manager/core.log", sizeof(g_config.log.file));
    g_config.openvpn.binary[0] = g_config.openvpn.config_dir[0] = g_config.openvpn.management_socket[0] = '\0';
    g_config.pki.ca_key[0] = '\0';
    g_config.master_key_file[0] = '\0';
    g_config.listen_port = 0;  // 0 = 默认使用 Unix Socket
    safe_strcpy(g_config.listen_host, "0.0.0.0", sizeof(g_config.listen_host));

    yaml_token_t token;
    int done = 0;
    while (!done) {
        if (!yaml_parser_scan(&parser, &token)) {
            log_message(LOG_ERR, "YAML 扫描失败");
            yaml_parser_delete(&parser);
            fclose(fh);
            return -1;
        }

        switch (token.type) {
            case YAML_KEY_TOKEN:
                expect_key = 1;
                break;
            case YAML_VALUE_TOKEN:
                expect_key = 0;
                break;
            case YAML_SCALAR_TOKEN: {
                const char *value = (const char*)token.data.scalar.value;
                if (in_section) {
                    // 在某个感兴趣的节内
                    if (expect_key) {
                        safe_strcpy(current_key, value, sizeof(current_key));
                    } else {
                        // 根据当前节赋值
                        if (in_section == 1) { // database
                            if (strcmp(current_key, "host") == 0)
                                safe_strcpy(g_config.db_host, value, sizeof(g_config.db_host));
                            else if (strcmp(current_key, "port") == 0)
                                g_config.db_port = atoi(value);
                            else if (strcmp(current_key, "name") == 0)
                                safe_strcpy(g_config.db_name, value, sizeof(g_config.db_name));
                            else if (strcmp(current_key, "user") == 0)
                                safe_strcpy(g_config.db_user, value, sizeof(g_config.db_user));
                            else if (strcmp(current_key, "password") == 0)
                                safe_strcpy(g_config.db_password, value, sizeof(g_config.db_password));
                        } else if (in_section == 2) { // log
                            if (strcmp(current_key, "level") == 0) {
                                safe_strcpy(g_config.log.level, value, sizeof(g_config.log.level));
                                if (strcmp(value, "debug") == 0) g_config.log.level_value = 0;
                                else if (strcmp(value, "info") == 0) g_config.log.level_value = 1;
                                else if (strcmp(value, "warn") == 0) g_config.log.level_value = 2;
                                else if (strcmp(value, "error") == 0) g_config.log.level_value = 3;
                                else g_config.log.level_value = 1;
                            } else if (strcmp(current_key, "file") == 0) {
                                safe_strcpy(g_config.log.file, value, sizeof(g_config.log.file));
                            }
                        } else if (in_section == 3) { // openvpn
                            if (strcmp(current_key, "binary") == 0)
                                safe_strcpy(g_config.openvpn.binary, value, sizeof(g_config.openvpn.binary));
                            else if (strcmp(current_key, "config_dir") == 0)
                                safe_strcpy(g_config.openvpn.config_dir, value, sizeof(g_config.openvpn.config_dir));
                            else if (strcmp(current_key, "management_socket") == 0)
                                safe_strcpy(g_config.openvpn.management_socket, value, sizeof(g_config.openvpn.management_socket));
                        } else if (in_section == 4) { // pki
                            if (strcmp(current_key, "ca_key") == 0)
                                safe_strcpy(g_config.pki.ca_key, value, sizeof(g_config.pki.ca_key));
                        }
                    }
                    } else {
                        // 不在任何节内，检查顶层键
                        if (expect_key) {
                            safe_strcpy(current_section, value, sizeof(current_section));
                            // 处理单独的顶层键 master_key_file
                            if (strcmp(value, "master_key_file") == 0) {
                                // 等待下一个值
                            } else if (strcmp(value, "encryption_key") == 0) {
                                // 等待下一个值
                            } else if (strcmp(value, "listen_port") == 0) {
                                // 等待下一个值
                            } else if (strcmp(value, "listen_host") == 0) {
                                // 等待下一个值
                            }
                        } else {
                            // 处理之前记录的顶层键的值
                            if (strcmp(current_section, "master_key_file") == 0) {
                                safe_strcpy(g_config.master_key_file, value, sizeof(g_config.master_key_file));
                            } else if (strcmp(current_section, "encryption_key") == 0) {
                                safe_strcpy(g_config.encryption_key, value, sizeof(g_config.encryption_key));
                            } else if (strcmp(current_section, "listen_port") == 0) {
                                g_config.listen_port = atoi(value);
                            } else if (strcmp(current_section, "listen_host") == 0) {
                                safe_strcpy(g_config.listen_host, value, sizeof(g_config.listen_host));
                            }
                        }
                    }
                // fprintf(stderr, "SCALAR: %s, depth=%d, in_section=%d, expect_key=%d, current_section=%s, current_key=%s\n",value, depth, in_section, expect_key, current_section, current_key); // 调试开启
                break;
            }
            case YAML_BLOCK_MAPPING_START_TOKEN:
            case YAML_FLOW_MAPPING_START_TOKEN:
                depth++;
                if (!in_section) {
                    if (strcmp(current_section, "database") == 0) {
                        in_section = 1;
                        target_depth = depth;
                    } else if (strcmp(current_section, "log") == 0) {
                        in_section = 2;
                        target_depth = depth;
                    } else if (strcmp(current_section, "openvpn") == 0) {
                        in_section = 3;
                        target_depth = depth;
                    } else if (strcmp(current_section, "pki") == 0) {
                        in_section = 4;
                        target_depth = depth;
                    }
                }
                break;
            case YAML_BLOCK_END_TOKEN:
            case YAML_FLOW_MAPPING_END_TOKEN:
                if (in_section && depth == target_depth) {
                    in_section = 0;
                }
                depth--;
                break;
            case YAML_STREAM_END_TOKEN:
                done = 1;
                break;
            default:
                break;
        }
        yaml_token_delete(&token);
    }

    yaml_parser_delete(&parser);
    fclose(fh);

    // 检查必要字段
    if (g_config.db_host[0] == '\0' || g_config.db_name[0] == '\0' ||
        g_config.db_user[0] == '\0' || g_config.db_password[0] == '\0') {
        log_message(LOG_ERR, "配置文件中缺少必要的数据库连接信息");
        return -1;
    }
    if (g_config.openvpn.binary[0] == '\0') {
        log_message(LOG_ERR, "配置文件中缺少 openvpn.binary 路径");
        return -1;
    }

    log_message(LOG_INFO, "配置解析成功");
    log_message(LOG_DEBUG, "配置详情: host=%s, port=%d, dbname=%s, user=%s, openvpn=%s",
                g_config.db_host, g_config.db_port, g_config.db_name, g_config.db_user,
                g_config.openvpn.binary);
    return 0;
}