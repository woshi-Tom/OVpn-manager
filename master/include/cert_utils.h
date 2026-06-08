#ifndef CERT_UTILS_H
#define CERT_UTILS_H

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <cjson/cJSON.h>

EVP_PKEY* generate_rsa_key(int bits);
X509* sign_certificate(EVP_PKEY *ca_key, X509 *ca_cert, EVP_PKEY *key, const char *common_name, int days);
char* encrypt_private_key(EVP_PKEY *key, const char *password);
EVP_PKEY* decrypt_private_key(const char *encrypted_pem, const char *password);
char* cert_to_pem(X509 *cert);
char* key_to_pem(EVP_PKEY *key);
void fingerprint_to_hex(const unsigned char *digest, unsigned int len, char *out);
char* escape_sql_string(const char *str);

#endif
