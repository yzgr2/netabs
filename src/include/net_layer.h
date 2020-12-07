#ifndef NET_LAYER_H
#define NET_LAYER_H

#include <stdint.h>

#include "net_common.h"

enum LAYER_TAG {
    LAYER_TCP,
    LAYER_TLS,
    LAYER_MQTT,
    LAYER_HTTP,
};

struct net_layer;

struct net_conn_param {
    int non_block;

    //private params of each layer.
};


typedef int (*net_config)(struct net_layer *layer, struct net_conn_param *config);

/**
 * @brief : declared function: send data from net_layer @layer.
 */
typedef int (*net_send)(struct net_layer *layer, void *buf, uint32_t len, int flags);

/**
 * @brief : declared function: send data from net_layer @layer.
 */
typedef int (*net_read)(struct net_layer *layer, void *buf, uint32_t len, int flags);

/**
 * @brief : @layer handle msg from lower layer.
 */
typedef int (*net_recv_cb)(struct net_layer *layer, void *msg, uint32_t len, int flags);


typedef int (*net_close)(struct net_layer *layer);

/**
 * @brief : enable read evt on this layer. usually used by TCP layer.
 */
typedef int (*net_enable_rd_evt)(struct net_layer *layer, int enable);

/**
 * @brief : enable write evt on this layer. usually used by TCP layer.
 */
typedef int (*net_enable_wr_evt)(struct net_layer *layer, int enable);

//return 0 when success.
//      -1 error
//      -EINPROGRESS inprogress for non-blocking mode.
typedef int (*net_connect)(struct net_layer *layer);

typedef enum  {
    CONN_FAIL = -1,
    CONN_SUCCESS,
} CONN_RESULT;


typedef int (*net_connect_cb)(struct net_layer *layer, CONN_RESULT status);

//
typedef int (*net_disconnect_cb)(struct net_layer *layer, int status);

/**
 * @brief : basic declare of net_layer, used as basic class of all net_layer.
 */
struct net_layer {
    struct net_layer *upper;
    struct net_layer *lower;

    int layer_tag;

    net_config      nconfig;

    net_connect     nconnect;
    net_connect_cb  nconnect_cb;

    net_send     nsend;

    //this layer not read data, let upper layer call nread() function to read data.
    //nrcv_cb notify upper layer to read data.
    int          empty_read;
    net_read     nread;     //active read.
    net_recv_cb  nrcv_cb;   //recv callback.

    net_disconnect_cb ndisconnect_cb;

    net_close   nclose;

    net_enable_rd_evt  nenable_rd_evt;
    net_enable_wr_evt  nenable_wr_evt;

    //private field of each field.
};


#define CALL_UPPER(func, params...)   do {         \
        struct net_layer *upper = ctx->base.upper; \
        if ( upper && upper->func ) {              \
                upper->func(upper,##params);       \
        }                                          \
    }while(0)


#define CALL_LOWER(func, params...)   do {         \
        struct net_layer *lower = ctx->base.lower; \
        if ( lower && lower->func ) {              \
                lower->func(lower,##params);       \
        }                                          \
    }while(0)


#define CALL_LOWER_RET_INT(func, params...)   ( {       \
        struct net_layer *lower = ctx->base.lower;      \
        int ret_temp;                                   \
        if ( lower && lower->func ) {                   \
                ret_temp = lower->func(lower,##params); \
        }                                               \
        ret_temp;                                       \
    })


#define LOWER_EMPTY_READ(val)         do {         \
        struct net_layer *lower = ctx->base.lower; \
        if ( lower ) {                             \
                lower->empty_read = val;           \
        }                                          \
    }while(0)

#endif
