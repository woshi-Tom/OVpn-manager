#include "openvpn.h"
#include "config.h"
#include "database.h"
#include "logger.h"
#include "monitor.h"
#include "cert_utils.h"
#include <libpq-fe.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <openssl/dh.h>
#include <openssl/pem.h>

static void generate_tun_config(FILE *fp, int config_id);
static void generate_tap_config(FILE *fp, int config_id);

/*
static void write_cert_file(const char *path, const char *content) {
    if (!content || content[0] == '\0') return;
    FILE *fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%s\n", content);
        fclose(fp);
        chmod(path, 0600);  // 私钥文件权限600
    } else {
        log_message(LOG_ERR, "无法写入证书文件 %s: %s", path, strerror(errno));
    }
        */

static void write_cert_file(const char *path, const char *content) {
    if (!content || content[0] == '\0') return;

    // 复制内容以便清理
    char *clean_content = strdup(content);
    if (!clean_content) {
        log_message(LOG_ERR, "内存不足，无法写入证书文件 %s", path);
        return;
    }

    // 移除所有 '\r' 字符
    char *src = clean_content, *dst = clean_content;
    while (*src) {
        if (*src != '\r') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';

    // 写入文件
    FILE *fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%s", clean_content);
        // 确保文件以换行结尾（PEM 格式要求最后有换行）
        if (clean_content[strlen(clean_content)-1] != '\n') {
            fputc('\n', fp);
        }
        fclose(fp);
        chmod(path, 0600);  // 私钥必须严格权限
        log_message(LOG_DEBUG, "已写入证书文件 %s (%zu 字节)", path, strlen(clean_content));
    } else {
        log_message(LOG_ERR, "无法写入证书文件 %s: %s", path, strerror(errno));
    }

    free(clean_content);
}


static void write_push_routes(FILE *fp, const char *routes_raw) {
    if (!routes_raw || routes_raw[0] == '\0' || strcmp(routes_raw, "{}") == 0) {
        return;
    }
    char *str = strdup(routes_raw);
    if (!str) return;
    int len = strlen(str);
    if (str[0] == '{' && str[len-1] == '}') {
        str[len-1] = '\0';
        char *content = str + 1;
        char *saveptr;
        char *token = strtok_r(content, ",", &saveptr);
        while (token) {
            while (*token == ' ') token++;
            fprintf(fp, "push \"route %s\"\n", token);
            token = strtok_r(NULL, ",", &saveptr);
        }
    }
    free(str);
}

static void cidr_to_netmask(const char *cidr, char *network, size_t net_size, char *netmask, size_t mask_size) {
    if (!cidr || !network || !netmask) {
        if (network) network[0] = '\0';
        if (netmask) netmask[0] = '\0';
        return;
    }
    
    network[0] = '\0';
    netmask[0] = '\0';
    
    // 检查是否是纯 netmask 格式 (如 255.255.255.0)
    if (strchr(cidr, '/') == NULL) {
        // 没有斜杠，可能是纯 netmask 格式或 IP
        // 如果包含点且像 netmask，假设它是 netmask
        if (strchr(cidr, '.') && atoi(cidr) > 0) {
            // 假设这是纯 netmask，network 留空让 OpenVPN 自动处理
            strncpy(netmask, cidr, mask_size-1);
            netmask[mask_size-1] = '\0';
            return;
        }
        // 否则当作 network
        strncpy(network, cidr, net_size-1);
        network[net_size-1] = '\0';
        return;
    }
    
    // 假设 cidr 格式如 "10.8.0.0/24"
    char *slash = strchr(cidr, '/');
    if (!slash) {
        strncpy(network, cidr, net_size-1);
        network[net_size-1] = '\0';
        return;
    }
    size_t net_len = slash - cidr;
    if (net_len >= net_size) net_len = net_size - 1;
    strncpy(network, cidr, net_len);
    network[net_len] = '\0';
    
    int bits = atoi(slash + 1);
    if (bits < 0 || bits > 32) {
        return;
    }
    uint32_t nm = 0;
    for (int i = 0; i < bits; i++) {
        nm |= (1 << (31 - i));
    }
    snprintf(netmask, mask_size, "%d.%d.%d.%d",
             (nm >> 24) & 0xFF,
             (nm >> 16) & 0xFF,
             (nm >> 8) & 0xFF,
             nm & 0xFF);
}

int get_openvpn_pid(int config_id) {
    char pid_path[256];
    snprintf(pid_path, sizeof(pid_path), "/var/run/openvpn/vpn-%d.pid", config_id);
    FILE *fp = fopen(pid_path, "r");
    if (!fp) return -1;
    int pid;
    if (fscanf(fp, "%d", &pid) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return pid;
}

int stop_openvpn(int config_id) {
    int pid = get_openvpn_pid(config_id);
    if (pid <= 0) {
        log_message(LOG_INFO, "没有找到 config_id %d 的运行中进程", config_id);
        monitor_stop(config_id);
        return 0;
    }
    
    // 先尝试 SIGTERM
    if (kill(pid, SIGTERM) == 0) {
        log_message(LOG_INFO, "已发送 SIGTERM 到进程 %d", pid);
        
        // 等待进程退出，最多等待 3 秒
        for (int i = 0; i < 30; i++) {
            if (kill(pid, 0) != 0) {
                // 进程已退出
                break;
            }
            usleep(100000); // 100ms
        }
        
        // 如果进程仍然存在，尝试 SIGKILL
        if (kill(pid, 0) == 0) {
            log_message(LOG_WARNING, "进程 %d 未响应 SIGTERM，发送 SIGKILL", pid);
            kill(pid, SIGKILL);
            usleep(100000);
        }
        
        monitor_stop(config_id);
        return 0;
    } else {
        log_message(LOG_ERR, "停止进程 %d 失败: %s", pid, strerror(errno));
        return -1;
    }
}

int generate_openvpn_config(int config_id, char *config_path, size_t path_size) {
    char sql[2048];
    
    snprintf(sql, sizeof(sql),
             "SELECT c.mode, c.proto, c.port, c.remote, c.ca_cert, c.server_cert, c.server_key "
             "FROM vpn_config c WHERE c.id = %d", config_id);

    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        log_message(LOG_ERR, "查询 config_id %d 失败: %s", config_id, PQerrorMessage(g_conn));
        PQclear(res);
        return -1;
    }

    const char *mode = PQgetvalue(res, 0, 0);
    const char *proto = PQgetvalue(res, 0, 1);
    const char *port = PQgetvalue(res, 0, 2);
    const char *remote = PQgetvalue(res, 0, 3);
    const char *ca_cert = PQgetvalue(res, 0, 4);
    const char *server_cert = PQgetvalue(res, 0, 5);
    const char *server_key = PQgetvalue(res, 0, 6);

    (void)remote;  // 保留用于日志记录

    write_cert_file("/etc/openvpn/ca.crt", ca_cert);
    write_cert_file("/etc/openvpn/server.crt", server_cert);
    
    char *decrypted_server_key = NULL;
    if (server_key && strlen(server_key) > 0) {
        EVP_PKEY *pkey = decrypt_private_key(server_key, NULL);
        if (pkey) {
            BIO *bio = BIO_new(BIO_s_mem());
            if (PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL)) {
                BUF_MEM *buf;
                BIO_get_mem_ptr(bio, &buf);
                decrypted_server_key = strndup(buf->data, buf->length);
                BIO_free(bio);
            }
            EVP_PKEY_free(pkey);
        }
        if (!decrypted_server_key) {
            log_message(LOG_ERR, "解密服务器私钥失败，使用加密版本");
            decrypted_server_key = strdup(server_key);
        }
    }
    write_cert_file("/etc/openvpn/server.key", decrypted_server_key);
    free(decrypted_server_key);

    if (access("/etc/openvpn/dh.pem", F_OK) != 0) {
        log_message(LOG_INFO, "dh.pem 不存在，正在生成...");
        FILE *dh_fp = fopen("/etc/openvpn/dh.pem", "w");
        if (dh_fp) {
            DH *dh = DH_generate_parameters(2048, 2, NULL, NULL);
            if (dh) {
                if (PEM_write_DHparams(dh_fp, dh)) {
                    log_message(LOG_INFO, "dh.pem 生成成功");
                } else {
                    log_message(LOG_ERR, "写入 dh.pem 失败");
                    fclose(dh_fp);
                    DH_free(dh);
                    PQclear(res);
                    return -1;
                }
                DH_free(dh);
            } else {
                log_message(LOG_ERR, "生成 DH 参数失败");
                fclose(dh_fp);
                PQclear(res);
                return -1;
            }
            fclose(dh_fp);
        } else {
            log_message(LOG_ERR, "无法创建 dh.pem: %s", strerror(errno));
            PQclear(res);
            return -1;
        }
    }

    snprintf(config_path, path_size, "/etc/openvpn/vpn-%d.conf", config_id);

    FILE *fp = fopen(config_path, "w");
    if (!fp) {
        log_message(LOG_ERR, "无法创建配置文件 %s: %s", config_path, strerror(errno));
        PQclear(res);
        return -1;
    }

    fprintf(fp, "port %s\n", port);
    fprintf(fp, "proto %s\n", proto);
    fprintf(fp, "ca /etc/openvpn/ca.crt\n");
    fprintf(fp, "cert /etc/openvpn/server.crt\n");
    fprintf(fp, "key /etc/openvpn/server.key\n");
    fprintf(fp, "dh /etc/openvpn/dh.pem\n");

    if (strcmp(mode, "tun") == 0) {
        generate_tun_config(fp, config_id);
    } else if (strcmp(mode, "tap") == 0) {
        generate_tap_config(fp, config_id);
    }

    fprintf(fp, "keepalive 10 120\n");
    fprintf(fp, "persist-key\n");
    fprintf(fp, "persist-tun\n");
    fprintf(fp, "status /var/log/openvpn/status-%d.log\n", config_id);
    fprintf(fp, "log-append /var/log/openvpn/openvpn-%d.log\n", config_id);
    fprintf(fp, "verb 3\n");
    fprintf(fp, "management /var/run/openvpn/management-%d.sock unix\n", config_id);
    fprintf(fp, "writepid /var/run/openvpn/vpn-%d.pid\n", config_id);

    fclose(fp);
    log_message(LOG_INFO, "配置文件已生成: %s", config_path);
    
    // 调试：读取并打印配置文件内容
    FILE *debug_fp = fopen(config_path, "r");
    if (debug_fp) {
        char line[256];
        log_message(LOG_INFO, "=== 配置文件内容 ===");
        while (fgets(line, sizeof(line), debug_fp)) {
            log_message(LOG_INFO, "%s", line);
        }
        log_message(LOG_INFO, "=== 配置文件结束 ===");
        fclose(debug_fp);
    }
    
    PQclear(res);
    return 0;
}

static void generate_tun_config(FILE *fp, int config_id) {
    char sql[2048];
    snprintf(sql, sizeof(sql),
             "SELECT t.server_ip, t.subnet_mask, t.push_dns, t.enable_nat, "
             "       t.push_redirect_gateway, t.push_routes "
             "FROM vpn_config_tun t WHERE t.config_id = %d", config_id);

    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        log_message(LOG_ERR, "查询 TUN 配置失败: %s", PQerrorMessage(g_conn));
        PQclear(res);
        return;
    }

    const char *server_ip = PQgetvalue(res, 0, 0);
    const char *subnet_mask = PQgetvalue(res, 0, 1);
    log_message(LOG_INFO, "generate_tun_config: db_server_ip=%s, db_subnet_mask=%s", server_ip ? server_ip : "NULL", subnet_mask ? subnet_mask : "NULL");
    const char *push_dns_raw = PQgetvalue(res, 0, 2);
    const char *enable_nat = PQgetvalue(res, 0, 3);
    const char *push_redirect_gateway = PQgetvalue(res, 0, 4);
    const char *push_routes_raw = PQgetvalue(res, 0, 5);

    fprintf(fp, "dev tun\n");
    fprintf(fp, "topology subnet\n");
    fprintf(fp, "cipher AES-256-GCM\n");

    if (server_ip && subnet_mask && subnet_mask[0] != '\0') {
        char network[64] = {0}, netmask[64] = {0};
        cidr_to_netmask(subnet_mask, network, sizeof(network), netmask, sizeof(netmask));
        if (netmask[0] != '\0' && network[0] != '\0') {
            // CIDR 格式: 10.8.0.0/24
            fprintf(fp, "server %s %s\n", network, netmask);
        } else if (netmask[0] != '\0') {
            // 纯 netmask 格式: 255.255.255.0，使用 server_ip
            fprintf(fp, "server %s %s\n", server_ip, netmask);
        } else {
            // 没有有效的掩码，使用原始值
            fprintf(fp, "server %s %s\n", server_ip, subnet_mask);
        }
    }

    if (push_dns_raw && strcmp(push_dns_raw, "{}") != 0) {
        char *dns_str = strdup(push_dns_raw);
        if (dns_str) {
            int len = strlen(dns_str);
            if (dns_str[0] == '{' && dns_str[len-1] == '}') {
                dns_str[len-1] = '\0';
                char *content = dns_str + 1;
                char *saveptr;
                char *token = strtok_r(content, ",", &saveptr);
                while (token) {
                    while (*token == ' ') token++;
                    fprintf(fp, "push \"dhcp-option DNS %s\"\n", token);
                    token = strtok_r(NULL, ",", &saveptr);
                }
            }
            free(dns_str);
        }
    }

    if (push_redirect_gateway && strcmp(push_redirect_gateway, "t") == 0) {
        fprintf(fp, "push \"redirect-gateway def1\"\n");
    }

    if (push_routes_raw && strcmp(push_routes_raw, "{}") != 0) {
        write_push_routes(fp, push_routes_raw);
    }

    if (enable_nat && strcmp(enable_nat, "t") == 0) {
        fprintf(fp, "; NAT 功能已启用，请确保 iptables 规则已配置\n");
    }

    PQclear(res);
}

static void generate_tap_config(FILE *fp, int config_id) {
    char sql[2048];
    snprintf(sql, sizeof(sql),
             "SELECT t.bridge_name, t.physical_if, t.dhcp_mode, t.server_ip, t.subnet_mask "
             "FROM vpn_config_tap t WHERE t.config_id = %d", config_id);

    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        log_message(LOG_ERR, "查询 TAP 配置失败: %s", PQerrorMessage(g_conn));
        PQclear(res);
        return;
    }

    const char *bridge_name = PQgetvalue(res, 0, 0);
    const char *physical_if = PQgetvalue(res, 0, 1);
    const char *dhcp_mode = PQgetvalue(res, 0, 2);
    const char *server_ip = PQgetvalue(res, 0, 3);
    const char *subnet_mask = PQgetvalue(res, 0, 4);

    fprintf(fp, "dev tap\n");
    fprintf(fp, "cipher AES-256-GCM\n");

    /* TAP 模式允许多播和广播，客户端可以互相发现 */
    fprintf(fp, "client-to-client\n");

    if (bridge_name && strlen(bridge_name) > 0) {
        /* 使用 bridge 指令替代 dev-node，OpenVPN 自动管理网桥 */
        if (physical_if && strlen(physical_if) > 0) {
            fprintf(fp, "server-bridge %s %s %s %s\n",
                    server_ip && strlen(server_ip) > 0 ? server_ip : "192.168.10.1",
                    subnet_mask && strlen(subnet_mask) > 0 ? subnet_mask : "255.255.255.0",
                    "192.168.10.128", "192.168.10.254");
            ensure_bridge_exists(bridge_name, physical_if);
        } else {
            fprintf(fp, "dev-node %s\n", bridge_name);
        }
    }

    if (dhcp_mode && strcmp(dhcp_mode, "server") == 0) {
        fprintf(fp, "mode server\n");
        fprintf(fp, "tls-server\n");
        if (server_ip && subnet_mask && strlen(server_ip) > 0 && strlen(subnet_mask) > 0) {
            /* server-bridge 指令格式: server_ip netmask pool_start pool_end */
            /* 解析子网以确定地址池范围 */
            struct in_addr addr;
            if (inet_aton(server_ip, &addr)) {
                uint32_t ip = ntohl(addr.s_addr);
                /* 池范围: server_ip+100 ~ server_ip+200 */
                uint32_t pool_start = (ip & 0xFFFFFF00) | 100;
                uint32_t pool_end = (ip & 0xFFFFFF00) | 200;
                char start_str[32], end_str[32];
                struct in_addr sa, ea;
                sa.s_addr = htonl(pool_start);
                ea.s_addr = htonl(pool_end);
                snprintf(start_str, sizeof(start_str), "%s", inet_ntoa(sa));
                snprintf(end_str, sizeof(end_str), "%s", inet_ntoa(ea));
                fprintf(fp, "server-bridge %s %s %s %s\n",
                        server_ip, subnet_mask, start_str, end_str);
            }
        }
        fprintf(fp, "ifconfig-pool-persist /var/log/openvpn/ipp-%d.txt\n", config_id);
    } else if (dhcp_mode && strcmp(dhcp_mode, "relay") == 0) {
        log_message(LOG_INFO, "TAP 模式中继模式需要额外配置 DHCP 中继服务器");
        fprintf(fp, "; DHCP relay mode - requires external DHCP relay agent\n");
        if (server_ip && subnet_mask && strlen(server_ip) > 0 && strlen(subnet_mask) > 0) {
            fprintf(fp, "ifconfig %s %s\n", server_ip, subnet_mask);
        }
    } else {
        /* none: 静态 IP 模式 */
        if (server_ip && subnet_mask && strlen(server_ip) > 0 && strlen(subnet_mask) > 0) {
            fprintf(fp, "ifconfig %s %s\n", server_ip, subnet_mask);
            /* 计算客户端地址池: 从 server_ip+100 开始 */
            struct in_addr addr;
            if (inet_aton(server_ip, &addr)) {
                uint32_t ip = ntohl(addr.s_addr);
                uint32_t pool_start = (ip & 0xFFFFFF00) | 100;
                uint32_t pool_end = (ip & 0xFFFFFF00) | 200;
                struct in_addr sa, ea;
                char start_str[32], end_str[32];
                sa.s_addr = htonl(pool_start);
                ea.s_addr = htonl(pool_end);
                snprintf(start_str, sizeof(start_str), "%s", inet_ntoa(sa));
                snprintf(end_str, sizeof(end_str), "%s", inet_ntoa(ea));
                fprintf(fp, "ifconfig-pool %s %s\n", start_str, end_str);
            }
        }
    }

    /* 启用 TAP 模式的多播支持 */
    fprintf(fp, "topology subnet\n");

    PQclear(res);
}

int ensure_bridge_exists(const char *bridge_name, const char *physical_if) {
    // 检查网桥是否存在
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip link show %s >/dev/null 2>&1", bridge_name);
    if (system(cmd) == 0) {
        return 0; // 已存在
    }
    // 创建网桥
    snprintf(cmd, sizeof(cmd), "ip link add name %s type bridge", bridge_name);
    if (system(cmd) != 0) {
        log_message(LOG_ERR, "创建网桥 %s 失败", bridge_name);
        return -1;
    }
    // 将物理网卡加入网桥
    snprintf(cmd, sizeof(cmd), "ip link set dev %s master %s", physical_if, bridge_name);
    if (system(cmd) != 0) {
        log_message(LOG_ERR, "将 %s 加入网桥 %s 失败", physical_if, bridge_name);
        return -1;
    }
    // 启用网桥
    snprintf(cmd, sizeof(cmd), "ip link set dev %s up", bridge_name);
    system(cmd);
    return 0;
}


/*
int start_openvpn(int config_id) {
    stop_openvpn(config_id);  // 停止可能存在的旧进程

    char config_path[256];
    if (generate_openvpn_config(config_id, config_path, sizeof(config_path)) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程
        setsid();

        // 重定向 stdout/stderr 到 OpenVPN 日志文件
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "/var/log/openvpn/openvpn-%d.log", config_id);
        int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        } else {
            // 如果无法打开日志文件，重定向到 /dev/null
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
        }
        freopen("/dev/null", "r", stdin);

        // 测试是否此处传参有错误
        execl(g_config.openvpn.binary, "openvpn", "--config", config_path, NULL);
        // 如果执行到这里，说明 execl 失败
        exit(1);
    } else if (pid > 0) {
        log_message(LOG_INFO, "OpenVPN 进程已启动，PID=%d", pid);

        // 等待一小段时间，检查进程是否立即退出
        usleep(500000);
        int status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            // 子进程已退出
            if (WIFEXITED(status)) {
                log_message(LOG_ERR, "OpenVPN 进程启动失败，退出码: %d", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                log_message(LOG_ERR, "OpenVPN 进程被信号 %d 终止", WTERMSIG(status));
            }
            return -1;
        } else if (ret == 0) {
            // 进程仍在运行，正常
            if (monitor_start(config_id) != 0) {
                log_message(LOG_WARNING, "启动监控线程失败，config_id=%d", config_id);
            }
            return 0;
        } else {
            // waitpid 出错
            log_message(LOG_ERR, "waitpid 失败: %s", strerror(errno));
            return -1;
        }
    } else {
        log_message(LOG_ERR, "fork 失败: %s", strerror(errno));
        return -1;
    }
}
    */

int start_openvpn(int config_id) {
    log_message(LOG_INFO, "OpenVPN binary path: %s", g_config.openvpn.binary);
    if (access(g_config.openvpn.binary, X_OK) != 0) {
        log_message(LOG_ERR, "OpenVPN 二进制文件不存在或不可执行: %s", g_config.openvpn.binary);
        return -1;
    }
    stop_openvpn(config_id);

    char config_path[256];
    if (generate_openvpn_config(config_id, config_path, sizeof(config_path)) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程
        setsid();

        // 关闭所有从父进程继承的文件描述符，避免干扰
        int maxfd = sysconf(_SC_OPEN_MAX);
        for (int i = 3; i < maxfd; i++) {
            close(i);
        }

        // 重定向stdin到/dev/null
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > 2) close(null_fd);
        }

        // 记录启动时间戳到OpenVPN日志文件（可选）
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "/var/log/openvpn/openvpn-%d.log", config_id);
        int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd >= 0) {
            dprintf(log_fd, "=== OpenVPN started by core at %ld ===\n", time(NULL));
            close(log_fd);
        }

        execl(g_config.openvpn.binary, "openvpn", "--config", config_path, NULL);
        // 如果执行到这里，说明execl失败
        int err = errno;
        dprintf(STDERR_FILENO, "Subprocess started at %ld\n", time(NULL));
        dprintf(STDERR_FILENO, "execl failed: %s (errno=%d)\n", strerror(err), err);
        int err_fd = open("/tmp/openvpn-exec-error.log", O_WRONLY|O_CREAT|O_APPEND, 0644);
        if (err_fd >= 0) {
            dprintf(err_fd, "execl failed: %s\n", strerror(err));
            close(err_fd);
        }
        exit(1);
    } else if (pid > 0) {
        log_message(LOG_INFO, "OpenVPN 进程已启动，PID=%d", pid);

        // 等待一段时间，让OpenVPN完成初始化
        usleep(500000);
        int status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            // 子进程已退出
            if (WIFEXITED(status)) {
                log_message(LOG_ERR, "OpenVPN 进程启动失败，退出码: %d", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                log_message(LOG_ERR, "OpenVPN 进程被信号 %d 终止", WTERMSIG(status));
            }
            return -1;
        } else if (ret == 0) {
            // 进程仍在运行
            log_message(LOG_INFO, "OpenVPN 进程持续运行");
            if (monitor_start(config_id) != 0) {
                log_message(LOG_WARNING, "启动监控线程失败，config_id=%d", config_id);
            }
            return 0;
        } else {
            log_message(LOG_ERR, "waitpid 失败: %s", strerror(errno));
            return -1;
        }
    } else {
        log_message(LOG_ERR, "fork 失败: %s", strerror(errno));
        return -1;
    }
}