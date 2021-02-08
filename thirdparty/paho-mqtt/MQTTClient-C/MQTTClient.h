/**
 * Copyright (c) 2008-2019, OPPO Mobile Comm Corp., Ltd
 * VENDOR_EDIT
 * @file MQTTClient.h
 * @brief mqtt客户端实现
 * @version 0.1
 * @date 2019-08-01
 * @author WatWu
 */

#if !defined(MQTT_CLIENT_H)
#define MQTT_CLIENT_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "MQTTPacket.h"
#include "hey/net/tls.h"

#define ON_LINE 1  //mqtt ready (login success)
#define OFF_LINE 0 //disconnected
#define TLS_OK 2   //tls/tcp ready

#define MAX_PACKET_ID 65535 /* according to the MQTT specification - do not change! */

#if !defined(MAX_MESSAGE_HANDLERS)
#define MAX_MESSAGE_HANDLERS 5 /* redefinable - how many subscriptions do you want? */
#endif

enum QoS { QOS0,
           QOS1,
           QOS2,
           SUBFAIL = 0x80 };

/* all failure return codes must be negative */
enum returnCode { BUFFER_OVERFLOW = -2,
                  FAILURE = -1,
                  SUCCESS = 0 };

typedef struct MQTTMessage {
    enum QoS qos;
    unsigned char retained;
    unsigned char dup;
    unsigned short id;
    void *payload;
    size_t payloadlen;
} MQTTMessage;

typedef struct MessageData {
    MQTTMessage *message;
    MQTTString *topicName;
} MessageData;

typedef struct MQTTConnackData {
    unsigned char rc;
    unsigned char sessionPresent;
} MQTTConnackData;

typedef struct MQTTSubackData {
    enum QoS grantedQoS;
} MQTTSubackData;

struct MQTTClient;

typedef void (*messageHandler)(MessageData *);

typedef ssize_t (*mqtt_send_raw_func)(struct MQTTClient *c, const uint8_t *buf, size_t size);

typedef struct MQTTClient {
    unsigned int next_packetid, command_timeout_ms;
    size_t sendbuf_size, readbuf_size;
    unsigned char *sendbuf, *readbuf;
    unsigned int keepAliveInterval;
    char ping_outstanding;
    int isconnected;
    int cleansession;
    MQTTPacket_connectData *conn_data;

    struct MessageHandlers {
        const char *topicFilter;

        void (*fp)(MessageData *);
    } messageHandlers[MAX_MESSAGE_HANDLERS]; /* Message handlers are indexed by subscription topic */

    void (*defaultMessageHandler)(MessageData *);

    mqtt_send_raw_func send_raw;

} MQTTClient;

#define DefaultClient                   \
    {                                   \
        0, 0, 0, 0, NULL, NULL, 0, 0, 0 \
    }

/**
 * @brief 初始化mqtt客户端
 * @param client                [mqtt客户端对象指针]
 * @param tls_net               [传输层网络连接接口]
 * @param command_timeout_ms    [命令超时时间]
 * @param sendbuf               [发送缓存区]
 * @param sendbuf_size          [发送缓存区大小]
 * @param readbuf               [接收缓存区]
 * @param readbuf_size          [接收缓存区大小]
 */
void MQTTClientInit(MQTTClient *client, unsigned int command_timeout_ms,
                    unsigned char *sendbuf, size_t sendbuf_size, unsigned char *readbuf, size_t readbuf_size,
                    mqtt_send_raw_func send_raw);

/**
 * @brief mqtt读取数据包，读取接收到的数据包到readbuf
 * @param client    [mqtt客户端对象]
 * @param data      [data]
 * @param data_len  [data length]
 * @return int      [读取到的数据包mqtt类型]
 */
int MQTTreadPacket(MQTTClient *client, void *data, uint16_t data_len);

/**
 * @brief 处理mqtt接收数据
 * @param client [mqtt客户端对象]
 * @return int   [处理结果，成功则返回数据包类型，失败则返回错误]
 */
int MQTTcycle(MQTTClient *client);

/**
 * @brief 判断上一次数据的收发是否超过了心跳周期，超过了则发送一包ping心跳数据（MQTTSerialize_pingreq）==todo==
 * @param client [mqtt客户端对象]
 * @return int   [心跳存活状态]
 */
int keepalive(MQTTClient *client);

/**
 * @brief 返回mqtt连接状态
 * @param client [mqtt客户端对象]
 * @return int   [连接状态]
 */
int MQTTIsConnected(MQTTClient *client);

/**
 * @brief mqtt连接，等待响应
 * @param client    [mqtt客户端对象]
 * @param options   [mqtt连接报文的参数数据]
 * @param data      [mqtt连接响应报文的数据]
 * @return int      [连接结果]
 */
int MQTTConnectWithResults(MQTTClient *client, MQTTPacket_connectData *options, MQTTConnackData *data);

/**
 * @brief mqtt连接，忽略响应报文
 * @param client    [mqtt客户端对象]
 * @param options   [mqtt连接报文的参数数据]
 * @return int      [连接结果]
 */
int MQTTConnect(MQTTClient *client, MQTTPacket_connectData *options);

/**
 * @brief 设置消息主题的回调函数
 * @param client            [mqtt客户端对象]
 * @param topicFilter       [订阅的主题]
 * @param messageHandler    [对应主题的回调函数]
 * @return int              [设置结果]
 */
int MQTTSetMessageHandler(MQTTClient *client, const char *topicFilter, messageHandler messageHandler);

/**
 * @brief mqtt订阅主题，等待订阅结果
 * @param client            [mqtt客户端]
 * @param topicFilter       [订阅的主题]
 * @param qos               [消息qos]
 * @param messageHandler    [对应主题的接收回调函数]
 * @param data              [订阅报文的响应数据]
 * @return int              [订阅结果]
 */
int MQTTSubscribeWithResults(MQTTClient *client, const char *topicFilter, enum QoS qos,
                             messageHandler messageHandler, MQTTSubackData *data);

/**
 * @brief mqtt订阅主题，忽略响应报文
 * @param client            [mqtt客户端对象]
 * @param topicFilter       [订阅的主题]
 * @param qos               [消息qos]
 * @param messageHandler    [对应主题的接收回调函数]
 * @return int              [订阅结果]
 */
int MQTTSubscribe(MQTTClient *client, const char *topicFilter, enum QoS qos,
                  messageHandler messageHandler);

/**
 * @brief 取消订阅主题
 * @param client        [mqtt客户端对象]
 * @param topicFilter   [待订阅的主题]
 * @return int          [操作结果]
 */
int MQTTUnsubscribe(MQTTClient *client, const char *topicFilter);

/**
 * @brief 发布消息
 * @param client    [mqtt客户端对象]
 * @param topicName [消息主题]
 * @param message   [消息内容]
 * @return int      [发布结果]
 */
int MQTTPublish(MQTTClient *client, const char *topicName, MQTTMessage *message);

/**
 * @brief 断开mqtt连接
 * @param client [mqtt客户端对象]
 * @return int   [断开结果]
 */
int MQTTDisconnect(MQTTClient *client);

#if defined(__cplusplus)
}
#endif

#endif
