#ifndef LAYER_TLS_H
#define LAYER_TLS_H

#include "net_layer.h"

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_PLATFORM_C)
#include "mbedtls/platform.h"
#else

#endif

#include "mbedtls/net_sockets.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

enum TLS_STATE {
    TLS_NOT_INITED = 0,
    TLS_INITED = 1,
};

struct tls_conn_param {
    struct net_conn_param base;

    char    *host;
    uint16_t port;

    char *ca_cert;
    int   ca_cert_len;

    char *our_cert;
    int our_cert_len;
};

struct layer_tls {
    struct net_layer base;

    int state;

    struct tls_conn_param config;

    //private field to tcp layer.
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_x509_crt clicert;
};

int tls_init(struct layer_tls *ctx);

#endif
