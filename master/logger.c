#include "logger.h"
#include "config.h"
#include <ctype.h>

extern core_config_t g_config;

static int should_log(int level) {
    int msg_level = 3;  // default error
    if (level == LOG_DEBUG) msg_level = 0;
    else if (level == LOG_INFO) msg_level = 1;
    else if (level == LOG_WARNING) msg_level = 2;
    else if (level == LOG_ERR) msg_level = 3;
    
    return msg_level >= g_config.log.level_value;
}

static void mask_sensitive(char *msg) {
    if (!msg) return;
    
    char *p = msg;
    while (*p) {
        if (strncmp(p, "-----BEGIN", 10) == 0) {
            char *end = strstr(p, "-----END");
            if (end) {
                end += 5;
                size_t len = strlen(p);
                if (len > 20) {
                    snprintf(p, len, "[PRIVATE KEY REDACTED]%s", end);
                }
            }
        }
        if (strncmp(p, "password", 8) == 0) {
            char *q = p + 8;
            while (*q && (*q == ' ' || *q == ':' || *q == '=')) q++;
            if (*q && !isspace(*q)) {
                char *r = strchr(q, ' ');
                if (!r) r = strchr(q, ',');
                if (!r) r = q + strlen(q);
                if (r - q > 0 && r - q < 20) {
                    size_t remaining = strlen(r);
                    memmove(q + 8, r, remaining + 1);
                    q[0] = '['; q[1] = 'P'; q[2] = 'A'; q[3] = 'S';
                    q[4] = 'S'; q[5] = 'W'; q[6] = 'O'; q[7] = 'R';
                    q[8] = 'D'; q[9] = ']'; q[10] = '\0';
                }
            }
        }
        p++;
    }
}

void log_message(int level, const char *format, ...) {
    va_list args, args_copy;
    va_start(args, format);
    va_copy(args_copy, args);
    
    if (!should_log(level)) {
        va_end(args_copy);
        va_end(args);
        return;
    }
    
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args_copy);
    
    if (g_config.log.level_value > 0) {
        mask_sensitive(buffer);
    }
    
    FILE *log_fp = fopen(LOG_FILE, "a");
    if (log_fp) {
        time_t now = time(NULL);
        char timebuf[64];
        struct tm *tm_info = localtime(&now);
        if (tm_info) {
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
            fprintf(log_fp, "[%s] ", timebuf);
        }
        fprintf(log_fp, "%s\n", buffer);
        fclose(log_fp);
    }
    
    openlog("vpn-core", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(level, "%s", buffer);
    closelog();
    
    va_end(args_copy);
    va_end(args);
}