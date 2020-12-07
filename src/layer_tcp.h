#ifndef LAYER_TCP_H
#define LAYER_TCP_H

#include "net_layer.h"
#include "net_evt.h"

enum TCP_STATE {
    TCP_IDLE = -2,
    TCP_CONN_FAILED = -1,
    TCP_CONNECTING = 1,
    TCP_READY = 2,
    TCP_CLOSED = 3,
};

struct tcp_conn_param {
    struct net_conn_param base;

    char    *host;
    uint16_t port;
};

struct layer_tcp {
    struct net_layer base;

    //private field to tcp layer.
    int sockfd;
    int state;

    //evts on sockfd.
    struct event_common *evt_rd;
    struct event_common *evt_wr;

    struct tcp_conn_param config;
};


#endif

