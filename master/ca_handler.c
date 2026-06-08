#include "ca_handler.h"
#include "cert_utils.h"
#include "logger.h"
#include "database.h"
#include "config.h"

#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

static cJSON* create_error_response(const char *message) {
    cJSON *err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "status", "error");
    cJSON_AddStringToObject(err, "message", message);
    return err;
}

cJSON* handle_generate_ca_cert(const cJSON *params) {
    const cJSON *common_name = cJSON_GetObjectItem(params, "common_name");
    const cJSON *valid_days = cJSON_GetObjectItem(params, "valid_days");

    if (!common_name || !cJSON_IsString(common_name)) {
        return create_error_response("缺少必要参数: common_name");
    }

    const char *cn = common_name->valuestring;
    int days = valid_days && cJSON_IsNumber(valid_days) ? valid_days->valueint : 3650;

    EVP_PKEY *ca_key = generate_rsa_key(4096);
    if (!ca_key) {
        return create_error_response("生成CA私钥失败");
    }

    X509 *ca_cert = X509_new();
    if (!ca_cert) {
        EVP_PKEY_free(ca_key);
        return create_error_response("创建CA证书失败");
    }

    X509_set_version(ca_cert, 2);
    BIGNUM *serial = BN_new();
    BN_rand(serial, 64, 0, 0);
    ASN1_INTEGER *serial_asn = BN_to_ASN1_INTEGER(serial, NULL);
    X509_set_serialNumber(ca_cert, serial_asn);
    ASN1_INTEGER_free(serial_asn);
    BN_free(serial);

    X509_gmtime_adj(X509_get_notBefore(ca_cert), 0);
    X509_gmtime_adj(X509_get_notAfter(ca_cert), days * 24 * 3600);
    X509_set_pubkey(ca_cert, ca_key);

    X509_NAME *name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)cn, -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char*)"VPN CA", -1, -1, 0);
    X509_set_subject_name(ca_cert, name);
    X509_set_issuer_name(ca_cert, name);
    X509_NAME_free(name);

    X509_EXTENSION *ext = X509V3_EXT_conf_nid(NULL, NULL, NID_basic_constraints, "critical,CA:TRUE");
    X509_add_ext(ca_cert, ext, -1);
    X509_EXTENSION_free(ext);

    ext = X509V3_EXT_conf_nid(NULL, NULL, NID_key_usage, "critical,keyCertSign,cRLSign,digitalSignature");
    X509_add_ext(ca_cert, ext, -1);
    X509_EXTENSION_free(ext);

    if (!X509_sign(ca_cert, ca_key, EVP_sha256())) {
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        return create_error_response("签名CA证书失败");
    }

    unsigned char fingerprint_bin[EVP_MAX_MD_SIZE];
    unsigned int fingerprint_len = 0;
    if (!X509_digest(ca_cert, EVP_sha256(), fingerprint_bin, &fingerprint_len)) {
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        return create_error_response("计算CA指纹失败");
    }
    char fingerprint_hex[65];
    fingerprint_to_hex(fingerprint_bin, fingerprint_len, fingerprint_hex);

    char *ca_cert_pem = cert_to_pem(ca_cert);
    log_message(LOG_INFO, "handle_generate_ca_cert: before encrypt, encryption_key=%s", g_config.encryption_key);
    char *encrypted_ca_key = encrypt_private_key(ca_key, NULL);

    log_message(LOG_INFO, "handle_generate_ca_cert: ca_cert_pem_len=%zu, encrypted_ca_key_len=%zu, encrypted_ca_key=%.100s", 
                strlen(ca_cert_pem), encrypted_ca_key ? strlen(encrypted_ca_key) : 0,
                encrypted_ca_key ? encrypted_ca_key : "NULL");

    if (!encrypted_ca_key) {
        free(ca_cert_pem);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        return create_error_response("加密CA私钥失败");
    }

    const char *paramValues[4];
    paramValues[0] = cn;
    paramValues[1] = ca_cert_pem;
    paramValues[2] = encrypted_ca_key;
    paramValues[3] = fingerprint_hex;
    
    const char *stmt = 
        "INSERT INTO vpn_ca (common_name, ca_cert, ca_key, ca_fingerprint) VALUES ($1, $2, $3, $4) "
        "ON CONFLICT (id) DO UPDATE SET common_name = EXCLUDED.common_name, ca_cert = EXCLUDED.ca_cert, ca_key = EXCLUDED.ca_key, ca_fingerprint = EXCLUDED.ca_fingerprint, created_at = NOW()";

    PGresult *res = PQexecParams(g_conn, stmt, 4, NULL, paramValues, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        log_message(LOG_ERR, "存储CA证书失败: %s", PQerrorMessage(g_conn));
        PQclear(res);
        free(ca_cert_pem);
        free(encrypted_ca_key);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        return create_error_response("数据库操作失败");
    }
    PQclear(res);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "data", "CA证书生成成功");
    cJSON_AddStringToObject(resp, "fingerprint", fingerprint_hex);

    free(ca_cert_pem);
    free(encrypted_ca_key);
    X509_free(ca_cert);
    EVP_PKEY_free(ca_key);

    return resp;
}

cJSON* handle_sign_server_cert(const cJSON *params) {
    const cJSON *config_id = cJSON_GetObjectItem(params, "config_id");
    const cJSON *ca_id = cJSON_GetObjectItem(params, "ca_id");
    const cJSON *common_name = cJSON_GetObjectItem(params, "common_name");
    const cJSON *valid_days = cJSON_GetObjectItem(params, "valid_days");

    if (!config_id || !cJSON_IsNumber(config_id) || !common_name || !cJSON_IsString(common_name)) {
        return create_error_response("缺少必要参数: config_id, common_name");
    }

    int cfg_id = config_id->valueint;
    const char *cn = common_name->valuestring;
    int days = valid_days && cJSON_IsNumber(valid_days) ? valid_days->valueint : 365;
    
    log_message(LOG_INFO, "sign_server_cert: cfg_id=%d, cn=%s, ca_id present=%d", cfg_id, cn, ca_id ? 1 : 0);

    char sql[4096];
    if (ca_id && cJSON_IsNumber(ca_id)) {
        snprintf(sql, sizeof(sql), "SELECT ca_cert, ca_key FROM vpn_ca WHERE id = %d", ca_id->valueint);
    } else {
        snprintf(sql, sizeof(sql), "SELECT ca_cert, ca_key FROM vpn_ca ORDER BY created_at DESC LIMIT 1");
    }
    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return create_error_response("请先生成CA证书");
    }

    const char *ca_cert_pem = PQgetvalue(res, 0, 0);
    const char *encrypted_ca_key = PQgetvalue(res, 0, 1);
    
    char *ca_cert_copy = strdup(ca_cert_pem);
    char *ca_key_copy = strdup(encrypted_ca_key);
    PQclear(res);
    
    log_message(LOG_INFO, "sign_server_cert: CA cert length: %zu, key length: %zu, ca_cert_copy=%.50s", 
                strlen(ca_cert_copy), strlen(ca_key_copy), ca_cert_copy);

    BIO *bio = BIO_new_mem_buf((void*)ca_cert_copy, -1);
    X509 *ca_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!ca_cert) {
        log_message(LOG_ERR, "解析CA证书失败, cert: %.200s", ca_cert_copy);
        free(ca_cert_copy);
        free(ca_key_copy);
        return create_error_response("解析CA证书失败");
    }
    
    EVP_PKEY *ca_key = decrypt_private_key(ca_key_copy, NULL);
    if (!ca_key) {
        X509_free(ca_cert);
        free(ca_cert_copy);
        free(ca_key_copy);
        return create_error_response("解密CA私钥失败");
    }

    EVP_PKEY *server_key = generate_rsa_key(4096);
    if (!server_key) {
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        free(ca_cert_copy);
        free(ca_key_copy);
        return create_error_response("生成服务器私钥失败");
    }

    X509 *server_cert = sign_certificate(ca_key, ca_cert, server_key, cn, days);
    if (!server_cert) {
        EVP_PKEY_free(server_key);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        free(ca_cert_copy);
        free(ca_key_copy);
        return create_error_response("签发服务器证书失败");
    }

    unsigned char fingerprint_bin[EVP_MAX_MD_SIZE];
    unsigned int fingerprint_len = 0;
    char fingerprint_hex[65] = "";
    if (X509_digest(ca_cert, EVP_sha256(), fingerprint_bin, &fingerprint_len)) {
        fingerprint_to_hex(fingerprint_bin, fingerprint_len, fingerprint_hex);
    }

    char *server_cert_pem = cert_to_pem(server_cert);
    char *encrypted_server_key = encrypt_private_key(server_key, NULL);

    if (!encrypted_server_key) {
        free(server_cert_pem);
        X509_free(server_cert);
        EVP_PKEY_free(server_key);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        free(ca_cert_copy);
        free(ca_key_copy);
        return create_error_response("加密服务器私钥失败");
    }

    const char *updateParams[5];
    updateParams[0] = server_cert_pem;
    updateParams[1] = encrypted_server_key;
    updateParams[2] = ca_cert_copy;
    updateParams[3] = fingerprint_hex;
    char cfg_id_str[32];
    snprintf(cfg_id_str, sizeof(cfg_id_str), "%d", cfg_id);
    updateParams[4] = cfg_id_str;
    
    const char *updateStmt = 
        "UPDATE vpn_config SET server_cert = $1, server_key = $2, ca_cert = $3, ca_fingerprint = $4 WHERE id = $5::integer";

    PGresult *res2 = PQexecParams(g_conn, updateStmt, 5, NULL, updateParams, NULL, NULL, 0);
    if (PQresultStatus(res2) != PGRES_COMMAND_OK) {
        log_message(LOG_ERR, "存储服务器证书失败: %s", PQerrorMessage(g_conn));
        PQclear(res2);
        free(server_cert_pem);
        free(encrypted_server_key);
        X509_free(server_cert);
        EVP_PKEY_free(server_key);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        free(ca_cert_copy);
        free(ca_key_copy);
        return create_error_response("数据库更新失败");
    }
    PQclear(res2);
    
    // 验证保存结果
    char verify_sql[256];
    snprintf(verify_sql, sizeof(verify_sql), "SELECT ca_cert, server_cert FROM vpn_config WHERE id = %d", cfg_id);
    PGresult *res3 = PQexec(g_conn, verify_sql);
    if (PQresultStatus(res3) == PGRES_TUPLES_OK && PQntuples(res3) > 0) {
        const char *saved_ca_cert = PQgetvalue(res3, 0, 0);
        log_message(LOG_INFO, "sign_server_cert: verify saved ca_cert_len=%zu", strlen(saved_ca_cert));
    }
    PQclear(res3);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "data", "服务器证书签发成功");

    free(server_cert_pem);
    free(encrypted_server_key);
    X509_free(server_cert);
    EVP_PKEY_free(server_key);
    X509_free(ca_cert);
    EVP_PKEY_free(ca_key);
    free(ca_cert_copy);
    free(ca_key_copy);

    return resp;
}

cJSON* handle_get_ca_cert(const cJSON *params) {
    (void)params;

    char sql[2048];
    snprintf(sql, sizeof(sql), "SELECT common_name, ca_cert, ca_fingerprint, created_at FROM vpn_ca ORDER BY created_at DESC LIMIT 1");
    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return create_error_response("未找到CA证书");
    }

    const char *common_name = PQgetvalue(res, 0, 0);
    const char *ca_cert = PQgetvalue(res, 0, 1);
    const char *fingerprint = PQgetvalue(res, 0, 2);
    const char *created_at = PQgetvalue(res, 0, 3);
    PQclear(res);

    BIO *bio = BIO_new_mem_buf((void*)ca_cert, -1);
    X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);

    char expires_at[64] = "未知";
    if (cert) {
        ASN1_TIME *not_after = X509_get_notAfter(cert);
        if (not_after) {
            BIO *out = BIO_new(BIO_s_mem());
            if (ASN1_TIME_print(out, not_after)) {
                char *data;
                long len = BIO_get_mem_data(out, &data);
                if (len > 0 && (size_t)len < sizeof(expires_at)) {
                    memcpy(expires_at, data, len);
                    expires_at[len] = '\0';
                }
            }
            BIO_free(out);
        }
        X509_free(cert);
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "common_name", common_name);
    cJSON_AddStringToObject(data, "created_at", created_at);
    cJSON_AddStringToObject(data, "expires_at", expires_at);
    cJSON_AddStringToObject(data, "fingerprint", fingerprint ? fingerprint : "");
    cJSON_AddItemToObject(resp, "data", data);
    return resp;
}

cJSON* handle_delete_ca_cert(const cJSON *params) {
    const cJSON *ca_id = cJSON_GetObjectItem(params, "ca_id");
    (void)params; // password 不再需要
    
    if (!ca_id || !cJSON_IsNumber(ca_id)) {
        return create_error_response("缺少必要参数: ca_id");
    }
    
    int id = ca_id->valueint;
    
    log_message(LOG_INFO, "handle_delete_ca_cert: ca_id=%d", id);
    
    char sql[2048];
    
    snprintf(sql, sizeof(sql), "SELECT ca_fingerprint, ca_cert, ca_key FROM vpn_ca WHERE id = %d", id);
    PGresult *res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return create_error_response("CA证书不存在");
    }
    
    const char *fingerprint = PQgetvalue(res, 0, 0);
    (void)PQgetvalue(res, 0, 1); // ca_cert_pem
    const char *encrypted_ca_key = PQgetvalue(res, 0, 2);
    log_message(LOG_INFO, "handle_delete_ca_cert: fingerprint=%s, ca_key_len=%zu", 
                fingerprint ? fingerprint : "NULL", strlen(encrypted_ca_key));
    
    char *ca_key_copy = strdup(encrypted_ca_key);
    char *fingerprint_copy = strdup(fingerprint);
    PQclear(res);
    
    if (!fingerprint_copy || strlen(fingerprint_copy) == 0) {
        free(ca_key_copy);
        free(fingerprint_copy);
        return create_error_response("CA指纹不存在，无法验证引用");
    }
    
    const char *params_ref[1];
    params_ref[0] = fingerprint_copy;
    res = PQexecParams(g_conn, "SELECT 1 FROM vpn_config WHERE ca_fingerprint = $1 LIMIT 1", 
        1, NULL, params_ref, NULL, NULL, 0);
    int ref_count = 0;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        ref_count = 1;
    }
    PQclear(res);
    
    if (ref_count > 0) {
        free(ca_key_copy);
        free(fingerprint_copy);
        return create_error_response("CA证书被VPN配置引用，无法删除");
    }
    
    EVP_PKEY *ca_key = decrypt_private_key(ca_key_copy, NULL);
    if (!ca_key) {
        free(ca_key_copy);
        free(fingerprint_copy);
        return create_error_response("解密CA私钥失败");
    }
    EVP_PKEY_free(ca_key);
    
    snprintf(sql, sizeof(sql), "DELETE FROM vpn_ca WHERE id = %d", id);
    res = PQexec(g_conn, sql);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        PQclear(res);
        free(ca_key_copy);
        free(fingerprint_copy);
        return create_error_response("删除CA证书失败");
    }
    PQclear(res);
    
    free(ca_key_copy);
    free(fingerprint_copy);
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "data", "CA证书删除成功");
    return resp;
}
