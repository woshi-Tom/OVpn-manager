#include "client_handler.h"
#include "cert_utils.h"
#include "logger.h"
#include "database.h"

#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/bn.h>

static cJSON* create_error_response(const char *message) {
    cJSON *err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "status", "error");
    cJSON_AddStringToObject(err, "message", message);
    return err;
}

cJSON* handle_gen_client_cert(const cJSON *params) {
    const cJSON *username = cJSON_GetObjectItem(params, "username");
    const cJSON *email = cJSON_GetObjectItem(params, "email");
    const cJSON *config_id = cJSON_GetObjectItem(params, "config_id");
    const cJSON *assigned_ip = cJSON_GetObjectItem(params, "assigned_ip");
    const cJSON *client_name = cJSON_GetObjectItem(params, "client_name");
    const cJSON *cert_cn = cJSON_GetObjectItem(params, "cert_cn");
    const cJSON *valid_days = cJSON_GetObjectItem(params, "valid_days");

    if (!username || !cJSON_IsString(username) || !config_id || !cJSON_IsNumber(config_id)) {
        return create_error_response("缺少必要参数");
    }

    const char *username_str = username->valuestring;
    const char *email_str = email && cJSON_IsString(email) ? email->valuestring : "";
    const char *client_name_str = client_name && cJSON_IsString(client_name) ? client_name->valuestring : "";
    int config_id_val = config_id->valueint;
    const char *assigned_ip_str = assigned_ip && cJSON_IsString(assigned_ip) ? assigned_ip->valuestring : NULL;
    const char *cert_cn_str = cert_cn && cJSON_IsString(cert_cn) ? cert_cn->valuestring : NULL;
    int days = valid_days && cJSON_IsNumber(valid_days) ? valid_days->valueint : 365;

    char sql[4096];
    // 先从 vpn_config 获取 ca_cert 和 ca_fingerprint
    snprintf(sql, sizeof(sql), 
        "SELECT ca_cert, server_cert, remote, port, ca_fingerprint, proto FROM vpn_config WHERE id = %d", config_id_val);
    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return create_error_response("配置不存在");
    }
    
    // 立即复制数据，因为 PQgetvalue 返回的指针在 PQclear 后会失效
    char *ca_cert_pem = strdup(PQgetvalue(res, 0, 0));
    char *ca_fingerprint = strdup(PQgetvalue(res, 0, 4));
    char *server_cert = strdup(PQgetvalue(res, 0, 1));
    char *remote = strdup(PQgetvalue(res, 0, 2));
    int port = atoi(PQgetvalue(res, 0, 3));
    char *proto = strdup(PQgetvalue(res, 0, 5));
    PQclear(res);
    
    log_message(LOG_INFO, "handle_gen_client_cert: step1 ca_cert_len=%zu, ca_fingerprint=%s", 
                strlen(ca_cert_pem), ca_fingerprint ? ca_fingerprint : "NULL");
    
    // 如果 ca_cert 为空，尝试从 vpn_ca 获取
    if (!ca_cert_pem || strlen(ca_cert_pem) < 100) {
        log_message(LOG_ERR, "handle_gen_client_cert: ca_cert is empty, trying to get from vpn_ca");
        snprintf(sql, sizeof(sql), 
            "SELECT ca.ca_cert, ca.ca_key FROM vpn_config c JOIN vpn_ca ca ON c.ca_fingerprint = ca.ca_fingerprint WHERE c.id = %d",
            config_id_val);
        PGresult *res2 = PQexec(g_conn, sql);
        if (PQresultStatus(res2) != PGRES_TUPLES_OK || PQntuples(res2) == 0) {
            free(ca_cert_pem); free(ca_fingerprint); free(server_cert); free(remote);
            PQclear(res2);
            return create_error_response("无法获取CA证书");
        }
        free(ca_cert_pem);
        ca_cert_pem = strdup(PQgetvalue(res2, 0, 0));
        char *ca_key_from_ca = strdup(PQgetvalue(res2, 0, 1));
        log_message(LOG_INFO, "handle_gen_client_cert: from vpn_ca ca_cert_len=%zu, ca_key_len=%zu",
                    strlen(ca_cert_pem), strlen(ca_key_from_ca));
        free(ca_key_from_ca);
        PQclear(res2);
    }
    
    if (!ca_fingerprint || strlen(ca_fingerprint) == 0) {
        free(ca_cert_pem); free(ca_fingerprint); free(server_cert); free(remote);
        return create_error_response("配置中无CA指纹");
    }
    
    // 从 vpn_ca 获取私钥 - 使用参数化查询
    const char *params_ca[1];
    params_ca[0] = ca_fingerprint;
    log_message(LOG_INFO, "handle_gen_client_cert: query CA key with fp=%s", ca_fingerprint);
    PGresult *res3 = PQexecParams(g_conn, "SELECT ca_key FROM vpn_ca WHERE ca_fingerprint = $1", 
        1, NULL, params_ca, NULL, NULL, 0);
    if (PQresultStatus(res3) != PGRES_TUPLES_OK) {
        log_message(LOG_ERR, "handle_gen_client_cert: SQL error: %s", PQerrorMessage(g_conn));
        free(ca_cert_pem); free(ca_fingerprint); free(server_cert); free(remote);
        PQclear(res3);
        return create_error_response("无法获取CA私钥");
    }
    if (PQntuples(res3) == 0) {
        log_message(LOG_ERR, "handle_gen_client_cert: no CA found with fingerprint=%s", ca_fingerprint);
        free(ca_cert_pem); free(ca_fingerprint); free(server_cert); free(remote);
        PQclear(res3);
        return create_error_response("无法获取CA私钥");
    }
    char *encrypted_ca_key = strdup(PQgetvalue(res3, 0, 0));
    log_message(LOG_INFO, "handle_gen_client_cert: final ca_cert_len=%zu, ca_key_len=%zu",
                strlen(ca_cert_pem), strlen(encrypted_ca_key));
    PQclear(res3);

    BIO *bio = BIO_new_mem_buf((void*)ca_cert_pem, -1);
    X509 *ca_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!ca_cert) {
        free(ca_cert_pem); free(ca_fingerprint); free(server_cert); free(remote); free(encrypted_ca_key);
        return create_error_response("解析CA证书失败");
    }

    bio = BIO_new_mem_buf((void*)encrypted_ca_key, -1);
    EVP_PKEY *ca_key = decrypt_private_key(encrypted_ca_key, NULL);
    BIO_free(bio);
    if (!ca_key) {
        X509_free(ca_cert);
        free(ca_cert_pem); free(ca_fingerprint); free(server_cert); free(remote); free(encrypted_ca_key);
        return create_error_response("密码错误，无法使用CA私钥");
    }

    EVP_PKEY *client_key = generate_rsa_key(2048);
    if (!client_key) {
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        free(ca_cert_pem); free(ca_fingerprint); free(server_cert); free(remote); free(encrypted_ca_key);
        return create_error_response("生成客户端密钥失败");
    }

    const char *cn_for_cert = cert_cn_str ? cert_cn_str : username_str;
    X509 *client_cert = sign_certificate(ca_key, ca_cert, client_key, cn_for_cert, days);
    if (!client_cert) {
        EVP_PKEY_free(client_key);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        free(ca_cert_pem); free(ca_fingerprint); free(server_cert); free(remote); free(encrypted_ca_key);
        return create_error_response("签发客户端证书失败");
    }

    char *client_cert_pem = cert_to_pem(client_cert);
    char *encrypted_client_key = encrypt_private_key(client_key, NULL);
    
    if (!encrypted_client_key) {
        free(client_cert_pem);
        X509_free(client_cert);
        EVP_PKEY_free(client_key);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        free(ca_cert_pem); free(ca_fingerprint); free(server_cert); free(remote); free(encrypted_ca_key);
        return create_error_response("加密客户端私钥失败");
    }

    // 插入用户 - 使用参数化查询防止SQL注入
    const char *params_user[3];
    params_user[0] = username_str;
    params_user[1] = email_str ? email_str : "";
    params_user[2] = client_name_str ? client_name_str : "";
    
    PGresult *res_user = PQexecParams(g_conn,
        "INSERT INTO vpn_users (username, email, disabled, client_name) VALUES ($1, $2, false, $3) "
        "ON CONFLICT (username) DO UPDATE SET email = EXCLUDED.email RETURNING id",
        3, NULL, params_user, NULL, NULL, 0);
    
    if (PQresultStatus(res_user) != PGRES_TUPLES_OK) {
        log_message(LOG_ERR, "插入用户失败: %s", PQerrorMessage(g_conn));
        PQclear(res_user);
        free(client_cert_pem);
        free(encrypted_client_key);
        X509_free(client_cert);
        EVP_PKEY_free(client_key);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        return create_error_response("数据库操作失败");
    }
    int user_id = atoi(PQgetvalue(res_user, 0, 0));
    PQclear(res_user);

    char user_id_str[16], config_id_str[16];
    snprintf(user_id_str, sizeof(user_id_str), "%d", user_id);
    snprintf(config_id_str, sizeof(config_id_str), "%d", config_id_val);

    // 处理 assigned_ip，INET 类型不能是空字符串
    const char *assigned_ip_value = (assigned_ip_str && strlen(assigned_ip_str) > 0) ? assigned_ip_str : NULL;

    const char *paramValues2[6];
    paramValues2[0] = user_id_str;
    paramValues2[1] = config_id_str;
    paramValues2[2] = assigned_ip_value;
    paramValues2[3] = assigned_ip_value ? "true" : "false";
    paramValues2[4] = client_cert_pem;
    paramValues2[5] = encrypted_client_key;

    const char *stmt = 
        "INSERT INTO vpn_client_profiles (user_id, config_id, assigned_ip, use_static_ip, client_cert, client_key) "
        "VALUES ($1, $2, $3::inet, $4, $5, $6) "
        "ON CONFLICT (user_id, config_id) DO UPDATE SET client_cert = EXCLUDED.client_cert, client_key = EXCLUDED.client_key "
        "RETURNING id";

    PGresult *res4 = PQexecParams(g_conn, stmt, 6, NULL, paramValues2, NULL, NULL, 0);
    if (PQresultStatus(res4) != PGRES_TUPLES_OK || PQntuples(res4) == 0) {
        log_message(LOG_ERR, "插入客户端档案失败: %s", PQerrorMessage(g_conn));
        PQclear(res4);
        free(client_cert_pem);
        free(encrypted_client_key);
        X509_free(client_cert);
        EVP_PKEY_free(client_key);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        return create_error_response("数据库操作失败");
    }
    int profile_id = atoi(PQgetvalue(res4, 0, 0));
    PQclear(res4);

    char *config;
    asprintf(&config,
        "client\n"
        "dev tun\n"
        "proto %s\n"
        "remote %s %d\n"
        "resolv-retry infinite\n"
        "nobind\n"
        "persist-key\n"
        "persist-tun\n"
        "remote-cert-tls server\n"
        "cipher AES-256-GCM\n"
        "verb 3\n"
        "pull\n"
        "<ca>\n%s\n</ca>\n"
        "<cert>\n%s\n</cert>\n"
        "<key>\n%s\n</key>\n",
        proto ? proto : "udp", remote, port, ca_cert_pem, client_cert_pem, encrypted_client_key);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "message", "客户端证书签发成功");
    cJSON_AddNumberToObject(resp, "profile_id", profile_id);
    cJSON_AddStringToObject(resp, "data", config ? config : "");
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "创建客户端证书成功: username=%s, profile_id=%d", username_str, profile_id);
    log_system_event("info", "core", log_msg);

    free(config);
    free(client_cert_pem);
    free(encrypted_client_key);
    X509_free(client_cert);
    EVP_PKEY_free(client_key);
    X509_free(ca_cert);
    EVP_PKEY_free(ca_key);
    free(proto);

    return resp;
}

cJSON* handle_get_client_config(const cJSON *params) {
    const cJSON *client_id = cJSON_GetObjectItem(params, "client_id");

    if (!client_id || !cJSON_IsNumber(client_id)) {
        return create_error_response("缺少 client_id");
    }

    int cid = client_id->valueint;

    char sql[2048];
    snprintf(sql, sizeof(sql),
        "SELECT cp.client_cert, cp.client_key, c.ca_cert, c.remote, c.port, c.proto "
        "FROM vpn_client_profiles cp JOIN vpn_config c ON cp.config_id = c.id WHERE cp.id = %d", cid);
    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return create_error_response("客户端不存在");
    }

    const char *client_cert_pem = PQgetvalue(res, 0, 0);
    const char *client_key_encrypted = PQgetvalue(res, 0, 1);
    const char *ca_cert_pem = PQgetvalue(res, 0, 2);
    const char *remote = PQgetvalue(res, 0, 3);
    int port = atoi(PQgetvalue(res, 0, 4));
    const char *proto = PQgetvalue(res, 0, 5);

    EVP_PKEY *pkey = decrypt_private_key(client_key_encrypted, NULL);
    if (!pkey) {
        PQclear(res);
        return create_error_response("解密私钥失败");
    }

    char *client_key_pem = key_to_pem(pkey);
    EVP_PKEY_free(pkey);

    char *config;
    asprintf(&config,
        "client\n"
        "dev tun\n"
        "proto %s\n"
        "remote %s %d\n"
        "resolv-retry infinite\n"
        "nobind\n"
        "persist-key\n"
        "persist-tun\n"
        "remote-cert-tls server\n"
        "cipher AES-256-GCM\n"
        "verb 3\n"
        "<ca>\n%s\n</ca>\n"
        "<cert>\n%s\n</cert>\n"
        "<key>\n%s\n</key>\n",
        proto ? proto : "udp", remote, port, ca_cert_pem, client_cert_pem, client_key_pem);

    free(client_key_pem);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "data", config);
    free(config);
    PQclear(res);
    return resp;
}

cJSON* handle_change_client_password(const cJSON *params) {
    // 密码修改功能暂时禁用，私钥现在使用系统密钥加密
    (void)params;
    return create_error_response("密码修改功能暂时禁用");
}

cJSON* handle_revoke_client_cert(const cJSON *params) {
    const cJSON *client_profile_id = cJSON_GetObjectItem(params, "client_profile_id");
    const cJSON *reason = cJSON_GetObjectItem(params, "reason");

    if (!client_profile_id || !cJSON_IsNumber(client_profile_id)) {
        return create_error_response("缺少 client_profile_id");
    }

    int profile_id = client_profile_id->valueint;
    const char *reason_str = reason && cJSON_IsString(reason) ? reason->valuestring : "revoked by admin";

    char sql[2048];
    snprintf(sql, sizeof(sql), "SELECT client_cert FROM vpn_client_profiles WHERE id = %d", profile_id);
    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return create_error_response("客户端档案不存在");
    }

    const char *client_cert_pem = PQgetvalue(res, 0, 0);
    char serial_str[128] = "unknown";
    BIO *bio = BIO_new_mem_buf((void*)client_cert_pem, -1);
    X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    
    if (cert) {
        ASN1_INTEGER *serial = X509_get_serialNumber(cert);
        if (serial) {
            BIGNUM *bn = ASN1_INTEGER_to_BN(serial, NULL);
            if (bn) {
                char *serial_hex = BN_bn2hex(bn);
                if (serial_hex) {
                    strncpy(serial_str, serial_hex, sizeof(serial_str) - 1);
                    OPENSSL_free(serial_hex);
                }
                BN_free(bn);
            }
        }
        X509_free(cert);
    }

    // 插入撤销记录 - 使用参数化查询防止SQL注入
    const char *params_revoke[3];
    char profile_id_str[16];
    snprintf(profile_id_str, sizeof(profile_id_str), "%d", profile_id);
    params_revoke[0] = profile_id_str;
    params_revoke[1] = serial_str;
    params_revoke[2] = reason_str ? reason_str : "revoked by admin";
    
    PGresult *res_revoke = PQexecParams(g_conn,
        "INSERT INTO vpn_revoked_certs (client_profile_id, serial_number, reason) VALUES ($1::integer, $2, $3)",
        3, NULL, params_revoke, NULL, NULL, 0);
    
    if (PQresultStatus(res_revoke) != PGRES_COMMAND_OK) {
        PQclear(res_revoke);
        PQclear(res);
        return create_error_response("撤销证书失败");
    }
    PQclear(res_revoke);
    PQclear(res);

    // 更新客户端档案为已撤销
    snprintf(sql, sizeof(sql), "UPDATE vpn_client_profiles SET revoked = TRUE WHERE id = %d", profile_id);
    PQexec(g_conn, sql);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "data", "证书已撤销");
    return resp;
}
