#ifndef OPENVPN_H
#define OPENVPN_H

#include "common.h"

int get_openvpn_pid(int config_id);
int stop_openvpn(int config_id);
int generate_openvpn_config(int config_id, char *config_path, size_t path_size);
int start_openvpn(int config_id);
int ensure_bridge_exists(const char *bridge_name, const char *physical_if);

#endif