/**
 * Copyright (c) 2008-2019, OPPO Mobile Comm Corp., Ltd
 * VENDOR_EDIT
 * @file MQTTClient.c
 * @brief mqtt客户端实现
 * @version 0.1
 * @date 2019-08-01
 * @author WatWu
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "MQTTClient.h"

/**
 * @brief 新建mqtt消息数据，把消息主题和内容绑定到消息数据体
 * @param md          [消息数据体]
 * @param aTopicName  [消息主题]
 * @param aMessage    [消息内容]
 */
static void NewMessageData(MessageData *md, MQTTString *aTopicName, MQTTMessage *aMessage)
{
    md->topicName = aTopicName;
    md->message = aMessage;
}

/**
 * @brief 获取下一包数据包的id
 * @param client [mqtt客户端对象]
 * @return int   [数据包id]
 */
static int getNextPacketId(MQTTClient *client)
{
    return client->next_packetid = (client->next_packetid == MAX_PACKET_ID) ? 1 : client->next_packetid + 1;
}

/**
 * @brief mqtt读取数据包，读取接收到的数据包到readbuf
 * @param client    [mqtt客户端对象]
 * @param data      [data]
 * @param data_len  [data length]
 * @return int      [读取到的数据包mqtt类型]
 */
int MQTTreadPacket(MQTTClient *client, void *data, uint16_t data_len)
{
    memcpy(client->readbuf, data, data_len);
    client->readbuf[data_len] = 0;
    MQTTHeader header = { 0 };
    header.byte = client->readbuf[0];
    return header.bits.type;
}

/**
 * @brief mqtt发送数据包，把sendbuf中的数据发送出去(==todo==，需要增加超时机制)
 * @param client [mqtt客户端对象]
 * @param length [待发送的长度]
 * @return int   [发送结果]
 */
static int sendPacket(MQTTClient *client, int length)
{
    ssize_t n;
    n = client->send_raw(client, client->sendbuf, length);
    if (n < 0) {
        // FIXME half send
        return FAILURE;
    }
    return SUCCESS;
}

/**
 * @brief 判断主题是否匹配
 * @param topicFilter [源主题]
 * @param topicName   [目标主题]
 * @return char       [判断结果]
 */
// static char isTopicMatched(char* topicFilter, MQTTString* topicName)
// {
//     char* curf = topicFilter;
//     char* curn = topicName->lenstring.data;
//     char* curn_end = curn + topicName->lenstring.len;

//     while(*curf && curn < curn_end) {
//         if(*curn == '/' && *curf != '/') {
//             break;
//         }
//         if(*curf != '+' && *curf != '#' && *curf != *curn) {
//             break;
//         }
//         if(*curf == '+') {
//             // skip until we meet the next separator, or end of string
//             char* nextpos = curn + 1;
//             while(nextpos < curn_end && *nextpos != '/') {
//                 nextpos = ++curn + 1;
//             }
//         } else if(*curf == '#') {
//             curn = curn_end - 1;    // skip until end of string
//         }
//         curf++;
//         curn++;
//     };

//     return (curn == curn_end) && (*curf == '\0');
// }

/**
 * @brief 根据消息主题分发接收的数据，并回调相应处理函数
 * @param client    [mqtt客户端对象]
 * @param topicName [消息主题]
 * @param message   [消息内容]
 * @return int      [分发结果]
 */
static int deliverMessage(MQTTClient *client, MQTTString *topicName, MQTTMessage *message)
{
    // int i;
    int rc = FAILURE;

    // we have to find the right message handler - indexed by topic
    // for(i = 0; i < MAX_MESSAGE_HANDLERS; ++i) {
    //     if(client->messageHandlers[i].topicFilter != 0 && (MQTTPacket_equals(topicName, (char*)client->messageHandlers[i].topicFilter) ||
    //             isTopicMatched((char*)client->messageHandlers[i].topicFilter, topicName))) {
    //         if(client->messageHandlers[i].fp != NULL) {
    //             MessageData md;
    //             NewMessageData(&md, topicName, message);
    //             client->messageHandlers[i].fp(&md);
    //             rc = SUCCESS;
    //         }
    //     }
    // }

    if (client->messageHandlers[0].fp != NULL) {
        MessageData md;
        NewMessageData(&md, topicName, message);
        client->messageHandlers[0].fp(&md);
        rc = SUCCESS;
    }

    if (rc == FAILURE && client->defaultMessageHandler != NULL) {
        MessageData md;
        NewMessageData(&md, topicName, message);
        client->defaultMessageHandler(&md);
        rc = SUCCESS;
    }

    return rc;
}

/**
 * @brief 判断上一次数据的收发是否超过了心跳周期，超过了则发送一包ping心跳数据（MQTTSerialize_pingreq）==todo==
 * @param client [mqtt客户端对象]
 * @return int   [心跳存活状态]
 */
int keepalive(MQTTClient *client)
{
    int rc = SUCCESS;

    if (client->keepAliveInterval == 0) {
        return rc;
    }

    if (client->ping_outstanding) {
        rc = FAILURE; /* PINGRESP not received in keepalive interval */
    } else {
        int len = MQTTSerialize_pingreq(client->sendbuf, client->sendbuf_size);
        if (len > 0 && (rc = sendPacket(client, len)) == SUCCESS) { // send the ping packet
            client->ping_outstanding = 1;
        }
    }

    return rc;
}

/**
 * @brief 清除mqtt会话主题列表
 * @param client [mqtt客户端对象]
 */
static void MQTTCleanSession(MQTTClient *client)
{
    int i = 0;

    for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i) {
        client->messageHandlers[i].topicFilter = NULL;
    }
}

/**
 * @brief 关闭mqtt会话
 * @param client [mqtt客户端对象]
 */
static void MQTTCloseSession(MQTTClient *client)
{
    client->ping_outstanding = 0;
    client->isconnected = 0;
    if (client->cleansession) {
        MQTTCleanSession(client);
    }
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
                    mqtt_send_raw_func send_raw)
{
    int i;

    for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i) {
        client->messageHandlers[i].topicFilter = 0;
    }
    client->command_timeout_ms = command_timeout_ms;
    client->sendbuf = sendbuf;
    client->sendbuf_size = sendbuf_size;
    client->readbuf = readbuf;
    client->readbuf_size = readbuf_size;
    client->isconnected = 0;
    client->cleansession = 0;
    client->ping_outstanding = 0;
    client->defaultMessageHandler = NULL;
    client->next_packetid = 1;
    client->send_raw = send_raw;
}

/**
 * @brief 处理mqtt接收数据，每次有新数据时需要调用一次
 * @param client [mqtt客户端对象]
 * @return int   [处理结果，成功则返回数据包类型，失败则返回错误]
 */
int MQTTcycle(MQTTClient *client)
{
    int len = 0, rc = SUCCESS;

    MQTTHeader header = { 0 };
    header.byte = client->readbuf[0];
    int packet_type = header.bits.type;

    // fprintf(stdout, "Recv packet type : %d \n", packet_type);

    switch (packet_type) {
    default:
        /* no more data to read, unrecoverable. Or read packet fails due to unexpected network error */
        rc = packet_type;
        goto exit;

    case 0: /* timed out reading packet */
        break;

    case CONNACK: {
        MQTTConnackData data;
        data.rc = 0;
        data.sessionPresent = 0;
        if (MQTTDeserialize_connack(&data.sessionPresent, &data.rc, client->readbuf, client->readbuf_size) == 1) {
            rc = data.rc;
        } else {
            rc = FAILURE;
        }
        break;
    }

    case PUBACK: {
        unsigned short mypacketid;
        unsigned char dup, type;
        if (MQTTDeserialize_ack(&type, &dup, &mypacketid, client->readbuf, client->readbuf_size) != 1) {
            rc = FAILURE;
        }
        break;
    }

    case SUBACK: {
        int count = 0;
        unsigned short mypacketid;
        MQTTSubackData data;
        data.grantedQoS = QOS0;
        if (MQTTDeserialize_suback(&mypacketid, 1, &count, (int *)&(data.grantedQoS), client->readbuf, client->readbuf_size) == 1) {
            if (data.grantedQoS != 0x80) {
            }
        }
        break;
    }

    case UNSUBACK: {
        unsigned short mypacketid; // should be the same as the packetid above
        if (MQTTDeserialize_unsuback(&mypacketid, client->readbuf, client->readbuf_size) == 1) {
        }
        break;
    }

    case PUBLISH: {
        MQTTString topicName;
        MQTTMessage msg;
        int intQoS;
        msg.payloadlen = 0; /* this is a size_t, but deserialize publish sets this as int */
        if (MQTTDeserialize_publish(&msg.dup, &intQoS, &msg.retained, &msg.id, &topicName,
                                    (unsigned char **)&msg.payload, (int *)&msg.payloadlen, client->readbuf, client->readbuf_size) != 1) {
            goto exit;
        }
        msg.qos = (enum QoS)intQoS;
        deliverMessage(client, &topicName, &msg);
        client->readbuf[0] = 0;
        if (msg.qos != QOS0) {
            if (msg.qos == QOS1) {
                len = MQTTSerialize_ack(client->sendbuf, client->sendbuf_size, PUBACK, 0, msg.id);
            } else if (msg.qos == QOS2) {
                len = MQTTSerialize_ack(client->sendbuf, client->sendbuf_size, PUBREC, 0, msg.id);
            }
            if (len <= 0) {
                rc = FAILURE;
            } else {
                rc = sendPacket(client, len);
            }
            if (rc == FAILURE) {
                goto exit; // there was a problem
            }
        }
        break;
    }

    case PUBREC:
    case PUBREL: {
        unsigned short mypacketid;
        unsigned char dup, type;
        if (MQTTDeserialize_ack(&type, &dup, &mypacketid, client->readbuf, client->readbuf_size) != 1) {
            rc = FAILURE;
        } else if ((len = MQTTSerialize_ack(client->sendbuf, client->sendbuf_size,
                                            (packet_type == PUBREC) ? PUBREL : PUBCOMP, 0, mypacketid)) <= 0) {
            rc = FAILURE;
        } else if ((rc = sendPacket(client, len)) != SUCCESS) { // send the PUBREL packet
            rc = FAILURE;                                       // there was a problem
        }
        if (rc == FAILURE) {
            goto exit; // there was a problem
        }
        break;
    }

    case PUBCOMP: {
        unsigned short mypacketid;
        unsigned char dup, type;
        if (MQTTDeserialize_ack(&type, &dup, &mypacketid, client->readbuf, client->readbuf_size) != 1) {
            rc = FAILURE;
        }
        break;
    }

    case PINGRESP:
        client->ping_outstanding = 0;
        break;

    case DISCONNECT: {
        if (NULL != client->sendbuf) {
            iot_free(client->sendbuf);
        }
        if (NULL != client->readbuf) {
            iot_free(client->readbuf);
        }
        if (NULL != client->conn_data) {
            iot_free(client->conn_data);
        }
        break;
    }
    }

    // if(keepalive(client) != SUCCESS) {
    //     //check only keepalive FAILURE status so that previous FAILURE status can be considered as FAULT
    //     rc = FAILURE;
    // }

exit:
    if (rc == SUCCESS) {
        rc = packet_type;
    } else if (client->isconnected) {
        MQTTCloseSession(client);
    }

    return rc;
}

/**
 * @brief 返回mqtt连接状态
 * @param client [mqtt客户端对象]
 * @return int   [连接状态]
 */
int MQTTIsConnected(MQTTClient *client)
{
    return client->isconnected;
}

/**
 * @brief mqtt连接，不等待响应
 * @param client    [mqtt客户端对象]
 * @param options   [mqtt连接报文的参数数据]
 * @return int      [连接结果]
 */
int MQTTConnectWithOutResults(MQTTClient *client, MQTTPacket_connectData *options)
{
    int rc = FAILURE;
    MQTTPacket_connectData default_options = MQTTPacket_connectData_initializer;
    int len = 0;

    if (client->isconnected) { /* don't send connect packet again if we are already connected */
        goto exit;
    }

    if (options == 0) {
        options = &default_options; /* set default options if none were supplied */
    }

    client->keepAliveInterval = options->keepAliveInterval;
    client->cleansession = options->cleansession;
    if ((len = MQTTSerialize_connect(client->sendbuf, client->sendbuf_size, options)) <= 0) {
        goto exit;
    }
    if ((rc = sendPacket(client, len)) != SUCCESS) { // send the connect packet
        goto exit;                                   // there was a problem
    }

exit:
    if (rc == SUCCESS) {
        client->isconnected = 1;
        client->ping_outstanding = 0;
    }

    return rc;
}

/**
 * @brief mqtt连接，忽略响应报文
 * @param client    [mqtt客户端对象]
 * @param options   [mqtt连接报文的参数数据]
 * @return int      [连接结果]
 */
int MQTTConnect(MQTTClient *client, MQTTPacket_connectData *options)
{
    return MQTTConnectWithOutResults(client, options);
}

/**
 * @brief 设置消息主题的回调函数
 * @param client            [mqtt客户端对象]
 * @param topicFilter       [订阅的主题]
 * @param messageHandler    [对应主题的回调函数]
 * @return int              [设置结果]
 */
int MQTTSetMessageHandler(MQTTClient *client, const char *topicFilter, messageHandler messageHandler)
{
    int rc = FAILURE;
    int i = -1;

    /* first check for an existing matching slot */
    for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i) {
        if (client->messageHandlers[i].topicFilter != NULL && strcmp(client->messageHandlers[i].topicFilter, topicFilter) == 0) {
            if (messageHandler == NULL) { /* remove existing */
                client->messageHandlers[i].topicFilter = NULL;
                client->messageHandlers[i].fp = NULL;
            }
            rc = SUCCESS; /* return i when adding new subscription */
            break;
        }
    }
    /* if no existing, look for empty slot (unless we are removing) */
    if (messageHandler != NULL) {
        if (rc == FAILURE) {
            for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i) {
                if (client->messageHandlers[i].topicFilter == NULL) {
                    rc = SUCCESS;
                    break;
                }
            }
        }
        if (i < MAX_MESSAGE_HANDLERS) {
            client->messageHandlers[i].topicFilter = topicFilter;
            client->messageHandlers[i].fp = messageHandler;
        }
    }
    return rc;
}

/**
 * @brief mqtt订阅主题，不等待订阅结果
 * @param client            [mqtt客户端]
 * @param topicFilter       [订阅的主题]
 * @param qos               [消息qos]
 * @param messageHandler    [对应主题的接收回调函数]
 * @return int              [订阅结果]
 */
int MQTTSubscribeWithOutResults(MQTTClient *client, const char *topicFilter, enum QoS qos, messageHandler messageHandler)
{
    int rc = FAILURE;
    int len = 0;
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)topicFilter;

    if (!client->isconnected) {
        goto exit;
    }

    len = MQTTSerialize_subscribe(client->sendbuf, client->sendbuf_size, 0, getNextPacketId(client), 1, &topic, (int *)&qos);
    if (len <= 0) {
        goto exit;
    }
    if ((rc = sendPacket(client, len)) != SUCCESS) { // send the subscribe packet
        goto exit;                                   // there was a problem
    }

    rc = MQTTSetMessageHandler(client, topicFilter, messageHandler);

exit:
    if (rc == FAILURE) {
        MQTTCloseSession(client);
    }

    return rc;
}

/**
 * @brief mqtt订阅主题，忽略响应报文
 * @param client            [mqtt客户端对象]
 * @param topicFilter       [订阅的主题]
 * @param qos               [消息qos]
 * @param messageHandler    [对应主题的接收回调函数]
 * @return int              [订阅结果]
 */
int MQTTSubscribe(MQTTClient *client, const char *topicFilter, enum QoS qos, messageHandler messageHandler)
{
    return MQTTSubscribeWithOutResults(client, topicFilter, qos, messageHandler);
}

/**
 * @brief 取消订阅主题
 * @param client        [mqtt客户端对象]
 * @param topicFilter   [待订阅的主题]
 * @return int          [操作结果]
 */
int MQTTUnsubscribe(MQTTClient *client, const char *topicFilter)
{
    int rc = FAILURE;
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)topicFilter;
    int len = 0;

    if (!client->isconnected) {
        goto exit;
    }

    if ((len = MQTTSerialize_unsubscribe(client->sendbuf, client->sendbuf_size, 0, getNextPacketId(client), 1, &topic)) <= 0) {
        goto exit;
    }
    if ((rc = sendPacket(client, len)) != SUCCESS) { // send the subscribe packet
        goto exit;                                   // there was a problem
    }

    /* remove the subscription message handler associated with this topic, if there is one */
    MQTTSetMessageHandler(client, topicFilter, NULL);

exit:
    if (rc == FAILURE) {
        MQTTCloseSession(client);
    }

    return rc;
}

/**
 * @brief 发布消息
 * @param client    [mqtt客户端对象]
 * @param topicName [消息主题]
 * @param message   [消息内容]
 * @return int      [发布结果]
 */
int MQTTPublish(MQTTClient *client, const char *topicName, MQTTMessage *message)
{
    int rc = FAILURE;
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)topicName;
    int len = 0;

    if (!client->isconnected) {
        goto exit;
    }

    if (message->qos == QOS1 || message->qos == QOS2) {
        message->id = getNextPacketId(client);
    }

    len = MQTTSerialize_publish(client->sendbuf, client->sendbuf_size, 0, message->qos, message->retained, message->id,
                                topic, (unsigned char *)message->payload, message->payloadlen);
    if (len <= 0) {
        goto exit;
    }
    if ((rc = sendPacket(client, len)) != SUCCESS) { // send the subscribe packet
        goto exit;                                   // there was a problem
    }

exit:
    if (rc == FAILURE) {
        MQTTCloseSession(client);
    }

    return rc;
}

/**
 * @brief 断开mqtt连接
 * @param client [mqtt客户端对象]
 * @return int   [断开结果]
 */
int MQTTDisconnect(MQTTClient *client)
{
    int rc = FAILURE;
    int len = 0;

    len = MQTTSerialize_disconnect(client->sendbuf, client->sendbuf_size);
    if (len > 0) {
        rc = sendPacket(client, len); // send the disconnect packet
    }
    MQTTCloseSession(client);

    return rc;
}
