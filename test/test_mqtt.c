#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "net_layer.h"
#include "layer_tcp.h"
#include "layer_tls.h"
#include "layer_mqtt.h"

#include "net_evt.h"
#include "net_timer.h"
#include "mqtt_client.h"

extern int tcp_init(struct layer_tcp *ctx);

char *HTTP_GET = "GET / HTTP/1.1\r\n\
Host: www.baidu.com\r\n\
User-Agent: Mozilla/5.0\r\n\
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n\
Accept-Language: en-US,en;q=0.5\r\n\
Accept-Encoding: gzip, deflate\r\n\r\n";

//test.mosquitto.org CA
char *ca_pem = "-----BEGIN CERTIFICATE-----\r\n\
MIIEAzCCAuugAwIBAgIUBY1hlCGvdj4NhBXkZ/uLUZNILAwwDQYJKoZIhvcNAQEL\r\n\
BQAwgZAxCzAJBgNVBAYTAkdCMRcwFQYDVQQIDA5Vbml0ZWQgS2luZ2RvbTEOMAwG\r\n\
A1UEBwwFRGVyYnkxEjAQBgNVBAoMCU1vc3F1aXR0bzELMAkGA1UECwwCQ0ExFjAU\r\n\
BgNVBAMMDW1vc3F1aXR0by5vcmcxHzAdBgkqhkiG9w0BCQEWEHJvZ2VyQGF0Y2hv\r\n\
by5vcmcwHhcNMjAwNjA5MTEwNjM5WhcNMzAwNjA3MTEwNjM5WjCBkDELMAkGA1UE\r\n\
BhMCR0IxFzAVBgNVBAgMDlVuaXRlZCBLaW5nZG9tMQ4wDAYDVQQHDAVEZXJieTES\r\n\
MBAGA1UECgwJTW9zcXVpdHRvMQswCQYDVQQLDAJDQTEWMBQGA1UEAwwNbW9zcXVp\r\n\
dHRvLm9yZzEfMB0GCSqGSIb3DQEJARYQcm9nZXJAYXRjaG9vLm9yZzCCASIwDQYJ\r\n\
KoZIhvcNAQEBBQADggEPADCCAQoCggEBAME0HKmIzfTOwkKLT3THHe+ObdizamPg\r\n\
UZmD64Tf3zJdNeYGYn4CEXbyP6fy3tWc8S2boW6dzrH8SdFf9uo320GJA9B7U1FW\r\n\
Te3xda/Lm3JFfaHjkWw7jBwcauQZjpGINHapHRlpiCZsquAthOgxW9SgDgYlGzEA\r\n\
s06pkEFiMw+qDfLo/sxFKB6vQlFekMeCymjLCbNwPJyqyhFmPWwio/PDMruBTzPH\r\n\
3cioBnrJWKXc3OjXdLGFJOfj7pP0j/dr2LH72eSvv3PQQFl90CZPFhrCUcRHSSxo\r\n\
E6yjGOdnz7f6PveLIB574kQORwt8ePn0yidrTC1ictikED3nHYhMUOUCAwEAAaNT\r\n\
MFEwHQYDVR0OBBYEFPVV6xBUFPiGKDyo5V3+Hbh4N9YSMB8GA1UdIwQYMBaAFPVV\r\n\
6xBUFPiGKDyo5V3+Hbh4N9YSMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEL\r\n\
BQADggEBAGa9kS21N70ThM6/Hj9D7mbVxKLBjVWe2TPsGfbl3rEDfZ+OKRZ2j6AC\r\n\
6r7jb4TZO3dzF2p6dgbrlU71Y/4K0TdzIjRj3cQ3KSm41JvUQ0hZ/c04iGDg/xWf\r\n\
+pp58nfPAYwuerruPNWmlStWAXf0UTqRtg4hQDWBuUFDJTuWuuBvEXudz74eh/wK\r\n\
sMwfu1HFvjy5Z0iMDU8PUDepjVolOCue9ashlS4EB5IECdSR2TItnAIiIwimx839\r\n\
LdUdRudafMu5T5Xma182OC0/u/xRlEm+tvKGGmfFcN0piqVl8OrSPBgIlb+1IKJE\r\n\
m/XriWr/Cq4h/JfB7NTsezVslgkBaoU=\r\n\
-----END CERTIFICATE-----\r\n";

void on_mqtt_publish_result(struct layer_mqtt *ctx, int msg_id, int result)
{
    printf("PUB result: msg_id=%d result=%d\n", msg_id, result);
}

int app_handle_conn_cb(struct net_layer *layer, CONN_RESULT status)
{
    printf("[APP] connect result: %d\n", status);

    if( status == CONN_SUCCESS ) {
        struct layer_mqtt *mqtt_ctx = layer->lower;

        //enable rx.
        printf("[APP] enable mqtt layer rx\n");
        mqtt_ctx->base.nenable_rd_evt(mqtt_ctx, 1);

        mqtt_publish(mqtt_ctx, "yzg/t001", "Hello", 5, 1, on_mqtt_publish_result);
    } else {
        //failed, close connection.
    }

    return 0;
}

int http_recv_cb(struct net_layer *layer, void *msg, uint32_t len, int flags)
{
    if( !msg ) {
        printf("lower layer need us call lower->nread() to get data directly.");
    } else {
        printf("[http] recv[len=%d] : %s", len, (char *)msg);
    }

    return 0;
}

int http_handle_disconnect(struct net_layer *layer, int code)
{
    printf("[http] tcp disconnected, code=%d", code);

    //close conn.
    struct net_layer *lower = layer->lower;
    lower->nclose(lower);

    printf("[http] close current connection");
    usleep(1000*1000*4);

    return 0;
}

gw_timer_t* demo_timer;

int demo_func(void *arg)
{
    static int cnt = 0;
    static clock_time_t start_t = 0;

    if( start_t == 0) {
        start_t = gw_current_time_ms();
    }

    printf("---- demo timer run ---cnt=%d  delta_time=%llu s\n", cnt, (gw_current_time_ms() - start_t )/1000 );
    ++cnt;

    gw_start_timer(demo_timer, 0, 3000);

    return 0;
}

void create_demo_timer()
{
    demo_timer = gw_create_timer(NULL, NULL, demo_func);
    gw_start_timer(demo_timer, 0, 2000);
}

#define NON_BLOCKING_MODE   1

int main(int argc, char *argv[])
{
    int ret;

    struct net_layer  app_layer = {0};
    struct layer_mqtt net_mqtt = {0};
    struct layer_tls  net_tls = {0};
    struct layer_tcp  net_tcp = {0};

    select_init();

    tcp_init(&net_tcp);
    tls_init(&net_tls);
    mqtt_init(&net_mqtt);

    net_tcp.base.upper = &net_tls;

    net_tls.base.lower = (struct net_layer *)&net_tcp;
    net_tls.base.upper = &net_mqtt;

    net_mqtt.base.lower = (struct net_layer *)&net_tls;
    net_mqtt.base.upper = &app_layer;

    app_layer.lower = &net_mqtt;
    app_layer.nconnect_cb = app_handle_conn_cb;

    // net_mqtt.base.nrcv_cb    = http_recv_cb;
    // net_mqtt.base.ndisconnect_cb = http_handle_disconnect;

    struct mqtt_conn_param mqtt_par = {0};
    mqtt_par.base.non_block = NON_BLOCKING_MODE;
    net_mqtt.base.nconfig( (struct net_layer *)&net_mqtt, (struct net_conn_param *)&mqtt_par);

    struct tls_conn_param par = {0};
    par.host = "test.mosquitto.org";
    par.port = 8883;  //1883 for plain tcp
    par.ca_cert = ca_pem;
    par.ca_cert_len = strlen(ca_pem) + 1;
    par.our_cert = NULL;
    par.our_cert_len = 0;
    par.base.non_block = NON_BLOCKING_MODE;
    net_tls.base.nconfig( (struct net_layer *)&net_tls, (struct net_conn_param *)&par);

    struct tcp_conn_param par2 = {0};
    par2.host = par.host;
    par2.port = par.port;
    par2.base.non_block = par.base.non_block;
    net_tcp.base.nconfig( (struct net_layer *)&net_tcp, (struct net_conn_param *)&par2);

    create_demo_timer();

    ret = net_mqtt.base.nconnect( &net_mqtt );
    if( ret < 0 ) {
        printf("[test] mqtt connect ret=%d", ret);
    }

#if NON_BLOCKING_MODE
    while(1) {
        int count  = 0;
        count = select_process_events(5000);

        if( count > 0 ) {
            select_handle_fired_events();
        } else if (count == 0 ) {
            printf("no evt in 500ms, timeout.\n");
        }
        else {
            printf("select_process_events  count=%d errno=%d, check it.\n", count, errno);
            usleep(1000*1000*20);
        }
    }
#else
    if( ret == 0 ) {
        printf("[test] tls handshake complete, start sending data.");
    } else {
        return 0;
    }

#endif

    printf("\n[] exit test\n");

    return 0;
}