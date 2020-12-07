#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "layer_tls.h"
#include "layer_tcp.h"

char *def_ca_path;
char *def_cert;

static int layer_tls_config(struct net_layer *layer, struct net_conn_param *config)
{
    struct layer_tls *ctx = (struct layer_tls *)layer;
    struct tls_conn_param *par = (struct tls_conn_param *)config;

    memcpy(&ctx->config, par, sizeof(struct tls_conn_param));
    ctx->config.host = NULL;
    string_assign( &ctx->config.host, par->host );

    ctx->config.ca_cert = NULL;
    string_assign( &ctx->config.ca_cert, par->ca_cert );

    ctx->config.our_cert = NULL;
    string_assign( &ctx->config.our_cert, par->our_cert );

    return 0;
}


void tls_cleanup(struct layer_tls *ctx);

static int tcp_disconnect_cb(struct net_layer *layer, int status)
{
    //notify lower layer close socekt.
    struct layer_tls *ctx = (struct layer_tls *)layer;

    CALL_UPPER(ndisconnect_cb, status);

    return 0;
}

static int tls_close(struct net_layer *layer)
{
    struct layer_tls *ctx = (struct layer_tls *)layer;
    if ( ctx->state == TLS_NOT_INITED ) {
        return -1;
    }

    CALL_LOWER(nclose);

    tls_cleanup(ctx);
    return 0;
}


int tls_read_data(struct net_layer *layer, void *buf, uint32_t len, int flags)
{
    struct layer_tls *ctx = (struct layer_tls *)layer;

    if ( ctx->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER ) {
        NET_LOG_ERR("function should be called after tls handshake done.");
        //TODO: do handshake
        return -1;
    }

    int rc;
    rc = mbedtls_ssl_read( &ctx->ssl, buf, len );
    if ( rc > 0 ) {
        return rc;
    }

    if ( rc == 0) {
        return rc;
    }

    switch (rc) {
        case MBEDTLS_ERR_SSL_WANT_READ:
        case MBEDTLS_ERR_SSL_WANT_WRITE:
            errno = EAGAIN;
            return -1;
        default:
            errno = EIO;
            return -1;
    }
}

int tls_write_data(struct net_layer *layer, void *buf, uint32_t len, int flags)
{
    struct layer_tls *ctx = (struct layer_tls *)layer;

    if ( ctx->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER ) {
        NET_LOG_ERR("function should be called after tls handshake done.");
        //TODO: do handshake
        return -1;
    }

    int rc;
    rc = mbedtls_ssl_write( &ctx->ssl, buf, len );
    if ( rc > 0 ) {
        return rc;
    }

    switch (rc) {
        case MBEDTLS_ERR_SSL_WANT_READ:
        case MBEDTLS_ERR_SSL_WANT_WRITE:
            errno = EAGAIN;
            return -1;
        default:
            errno = EIO;
            return -1;
    }
}

#define MAX_RECV_BUF_LEN 4096

static int recv_tcp_data_cb(struct net_layer *layer, void *msg, uint32_t len, int flags)
{
    struct layer_tls *ctx = (struct layer_tls *)layer;

    if ( ctx->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER ) {
        NET_LOG_ERR("function should be called after tls handshake done.");
        //TODO: do handshake
        return -1;
    }

    //assert(msg==NULL); //msg should be null, tcp layer not readout data.

    if ( ctx->base.empty_read ) {
        NET_LOG_DBG("handshake complete, notify upper layer to read data.");
        CALL_UPPER(nrcv_cb, NULL, 0, 0);
    } else {
        //ssl read.
        NET_LOG_DBG("[TLS] recv_tcp_data_cb IN.\n");

        uint8_t *buf = malloc(MAX_RECV_BUF_LEN);

        //read all available data out.
        do {
            int rc = tls_read_data(layer, buf, MAX_RECV_BUF_LEN, 0);
            if ( rc > 0) {
                NET_LOG_DBG("[tls] decryped data: len=%d", rc);
                CALL_UPPER(nrcv_cb, buf, rc, 0);
            } else if ( rc == 0 ) {
                NET_LOG_DBG("[tls] disconnected by peer.");
                CALL_UPPER(ndisconnect_cb, rc);
                goto DISCONN;
            } else if ( rc < 0 ) {
                NET_LOG_DBG("[tls] error rc=%d errno=%d, disconnect it", rc, errno);
                CALL_UPPER(ndisconnect_cb, rc);
                goto DISCONN;
            }
        } while ( mbedtls_ssl_get_bytes_avail(&ctx->ssl) > 0 );

        free(buf);
    }

    // // monitor rx data available ?  mbedtls_ssl_write read data from TCP layer directly.
    // NET_LOG_DBG("[TLS] enable rd evt, WHEN ssl_read() done.");
    // CALL_LOWER(nenable_rd_evt, 1);

DISCONN:
    return 0;
}

static int recv_tcp_handshake_cb(struct net_layer *layer, void *msg, uint32_t len, int flags)
{
    struct layer_tls *ctx = (struct layer_tls *)layer;
    int ret = 0;

    do {
        ret = mbedtls_ssl_handshake_step( &ctx->ssl );
        switch (ret) {
            case 0:
                if ( ctx->ssl.state == MBEDTLS_SSL_HANDSHAKE_OVER ) {
                    NET_LOG_DBG("[tls] #### TLS HANDSHAKE COMPLETE ####\n");

                    CALL_UPPER(nconnect_cb, CONN_SUCCESS);

                    // NET_LOG_DBG("recv_tcp_handshake_cb enable evt rx, WHEN handshake complete.\n");
                    LOWER_EMPTY_READ(1);
                    ctx->base.nrcv_cb   = recv_tcp_data_cb;
                    // CALL_LOWER(nenable_rd_evt, 1);      //when has data to read, let this layer handle it.

                    return 0;  //complete.
                }
            case MBEDTLS_ERR_SSL_WANT_READ:
                //lower layer registerd rx_cb, rx cb will be called.
                LOWER_EMPTY_READ(1);
                ctx->base.nrcv_cb   = recv_tcp_handshake_cb;
                NET_LOG_DBG("recv_tcp_handshake_cb enable evt rx, WHEN WANT_READ.\n");
                CALL_LOWER(nenable_rd_evt, 1);      //when has data to read, call this function again.

            case MBEDTLS_ERR_SSL_WANT_WRITE:
                break;
            default:
                NET_LOG_ERR("tls handshake error. ret=%d", ret);
                CALL_UPPER(nconnect_cb, CONN_FAIL);
                break;
        }
    } while (ret == 0);

    return ret;
}

static int tls_handshake_block(struct layer_tls *ctx )
{
    int ret;

    while ( ( ret = mbedtls_ssl_handshake( &ctx->ssl ) ) != 0 ) {
        if ( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE ) {
            NET_LOG_ERR( " failed\n  ! mbedtls_ssl_handshake returned -0x%x\n\n", -ret );
            return ret;
        }
    }

    NET_LOG_DBG("#### block handshake complete ####");

    return 0;
}

int tcp_connect_cb(struct net_layer *layer, int status)
{
    // struct layer_tls *ctx = (struct layer_tls *)layer;
    int ret;

    if ( status != CONN_SUCCESS ) {
        NET_LOG_WRN("[TLS] tcp connect is not ready. status=%d\n", status);
        return -1;
    }

    ret = recv_tcp_handshake_cb(layer, NULL, 0, 0);

    return ret;
}


static void my_debug( void *ctx, int level, const char *file, int line, const char *str )
{
    ((void) level);
    NET_LOG_DBG("%s:%04d: %s", file, line, str );
}


/*
 * Write at most 'len' characters
 */
int my_mbedtls_net_send( void *ctx_ssl, const unsigned char *buf, size_t len )
{
    struct layer_tls *ctx = (struct layer_tls *)ctx_ssl;
    struct net_layer *lower = ctx->base.lower;

    assert(lower && lower->nsend );

    int ret = 0;
    if ( lower && lower->nsend ) {
        ret = lower->nsend(lower, (void *)buf, len, 0);
    }

    if ( ret < 0 ) {
        // if( net_would_block( ctx ) != 0 )
        //     return( MBEDTLS_ERR_SSL_WANT_WRITE );

        if ( errno == EAGAIN || errno == EWOULDBLOCK) {
            return (MBEDTLS_ERR_SSL_WANT_WRITE);
        }

        if ( errno == EINTR ) {
            return ( MBEDTLS_ERR_SSL_WANT_WRITE );
        }

        if ( errno == EPIPE || errno == ECONNRESET ) {
            return ( MBEDTLS_ERR_NET_CONN_RESET );
        }

        return ( MBEDTLS_ERR_NET_SEND_FAILED );
    }

    return ( ret );
}


/*
 * Read at most 'len' characters
 */
int my_mbedtls_net_recv( void *ctx_ssl, unsigned char *buf, size_t len )
{
    struct layer_tls *ctx = (struct layer_tls *)ctx_ssl;
    int nbytes = 0;

    struct net_layer *lower = ctx->base.lower;
    if ( lower && lower->nsend ) {
        nbytes = lower->nread(lower, buf, len, 0);
    } else {
        assert(lower && lower->nsend);
    }

    if ( nbytes < 0 ) {
        // if( net_would_block( ctx ) != 0 )
        //     return( MBEDTLS_ERR_SSL_WANT_READ );
        if ( errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return (MBEDTLS_ERR_SSL_WANT_READ);
        }

        if ( errno == EPIPE || errno == ECONNRESET ) {
            return ( MBEDTLS_ERR_NET_CONN_RESET );
        }

        NET_LOG_WRN("\n[TLS] lower->nread ret=%d errno=%d\n", nbytes, errno);
        return ( MBEDTLS_ERR_NET_RECV_FAILED );
    }

    return nbytes;
}


int layer_tls_conn(struct net_layer *layer)
{
    struct layer_tls *ctx = (struct layer_tls *)layer;
    struct tls_conn_param *par = (struct tls_conn_param *)&ctx->config;
    int ret;

    if ( ( ret = mbedtls_ssl_config_defaults( &ctx->conf,
                 MBEDTLS_SSL_IS_CLIENT,
                 MBEDTLS_SSL_TRANSPORT_STREAM,
                 MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 ) {
        NET_LOG_ERR( " failed\n  ! mbedtls_ssl_config_defaults returned %d\n\n", ret );
        goto exit;
    }

    ret = mbedtls_x509_crt_parse( &ctx->cacert, (const unsigned char *) par->ca_cert,
                                  par->ca_cert_len );
    if ( ret < 0 ) {
        NET_LOG_ERR( " failed\n  !  mbedtls_x509_crt_parse returned -0x%x\n\n", -ret );
        goto exit;
    }

    NET_LOG_DBG("[tls] ca cert parsed. cert len=%d", par->ca_cert_len);

    if ( par->our_cert ) {
        ret = mbedtls_x509_crt_parse( &ctx->clicert, (const unsigned char *) par->our_cert,
                                      par->our_cert_len );
        if ( ret < 0 ) {
            NET_LOG_ERR( " failed\n  !  mbedtls_x509_crt_parse returned -0x%x\n\n", -ret );
            goto exit;
        }

        if ( ( ret = mbedtls_ssl_conf_own_cert( &ctx->conf, &ctx->clicert, NULL ) ) != 0 ) {
            NET_LOG_ERR( " failed\n  ! mbedtls_ssl_conf_own_cert returned %d\n\n", ret );
            goto exit;
        }
    }

    /* OPTIONAL is not optimal for security */
    //mbedtls_ssl_conf_authmode( &ctx->conf, MBEDTLS_SSL_VERIFY_OPTIONAL );
    mbedtls_ssl_conf_authmode( &ctx->conf, MBEDTLS_SSL_VERIFY_NONE );
    mbedtls_ssl_conf_ca_chain( &ctx->conf, &ctx->cacert, NULL );

    mbedtls_ssl_conf_rng( &ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg );
    mbedtls_ssl_conf_dbg( &ctx->conf, my_debug, stdout );

    mbedtls_ssl_conf_read_timeout( &ctx->conf, 60 * 1000 );

    if ( ( ret = mbedtls_ssl_setup( &ctx->ssl, &ctx->conf ) ) != 0 ) {
        mbedtls_printf( " failed\n  ! mbedtls_ssl_setup returned %d\n\n", ret );
        goto exit;
    }

    if ( ( ret = mbedtls_ssl_set_hostname( &ctx->ssl, par->host ) ) != 0 ) {
        mbedtls_printf( " failed\n  ! mbedtls_ssl_set_hostname returned %d\n\n", ret );
        goto exit;
    }

    mbedtls_ssl_set_bio( &ctx->ssl, ctx, my_mbedtls_net_send, my_mbedtls_net_recv, NULL );

    NET_LOG_DBG("[tls] set bio done");

    //connect to host.
    if ( ctx->config.base.non_block ) {
        ctx->base.nconnect_cb = tcp_connect_cb;
        ctx->base.nrcv_cb    = recv_tcp_handshake_cb;

        NET_LOG_DBG("Wait TCP connect complete.");

        CALL_LOWER(nconnect);
    } else {
        ret = CALL_LOWER_RET_INT(nconnect);  //blocking mode,
        if ( ret != 0 ) {
            return ret;
        }

        //start handshake in blocking mode.
        ret = tls_handshake_block(ctx);

        return ret;
    }

    return 0;

exit:
    return -1;
}

int layer_tls_enable_rd_evt(struct net_layer *layer, int enable)
{
    struct layer_tls *ctx = (struct layer_tls *)layer;

    // NET_LOG_DBG("[TLS] enable next layer(TCP) rd evt. enable=%d", enable);
    CALL_LOWER(nenable_rd_evt, 1);
    return 0;
}


void tls_cleanup(struct layer_tls *ctx)
{
    mbedtls_ssl_free( &ctx->ssl );
    mbedtls_ssl_config_free( &ctx->conf );
    mbedtls_x509_crt_free( &ctx->cacert );
    mbedtls_ctr_drbg_free( &ctx->ctr_drbg );
    mbedtls_entropy_free( &ctx->entropy );
    ctx->state = TLS_NOT_INITED;
}

int tls_init(struct layer_tls *ctx)
{
    int ret;

    memset(ctx, 0, sizeof(struct layer_tls));
    ctx->base.layer_tag     = LAYER_TLS;
    ctx->base.nconfig       = layer_tls_config;
    ctx->base.nconnect      = layer_tls_conn;
    ctx->base.ndisconnect_cb = tcp_disconnect_cb;
    ctx->base.nclose        = tls_close;
    ctx->base.nsend         = tls_write_data;
    ctx->base.nread         = tls_read_data;
    ctx->base.nenable_rd_evt = layer_tls_enable_rd_evt;

#if defined(MBEDTLS_DEBUG_C)
    mbedtls_debug_set_threshold( 3 );
#endif

    mbedtls_ssl_init( &ctx->ssl );
    mbedtls_ssl_config_init( &ctx->conf );
    mbedtls_x509_crt_init( &ctx->cacert );
    mbedtls_ctr_drbg_init( &ctx->ctr_drbg );
    mbedtls_entropy_init( &ctx->entropy );

    if ( ( ret = mbedtls_ctr_drbg_seed( &ctx->ctr_drbg, mbedtls_entropy_func,
                                        &ctx->entropy, NULL,  0 ) ) != 0 ) {
        NET_LOG_ERR( " failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret );
        goto exit;
    }

    ctx->state = TLS_INITED;

    return 0;

exit:
    return -1;
}

