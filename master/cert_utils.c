#include "cert_utils.h"
#include "logger.h"
#include "database.h"
#include "config.h"

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/bio.h>

EVP_PKEY* generate_rsa_key(int bits) {
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) return NULL;
    if (EVP_PKEY_keygen_init(ctx) <= 0) goto err;
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0) goto err;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) goto err;
err:
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

X509* sign_certificate(EVP_PKEY *ca_key, X509 *ca_cert, EVP_PKEY *key, const char *common_name, int days) {
    X509 *cert = X509_new();
    if (!cert) return NULL;

    X509_set_version(cert, 2);
    BIGNUM *serial = BN_new();
    BN_rand(serial, 64, 0, 0);
    ASN1_INTEGER *serial_asn = BN_to_ASN1_INTEGER(serial, NULL);
    X509_set_serialNumber(cert, serial_asn);
    ASN1_INTEGER_free(serial_asn);
    BN_free(serial);

    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), days * 24 * 3600);
    X509_set_pubkey(cert, key);
    X509_set_issuer_name(cert, X509_get_subject_name(ca_cert));

    X509_NAME *name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)common_name, -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_NAME_free(name);

    X509_EXTENSION *ext = X509V3_EXT_conf_nid(NULL, NULL, NID_basic_constraints, "critical,CA:FALSE");
    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);

    if (!X509_sign(cert, ca_key, EVP_sha256())) {
        X509_free(cert);
        return NULL;
    }
    return cert;
}

char* cert_to_pem(X509 *cert) {
    BIO *bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, cert);
    char *data;
    long len = BIO_get_mem_data(bio, &data);
    char *result = malloc(len + 1);
    memcpy(result, data, len);
    result[len] = '\0';
    BIO_free(bio);
    return result;
}

char* key_to_pem(EVP_PKEY *key) {
    BIO *bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL);
    char *data;
    long len = BIO_get_mem_data(bio, &data);
    char *result = malloc(len + 1);
    memcpy(result, data, len);
    result[len] = '\0';
    BIO_free(bio);
    return result;
}

EVP_PKEY* decrypt_private_key(const char *encrypted_pem, const char *password) {
    size_t pem_len = encrypted_pem ? strlen(encrypted_pem) : 0;
    log_message(LOG_INFO, "decrypt_private_key: system_key_present=%d, pem_len=%zu", 
                g_config.encryption_key[0] ? 1 : 0, pem_len);
    ERR_clear_error();
    
    // 尝试用系统密钥解密
    if (g_config.encryption_key[0]) {
        BIO *bio = BIO_new_mem_buf((void*)encrypted_pem, -1);
        if (bio) {
            OpenSSL_add_all_algorithms();
            EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, NULL, NULL, (void*)g_config.encryption_key);
            BIO_free(bio);
            if (key) {
                log_message(LOG_INFO, "decrypt_private_key: worked with system key");
                return key;
            }
            log_message(LOG_ERR, "decrypt_private_key: system key failed");
        }
    }
    
    // 尝试用用户密码解密
    if (password && password[0]) {
        BIO *bio = BIO_new_mem_buf((void*)encrypted_pem, -1);
        if (bio) {
            EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, NULL, NULL, (void*)password);
            BIO_free(bio);
            if (key) {
                log_message(LOG_INFO, "decrypt_private_key: worked with user password");
                return key;
            }
        }
    }
    
    // 尝试不加密的私钥
    BIO *bio = BIO_new_mem_buf((void*)encrypted_pem, -1);
    if (bio) {
        EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (key) {
            log_message(LOG_INFO, "decrypt_private_key: worked without password");
            return key;
        }
    }
    
    log_message(LOG_ERR, "decrypt_private_key: all attempts failed");
    unsigned long err = ERR_peek_error();
    if (err) {
        log_message(LOG_ERR, "decrypt_private_key: OpenSSL error: %s", ERR_error_string(err, NULL));
    }
    return NULL;
}

char* encrypt_private_key(EVP_PKEY *key, const char *password) {
    const char *enc_key = g_config.encryption_key[0] ? g_config.encryption_key : password;
    log_message(LOG_INFO, "encrypt_private_key: system_key_present=%d, key_len=%zu", 
                g_config.encryption_key[0] ? 1 : 0, g_config.encryption_key[0] ? strlen(g_config.encryption_key) : 0);
    if (!enc_key) {
        log_message(LOG_ERR, "encrypt_private_key: no encryption key available");
        return NULL;
    }
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) return NULL;
    
    EVP_CIPHER *cipher = EVP_aes_256_cbc();
    int pwd_len = strlen(enc_key);
    if (!PEM_write_bio_PrivateKey(bio, key, cipher, (unsigned char*)enc_key, pwd_len, NULL, NULL)) {
        log_message(LOG_ERR, "encrypt_private_key: PEM_write_bio_PrivateKey failed");
        BIO_free(bio);
        return NULL;
    }
    char *data;
    long len = BIO_get_mem_data(bio, &data);
    char *result = malloc(len + 1);
    if (result) {
        memcpy(result, data, len);
        result[len] = '\0';
    }
    BIO_free(bio);
    return result;
}

void fingerprint_to_hex(const unsigned char *digest, unsigned int len, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned int i = 0; i < len; i++) {
        out[i*2] = hex[(digest[i] >> 4) & 0x0F];
        out[i*2 + 1] = hex[digest[i] & 0x0F];
    }
    out[len*2] = '\0';
}

char* escape_sql_string(const char *str) {
    if (!str) return NULL;
    int len = strlen(str);
    int count = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] == '\'') count++;
    }
    char *escaped = malloc(len + count + 1);
    char *p = escaped;
    for (int i = 0; i < len; i++) {
        if (str[i] == '\'') {
            *p++ = '\'';
            *p++ = '\'';
        } else {
            *p++ = str[i];
        }
    }
    *p = '\0';
    return escaped;
}
