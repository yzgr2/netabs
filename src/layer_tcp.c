#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>  //close
#include <assert.h>

//addr info.
#include <sys/types.h>
#include <netdb.h>

#include <arpa/inet.h>  //inet_pton

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include "layer_tcp.h"
#include "net_evt.h"


int host_to_sockaddr(char *host, uint16_t port, struct sockaddr_in *s_addr)
{
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ( 0 != getaddrinfo(host, NULL, &hints, &result)) {
        return NET_ERROR;
    }

    int n = 0;
    struct addrinfo *rp = NULL;

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if( rp->ai_family == AF_INET ) {
            n++;
            *s_addr = *(struct sockaddr_in *)rp->ai_addr;
            s_addr->sin_port = htons(port);

            char buf[64] = {0};
            inet_ntop(AF_INET, &s_addr->sin_addr, buf, 64);
            NET_LOG_DBG("host:%s ip:%s\n", host, buf);

            break;
        }
        //AF_INET6
    }

    if( n == 0 ) {
        goto EXIT;
    }

    freeaddrinfo(result);
    return 0;

EXIT:
    NET_LOG_ERR("DNS resolved failed");
    freeaddrinfo(result);
    return NET_ERROR;
}

int set_ipaddr(char *ipstr, uint16_t port, struct sockaddr_in *s_addr)
{
    int rc = inet_pton(AF_INET, ipstr, &s_addr->sin_addr);
    if ( rc != 1 ) {
        NET_LOG_ERR("inet_pton failed, system errno=%d ", errno);
        return NET_ERROR;
    }

    s_addr->sin_family = AF_INET;
    s_addr->sin_port = htons(port);
    return 0;
}

int layer_tcp_read(struct net_layer *layer, void *buf, uint32_t len, int flags)
{
    struct layer_tcp *ctx = (struct layer_tcp *)layer;
    int n;

    n = read(ctx->sockfd, buf, len);
    if( n < 0) {
        NET_LOG_DBG("\n[TCP] layer_tcp_read n=%d errno=%d\n", n, errno);
    } else if (n ==0 ) {
        NET_LOG_DBG("\n[TCP] layer_tcp_read n=%d, EOF.\n", n);
    }

    return n;
}

#define MAX_RECV_BUF_LEN  1024

static void tcp_recv_evt_handle(int event_type, struct event_common *evt)
{
    struct layer_tcp *ctx = (struct layer_tcp *)evt->args;
    int n;

    assert(event_type & EVT_READ);

    if( ctx->base.empty_read ) {
        CALL_UPPER(nrcv_cb, NULL, 0, 0);
        // let upper layer enable Rx evt.
        // select_add_event(EVT_READ, evt);
        return;
    }

    uint8_t *buf = malloc(MAX_RECV_BUF_LEN);

    n = read(ctx->sockfd, buf, MAX_RECV_BUF_LEN);
    if( n > 0 ) {
        CALL_UPPER(nrcv_cb, buf, n, 0);

        //start read evt again. //let upper layer enable Rx evt.
        // select_add_event(EVT_READ, evt);
    } else if ( n == 0 ) {
        //connection closed.
        CALL_UPPER(ndisconnect_cb, n);
    } else if ( n < 0 ) {
        if( errno == EINTR ) {
            //do nothing. //start read evt again. //let upper layer enable Rx evt.
            // select_add_event(EVT_READ, evt);
        } else {
            CALL_UPPER(ndisconnect_cb, n);
        }
    }

    free(buf);

}

static int tcp_enable_rd_evt(struct net_layer *layer, int enable)
{
    struct layer_tcp *ctx = (struct layer_tcp *)layer;

    if( !enable ) {
        return 0;
    }

    if( !ctx->evt_rd ) {
        ctx->evt_rd = evt_allocate(EVT_READ, tcp_recv_evt_handle, ctx->sockfd, ctx);
    }

    NET_LOG_DBG("[TCP] enable read evt. inlist:%d\n", select_evt_inlist(ctx->evt_rd) );

    if( !select_evt_inlist(ctx->evt_rd)) {
        select_add_event(EVT_READ, ctx->evt_rd);
    }

    return 0;
}

// static int tcp_enable_wr_evt(struct net_layer *layer, int enable)
// {
//     // if( !ctx->evt_wr ) {
//     //     ctx->evt_wr = evt_allocate(EVT_WRITE, tcp_recv_evt_handle, ctx->sockfd, ctx);
//     // }

//     // if( !select_evt_inlist(ctx->evt_wr)) {
//     //     select_add_event(EVT_WRITE, ctx->evt_wr);
//     // }

//     return 0;
// }

static void tcp_conn_ready_handle(struct layer_tcp *ctx, int status)
{
    struct net_layer *upper = ctx->base.upper;

    NET_LOG_DBG("[TCP] call_upper_conn_cb : upper:%p upper->nconnect_cb:%p \n", upper, upper ? upper->nconnect_cb : NULL);

    if ( upper && upper->nconnect_cb ) {
        upper->nconnect_cb(upper, status);
    }

    // If upper layer want to read data in reactor mode, it will call tcp_enable_rd_evt() to enable recv_evt.
    // If upper layer want to read data in poll mode,  it can call nread() to read data directly.
    // if( upper && upper->nrcv_cb ) {
    //     NET_LOG_DBG("[TCP] enable recv evt.\n");
    //     if( !ctx->evt_rd ) {
    //         ctx->evt_rd = evt_allocate(EVT_READ, tcp_recv_evt_handle, ctx->sockfd, ctx);
    //     }
    //     select_add_event(EVT_READ, ctx->evt_rd);
    // }
}


static void tcp_conn_evt_handle(int event_type, struct event_common *evt)
{
    if ( event_type & EVT_WRITE ) {
        struct layer_tcp *ctx = (struct layer_tcp *)evt->args;

        NET_LOG_DBG("[TCP] tcp_conn_evt_handle() check conn status\n");

        int error;
        socklen_t len = sizeof(error);
        if ( getsockopt(evt->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 ) {
            NET_LOG_ERR("getsockopt error, system errno=%d", errno);
            return;
        }

        if ( error != 0 ) {
            //connect failed.
            NET_LOG_ERR("Failed to connect, system error=%d strerror:%s\n", error, strerror(error));
            ctx->state = TCP_CONN_FAILED;

            CALL_UPPER(nconnect_cb, CONN_FAIL);
        } else {
            //success, can start monitor rx evt in cb.
            ctx->state = TCP_READY;
            tcp_conn_ready_handle(ctx, CONN_SUCCESS);
        }
    } else {
        assert(event_type & EVT_WRITE);
    }

    free(evt);
}


int layer_tcp_config(struct net_layer *layer,  struct tcp_conn_param *par)
{
    struct layer_tcp *ctx = (struct layer_tcp *)layer ;

    memcpy( &ctx->config, par, sizeof(struct tcp_conn_param));

    //copy string.
    ctx->config.host = NULL;
    string_assign( &ctx->config.host, par->host);

    return 0;
}


int layer_tcp_connect(struct net_layer *layer)
{
    int ret;
    struct layer_tcp *ctx = (struct layer_tcp *)layer;

    char *host = ctx->config.host;
    uint16_t port = ctx->config.port;
    int non_block = ctx->config.base.non_block;

    ctx->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if ( ctx->sockfd == -1 ) {
        NET_LOG_ERR("Failed to create socekt, system errno=%d ", errno);
        return NET_ERROR;
    }

    NET_LOG_DBG("[TCP] layer_tcp_connect ctx->sockfd = %d\n", ctx->sockfd);

    if ( non_block ) {
        ret = fcntl(ctx->sockfd, F_SETFL, fcntl(ctx->sockfd, F_GETFL) | O_NONBLOCK);
        if ( ret == -1 ) {
            NET_LOG_ERR("fcntl() failed, system errno=%d ", errno);
            return NET_ERROR;
        }
    }

    struct sockaddr sockaddr;
    socklen_t    socklen;

    ret = host_to_sockaddr(host, port, (struct sockaddr_in *)&sockaddr);
    if ( ret != 0 ) {
        NET_LOG_ERR("[TCP] failed to translate hostname to ipaddr. host:[%s]\n", host);
        return NET_ERROR;
    }
    socklen = sizeof(sockaddr);

    ctx->state = TCP_CONNECTING;

    ret = connect(ctx->sockfd, &sockaddr, socklen);
    if ( ret < 0 &&  errno != EINPROGRESS  ) {
        close(ctx->sockfd);
        NET_LOG_ERR("tcp connect error, errno=%d", errno);
        return NET_ERROR;
    }

    if ( ret == 0 ) {
        //connect success.
        NET_LOG_DBG("tcp connect success");
        ctx->state = TCP_READY;
        tcp_conn_ready_handle(ctx, CONN_SUCCESS);
        return 0;
    }

    if ( ret < 0 &&  errno == EINPROGRESS  ) {
        NET_LOG_DBG("tcp connect EINPROGRESS\n");

        //EINPROGRESS, add socket event.
        ctx->evt_wr = evt_allocate(EVT_WRITE, tcp_conn_evt_handle, ctx->sockfd, ctx);
        select_add_event(EVT_WRITE, ctx->evt_wr);
        return -EINPROGRESS;
    }

    NET_LOG_DBG("[TCP] connect failed, ret=%d non-blocking=%d errno=%d\n", ret, non_block, errno);
    return -1;
}



int layer_tcp_send(struct net_layer *layer, void *buf, uint32_t len, int flags)
{
    struct layer_tcp *ctx = (struct layer_tcp *)layer ;
    int tcp_flags = 0, nbytes;

    // if( flags & F_NONBLOCK ) {
    //     tcp_flags = MSG_DONTWAIT ;
    // }

    nbytes =  send(ctx->sockfd, buf, len, tcp_flags);

    NET_LOG_DBG("layer_tcp_send: want_send=%d  nbytes=%d\n", len, nbytes);

    return nbytes;
}

int layer_tcp_close(struct net_layer *layer)
{
    struct layer_tcp *ctx = (struct layer_tcp *)layer ;

    NET_LOG_WRN("\n\n!!! layer_tcp_close !!!\n\n");

    //remove evt if they are in list.
    if( ctx->evt_rd && select_evt_inlist(ctx->evt_rd) ) {
        select_del_event(ctx->evt_rd);
    }

    if( ctx->evt_wr && select_evt_inlist(ctx->evt_wr) ) {
        select_del_event(ctx->evt_wr);
    }

    if( ctx->evt_rd ) {
        free(ctx->evt_rd);
        ctx->evt_rd = NULL;
    }

    if( ctx->evt_wr ) {
        free(ctx->evt_wr);
        ctx->evt_wr = NULL;
    }

    if( ctx->sockfd != -1 ) {
        close(ctx->sockfd);
        ctx->sockfd = -1;
    }

    return 0;
}

int tcp_init(struct layer_tcp *ctx)
{
    memset(ctx, 0, sizeof(struct layer_tcp));

    ctx->base.layer_tag = LAYER_TCP;
    ctx->base.nconfig  = (net_config)layer_tcp_config;
    ctx->base.nconnect = layer_tcp_connect;
    ctx->base.nsend    = layer_tcp_send;
    ctx->base.nread    = layer_tcp_read;
    ctx->base.nclose   = layer_tcp_close;
    ctx->base.nenable_rd_evt = tcp_enable_rd_evt;

    //nconnect_cb, nrcv_cb = NULL, since no lower layer below TCP layer.

    return 0;
}

