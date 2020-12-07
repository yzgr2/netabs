#ifndef LAYER_MQTT_H
#define LAYER_MQTT_H

#include "net_layer.h"


#define MQTT_BUF_SIZE    1024


struct mqtt_conn_param {
    struct net_conn_param base;

    char *client_id;
    char *username;
    char *password;
};

enum MQTT_STATE {
    S_MQTT_NOTINITED = 0,
    S_MQTT_INITED = 1,
};

struct layer_mqtt {
    struct net_layer base;

    int state;
    struct mqtt_conn_param config;

    //private field to mqtt layer.
    uint8_t  *send_buf;     //size: MQTT_BUF_SIZE
    uint16_t  send_buf_len;

    uint8_t  *read_buf;     //size: MQTT_BUF_SIZE
    uint16_t  read_buf_len;

    uint16_t msg_id;
};


#endif