#ifndef MONITOR_H
#define MONITOR_H

#include "common.h"

// 启动监控线程，config_id 用于标识哪个 OpenVPN 实例
int monitor_start(int config_id);

// 停止监控线程
void monitor_stop(int config_id);

// 停止所有监控线程及对应的 OpenVPN 进程
void monitor_stop_all(void);  

// 踢出客户端（通过 common_name 或 IP:port）
int kick_client(int config_id, const char *target);

#endif