#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "MQTTPacket.h"
#include "MQTTConnect.h"

#include "utlist.h"
#include "net_timer.h"
#include "layer_mqtt.h"
#include "mqtt_client.h"


struct publish_waitter {
    struct publish_waitter *prev, *next;

    struct layer_mqtt *ctx; //ctx and msg_id is the key.
    int     msg_id;

    mqtt_publish_cb  callback;

    gw_timer_t  *timeout_timer;
};

struct publish_waitter *publish_waitter_queue;

struct subscriber {
    struct subscriber *prev, *next;

    struct layer_mqtt *ctx; //ctx and msg_id is the key.
    char    *topic;
    int     msg_id;

    mqtt_subscribe_cb  callback;
    int rx_cnt;
};
struct subscriber *subscriber_queue;


static int pub_timeout_fn(void *arg)
{
    struct publish_waitter *w = (struct publish_waitter *)arg;

    DL_DELETE(publish_waitter_queue, w);

    w->callback(w->ctx, w->msg_id, PUB_TIMEOUT);

    free(w);

    return 0;
}

static struct publish_waitter *pub_waitter_add(struct layer_mqtt *ctx, int msg_id, mqtt_publish_cb on_publish, int pub_timeout)
{
    struct publish_waitter *w = malloc(sizeof(struct publish_waitter));
    if( !w ) {
        NET_LOG_ERR("OOM for pub_waitter, size=%lu\n", sizeof(struct publish_waitter));
        return NULL;
    }

    w->ctx = ctx;
    w->msg_id = msg_id;
    w->callback = on_publish;
    w->timeout_timer = gw_create_timer("p", w, pub_timeout_fn);
    gw_start_timer(w->timeout_timer, 0, pub_timeout);

    DL_APPEND(publish_waitter_queue, w);
    return w;
}

static void handle_pub_ack(struct layer_mqtt *ctx, int msg_id)
{
    struct publish_waitter *w, *tmp;

    DL_FOREACH_SAFE(publish_waitter_queue, w, tmp) {
        if( w->msg_id == msg_id && w->ctx == ctx ) {
            DL_DELETE(publish_waitter_queue, w);
            break;
        }
    }

    //stop & free timer.
    gw_del_timer(w->timeout_timer);
    w->timeout_timer = NULL;

    w->callback(w->ctx, w->msg_id, PUB_SUCCESS);

    free(w);
}

void mqtt_clean_all_pub_waitter(struct layer_mqtt *ctx)
{
    struct publish_waitter *w, *tmp;

    DL_FOREACH_SAFE(publish_waitter_queue, w, tmp) {
        if( w->ctx == ctx ) {
            DL_DELETE(publish_waitter_queue, w);
            //stop & free timer.
            gw_del_timer(w->timeout_timer);
            w->timeout_timer = NULL;

            free(w);
        }
    }
}


//===================
struct subscriber *subscriber_add(struct layer_mqtt *ctx, int msg_id, char *topic, mqtt_subscribe_cb on_subscribe)
{
    struct subscriber *s = malloc(sizeof(struct subscriber));
    if( !s ) {
        return NULL;
    }

    s->ctx = ctx;
    string_assign( &s->topic, topic);
    s->msg_id = msg_id;
    s->callback = on_subscribe;
    s->rx_cnt = 0;

    DL_APPEND(subscriber_queue, s);
    return s;
}

static void handle_sub_msg(struct layer_mqtt *ctx, int msg_id, char *topic, void *msg, int msg_len)
{
    struct subscriber *s, *tmp;

    if( !topic ) {
        return ;
    }

    //TODO: strlen(topic) check.

    DL_FOREACH_SAFE(subscriber_queue, s, tmp) {
        if( !strcmp(s->topic,topic) && s->ctx == ctx ) {
            DL_DELETE(subscriber_queue, s);
            break;
        }
    }

    s->callback(ctx, msg_id, topic, msg, msg_len );

    free(s);
}

void mqtt_clean_all_subscriber(struct layer_mqtt *ctx)
{
    struct subscriber *s, *tmp;

    DL_FOREACH_SAFE(subscriber_queue, s, tmp) {
        if( s->ctx == ctx ) {
            DL_DELETE(subscriber_queue, s);
            free(s);
        }
    }
}

//==================================

uint16_t get_msg_id(struct layer_mqtt *ctx)
{
    uint16_t msg_id = ctx->msg_id;

    if( msg_id == 0 ) {
        msg_id = 1;
        ctx->msg_id = 1;
    } else {
        ++ctx->msg_id;
    }

    return msg_id;
}

int mqtt_publish(struct layer_mqtt *ctx, char *topic, unsigned char *msg, int msg_len, int qos, mqtt_publish_cb on_publish)
{
    uint32_t msg_id = get_msg_id(ctx);

    MQTTString m_topic = {topic, {0, NULL}};

    //build publish msg.
    int len = MQTTSerialize_publish( ctx->send_buf, MQTT_BUF_SIZE,
			0, qos, 0, msg_id,
            m_topic,
            msg, msg_len);
    if( len <= 0) {
        NET_LOG_ERR("MQTTSerialize_publish failed. ret=%d\n", len);
        return len;
    }
    ctx->send_buf_len = len;

    int ret;
    ret = ctx->base.nsend( (struct net_layer *)ctx, ctx->send_buf, ctx->send_buf_len, 0);
    if( ret <= 0 ) {
        NET_LOG_ERR("mqtt send failed. ret=%d errno=%d", ret, errno);
        return ret;
    }

    ctx->base.nenable_rd_evt( (struct net_layer *)ctx, 1);

    struct publish_waitter *w = pub_waitter_add(ctx, msg_id, on_publish, MQTT_PUBLISH_TIMEOUT);
    if( w ) {
        return msg_id;
    } else {
        return -1;
    }
}


int mqtt_subscribe(struct layer_mqtt *ctx, char *topic, int qos, int *msg_id, mqtt_subscribe_cb on_subscribe)
{
    *msg_id =  get_msg_id(ctx);
    MQTTString m_topic = {topic, {0, NULL}};

    int len = MQTTSerialize_subscribe(ctx->send_buf, ctx->send_buf_len, 0, *msg_id, 0, &m_topic, &qos);
    if( len <= 0 ) {
        NET_LOG_ERR("MQTTSerialize_subscribe failed. ret=%d\n", len);
        return len;
    }
    ctx->send_buf_len = len;

    int ret;
    ret = ctx->base.nsend((struct net_layer *)ctx, ctx->send_buf, ctx->send_buf_len, 0);
    if( ret <= 0 ) {
        NET_LOG_ERR("mqtt send failed. ret=%d errno=%d", ret, errno);
        return ret;
    }

    ctx->base.nenable_rd_evt((struct net_layer *)ctx, 1);

    struct subscriber *s = subscriber_add(ctx, *msg_id, topic, on_subscribe);
    if( !s ) {
        return *msg_id;
    } else {
        return -1;
    }
}



//============================================

typedef struct sck {
	char *buf;
	size_t buf_len;
	size_t use_ptr;
}sck_t;

int transport_getdatanb(void *sck, unsigned char *buf, int count)
{
	sck_t *st = (sck_t *)sck;
	size_t cl = count;
	if (st->buf_len - st->use_ptr < count ) {
		cl = st->buf_len - st->use_ptr;
	}
	memcpy(buf, st->buf+st->use_ptr, cl);
	st->use_ptr += cl;
	return cl;
}

int decode_mqtt_packet(void *msg , uint32_t len, void *out_buf, int out_buf_len)
{
    sck_t sck = {0x00};

	MQTTTransport mytransport = {0x00};

	sck.buf = msg;
	sck.buf_len = len;
	sck.use_ptr = 0;
	mytransport.sck	= &sck;
	mytransport.getfn = transport_getdatanb;
	mytransport.state = 0;

	int pkt_type = MQTTPacket_readnb(out_buf, out_buf_len, &mytransport);

    return pkt_type;
}

#define MAX_RECV_BUF_LEN 4096

/**
 * @brief : handle on_recv event after mqtt connection is established.
 * @param ctx
 * @param msg
 * @param len
 * @return int
 */
int mqtt_on_recv(struct layer_mqtt *ctx, void *msg, uint32_t len, int flags)
{
    unsigned char buf[2048] = {0x00};
	int buf_len = sizeof(buf);
    int rc = 0;

    unsigned char *inner_buf;

    if( !msg ) {
        //read data actively.
        inner_buf = malloc(MAX_RECV_BUF_LEN);
        if( !inner_buf ) {
            return -1;
        }

        struct net_layer *lower = ctx->base.lower;
        int buf_len = lower->nread(lower, buf, MAX_RECV_BUF_LEN, 0);
        if( buf_len < 0 ) {
            if( ctx->base.ndisconnect_cb ) {
                ctx->base.ndisconnect_cb((struct net_layer *)ctx, 0);
            }
            free(inner_buf);
            return buf_len;
        } else if ( buf_len == 0 ) {
            //disconnected by peer.
            if( ctx->base.ndisconnect_cb ) {
                ctx->base.ndisconnect_cb((struct net_layer *)ctx, 0);
            }
            free(inner_buf);
            return buf_len;
        }
    }

    //enable next rx cb.
    ctx->base.nenable_rd_evt((struct net_layer *)ctx, 1);

    int pkt_type = decode_mqtt_packet(msg, len, buf, buf_len);
    switch(pkt_type) {
        case CONNACK:   //should not get connack msg after connection ok.
            break;
        case PUBREC: //qos2 not support;
            break;
        case PUBACK:    //client send publish, ACK from server
        {
            unsigned char type, dup;
            unsigned short msg_id;
            rc = MQTTDeserialize_ack(&type, &dup, &msg_id, buf, buf_len);
            if( rc && type == PUBACK) {
                handle_pub_ack(ctx, msg_id);
                break;
            }
            break; //decode failure
        }
        case PUBLISH:   //client send subscribe , msg from server.
        {
            MQTTString topic = MQTTString_initializer;
            unsigned char dup, retained;
            unsigned short msg_id;
            int qos;
            unsigned char *payload; //will point to buffer.
            int payload_len;
            rc = MQTTDeserialize_publish(&dup, &qos,
                    &retained, &msg_id, &topic,
                    &payload, &payload_len,
                    buf, buf_len);
            if( rc ) {
                handle_sub_msg(ctx, msg_id, topic.cstring, payload, payload_len);
                break;
            }
            break;
        }
        case SUBACK:    //client send subscribe , ACK from server.
            break;
        default:
            break;
    }

    if(inner_buf) {
        free(inner_buf);
    }

    return 0;
}


