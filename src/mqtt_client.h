#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#define MQTT_PUBLISH_TIMEOUT     (10*1000)

#include "layer_mqtt.h"

int decode_mqtt_packet(void *msg , uint32_t len, void *out_buf, int out_buf_len);

enum MQTT_RESULT {
    PUB_TIMEOUT = -2,
    PUB_FAILED = -1,
    PUB_SUCCESS = 0,
};

typedef void (*mqtt_publish_cb)(struct layer_mqtt *ctx, int msg_id, int result);

typedef void (*mqtt_subscribe_cb)(struct layer_mqtt *ctx, int msg_id, char *topic, unsigned char *msg, int msg_len);

int mqtt_publish(struct layer_mqtt *ctx, char *topic, unsigned char *msg, int msg_len, int qos, mqtt_publish_cb on_publish);

int mqtt_subscribe(struct layer_mqtt *ctx, char *topic, int qos, int *msg_id, mqtt_subscribe_cb on_subscribe);

void mqtt_clean_all_pub_waitter(struct layer_mqtt *ctx);

void mqtt_clean_all_subscriber(struct layer_mqtt *ctx);

#endif
