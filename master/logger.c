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
                /* find the end of the -----END ...----- line */
                char *line_end = strchr(end, '\n');
                if (!line_end) line_end = end + strlen(end);
                else line_end++; /* include the newline */

                const char *redacted = "[PRIVATE KEY REDACTED]\n";
                size_t redacted_len = strlen(redacted);
                size_t block_len = line_end - p;

                if (redacted_len <= block_len) {
                    memcpy(p, redacted, redacted_len);
                    memmove(p + redacted_len, line_end, strlen(line_end) + 1);
                }
            }
        }
        if (strncmp(p, "password", 8) == 0) {
            char *q = p + 8;
            while (*q && (*q == ' ' || *q == ':' || *q == '=')) q++;
            if (*q && !isspace((unsigned char)*q)) {
                char *r = strchr(q, ' ');
                if (!r) r = strchr(q, ',');
                if (!r) r = q + strlen(q);
                if (r - q > 0 && r - q < 20) {
                    memmove(q + 10, r, strlen(r) + 1);
                    memcpy(q, "[PASSWORD]", 10);
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