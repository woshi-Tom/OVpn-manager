#ifndef CA_HANDLER_H
#define CA_HANDLER_H

#include <cjson/cJSON.h>

cJSON* handle_generate_ca_cert(const cJSON *params);
cJSON* handle_sign_server_cert(const cJSON *params);
cJSON* handle_get_ca_cert(const cJSON *params);
cJSON* handle_delete_ca_cert(const cJSON *params);

#endif
