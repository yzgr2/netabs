#include <string.h>
#include <stdlib.h>

#include "layer_mqtt.h"

#include "MQTTPacket.h"
#include "MQTTConnect.h"

#include "net_timer.h"

extern int decode_mqtt_packet(void *msg , uint32_t len, void *out_buf, int out_buf_len);
extern void mqtt_clean_all_pub_waitter(struct layer_mqtt *ctx);
extern void mqtt_clean_all_subscriber(struct layer_mqtt *ctx);

static int layer_mqtt_config(struct net_layer *layer, struct net_conn_param *config)
{
    struct layer_mqtt *ctx = (struct layer_mqtt *)layer;
    struct mqtt_conn_param *par = (struct mqtt_conn_param *)config;

    memcpy(&ctx->config, par, sizeof(struct mqtt_conn_param));

    ctx->config.client_id = NULL;
    string_assign( &ctx->config.client_id, par->client_id );

    ctx->config.username = NULL;
    string_assign( &ctx->config.username, par->username );

    ctx->config.password = NULL;
    string_assign( &ctx->config.password, par->password );

    return 0;
}

int verify_mqtt_conn_result(struct layer_mqtt *ctx, void *msg, uint32_t len)
{
    unsigned char buf[2048] = {0x00};
	int buf_len = sizeof(buf);

    int pkt_type = decode_mqtt_packet(msg, len, buf, buf_len);
    if( pkt_type == CONNACK ) {
        unsigned char sessionPresent, connack_rc;
        int ret = MQTTDeserialize_connack(&sessionPresent,
                                        &connack_rc, buf, len);
        if (ret == 1 && connack_rc == 0) {
            return 1;
        } else {
            NET_LOG_ERR("connack ack result error: connack_rc=%d ret=%d", connack_rc, ret);
            return 0;
        }
    } else {
        NET_LOG_WRN("get other Mqtt packet during connect phase.");
        return 0;
    }
}


extern int mqtt_on_recv(struct layer_mqtt *ctx, void *msg, uint32_t len, int flags);

int mqtt_conn_ack_check(struct net_layer *layer, void *msg, uint32_t len, int flags)
{
    struct layer_mqtt *ctx = (struct layer_mqtt *)layer;

    if( !msg ) {
        //read data actively.. should not happen ?
    }

    int success = verify_mqtt_conn_result(ctx, msg, len);
    if ( success ) {  //success.
        NET_LOG_DBG("MQTT connect success.\n");
        CALL_UPPER(nconnect_cb, CONN_SUCCESS);

        ctx->base.nrcv_cb = (net_recv_cb)mqtt_on_recv;   //let mqtt client to handle rx.
    } else {
        CALL_UPPER(nconnect_cb, CONN_FAIL);
    }

    return 0;
}

int tls_tcp_connect_cb(struct net_layer *layer, int status)
{
    struct layer_mqtt *ctx = (struct layer_mqtt *)layer;
    int ret;

    if ( status != CONN_SUCCESS ) {
        NET_LOG_WRN("[TLS] tcp connect is not ready. status=%d\n", status);
        return -1;
    }

    //send connect cmd.
    ctx->base.nrcv_cb     = mqtt_conn_ack_check;

    CALL_LOWER(nenable_rd_evt, 1);

    ret = CALL_LOWER_RET_INT(nsend, ctx->send_buf, ctx->send_buf_len, 0);

    return ret;
}

int layer_mqtt_connect(struct net_layer *layer)
{
    struct layer_mqtt *ctx = (struct layer_mqtt *)layer;

    MQTTPacket_connectData default_options = MQTTPacket_connectData_initializer;
    default_options.clientID.cstring = ctx->config.client_id;
    default_options.username.cstring = ctx->config.client_id;
    default_options.password.cstring = ctx->config.client_id;

    int len = MQTTSerialize_connect( ctx->send_buf, MQTT_BUF_SIZE, &default_options);
    if( len <= 0 ) {
        return NET_PARAM_ERR;
    }
    ctx->send_buf_len = len;

    if( ctx->config.base.non_block ) {
        ctx->base.nconnect_cb = tls_tcp_connect_cb;

        NET_LOG_DBG("[MQTT] START lower layer non-blocking connect.");

        int ret = CALL_LOWER_RET_INT(nconnect);  //non-blocking mode

        return ret;
    } else {
        int ret = CALL_LOWER_RET_INT(nconnect);  //blocking mode,
        if( ret != 0 ) {
            return ret;
        }

        ret = CALL_LOWER_RET_INT(nsend, ctx->send_buf, ctx->send_buf_len, 0);
        if( ret <= 0 ) {
            return ret;
        }

        ret = CALL_LOWER_RET_INT(nread, ctx->read_buf, ctx->read_buf_len, 0);
        if( ret > 0 ) {
            verify_mqtt_conn_result(ctx, ctx->send_buf, ret);
        } else {
            //
        }

        return ret;
    }
}


//return message_id > 0
int layer_mqtt_send(struct net_layer *layer, void *buf, uint32_t buf_len, int flags)
{
    struct layer_mqtt *ctx = (struct layer_mqtt *)layer;

    NET_LOG_DBG("[MQTT] Send len=%d\n", buf_len);

    int ret = CALL_LOWER_RET_INT(nsend, buf, buf_len, 0);
    return ret;
}

int layer_mqtt_enable_rd_evt(struct net_layer *layer, int enable)
{
    struct layer_mqtt *ctx = (struct layer_mqtt *)layer;

    CALL_LOWER(nenable_rd_evt, enable);
    return 0;
}


static int mqtt_close(struct net_layer *layer)
{
    struct layer_mqtt *ctx = (struct layer_mqtt *)layer;
    if ( ctx->state == S_MQTT_NOTINITED ) {
        return -1;
    }

    NET_LOG_DBG("[mqtt] disconnect_cb here\n");

    //notify lower layer close socekt.
    CALL_LOWER(nclose);

    //TODO: clean config

    mqtt_clean_all_pub_waitter(ctx);
    mqtt_clean_all_subscriber(ctx);

    if( ctx->send_buf_len) {
        free(ctx->send_buf);
    }

    if( ctx->read_buf ) {
        free(ctx->read_buf);
    }

    return 0;
}

static int disconnect_cb(struct net_layer *layer, int status)
{
    struct layer_mqtt *ctx = (struct layer_mqtt *)layer;

    CALL_UPPER(ndisconnect_cb, status);
    return 0;
}



int mqtt_init(struct layer_mqtt *ctx)
{
    memset(ctx, 0, sizeof(struct layer_mqtt));

    ctx->base.layer_tag = LAYER_MQTT;
    ctx->base.nconfig  = layer_mqtt_config;
    ctx->base.nconnect = layer_mqtt_connect;
    ctx->base.nsend    = layer_mqtt_send;
    ctx->base.ndisconnect_cb = disconnect_cb;
    ctx->base.nclose   = mqtt_close;
    ctx->base.nenable_rd_evt = layer_mqtt_enable_rd_evt;

    ctx->msg_id = 1;
    ctx->send_buf = malloc(MQTT_BUF_SIZE);
    ctx->read_buf = malloc(MQTT_BUF_SIZE);

    ctx->state = S_MQTT_INITED;

    return 0;
}