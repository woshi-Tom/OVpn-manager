#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <libpq-fe.h>
#include <yaml.h>
#include <cjson/cJSON.h>

#define CONFIG_PATH "/etc/vpn-manager/core.yaml"
#define SOCKET_PATH "/var/run/vpn-manager/core.sock"
#define LOG_FILE "/var/log/vpn-manager/core.log"
#define MAX_BUFFER 65536

#endif