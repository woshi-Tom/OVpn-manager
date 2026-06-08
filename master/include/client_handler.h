#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#include <cjson/cJSON.h>

cJSON* handle_gen_client_cert(const cJSON *params);
cJSON* handle_get_client_config(const cJSON *params);
cJSON* handle_change_client_password(const cJSON *params);
cJSON* handle_revoke_client_cert(const cJSON *params);

#endif
