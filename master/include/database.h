#ifndef DATABASE_H
#define DATABASE_H

#include "common.h"

extern PGconn *g_conn;

int connect_db(void);
void disconnect_db(void);
int check_db_initialized(void);
int init_database(const char *schema_path);
void log_system_event(const char *level, const char *source, const char *msg);

#endif