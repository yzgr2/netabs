#include <stdio.h>
#include <string.h>

#include "net_layer.h"
#include "layer_tcp.h"
#include "net_evt.h"

extern int tcp_init(struct layer_tcp *ctx);

char *HTTP_GET = "GET / HTTP/1.1\r\n\
Host: www.baidu.com\r\n\
User-Agent: Mozilla/5.0\r\n\
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n\
Accept-Language: en-US,en;q=0.5\r\n\
Accept-Encoding: gzip, deflate\r\n\r\n";

static int tcp_layer_conn_cb(struct net_layer *layer, int status)
{
    printf("[http] test1: tcp connect result: %d\n", status);

    printf("[http] send test data.\n");

    struct net_layer *tcp_layer = layer->lower;
    int n;

    n = tcp_layer->nsend(tcp_layer, HTTP_GET, strlen(HTTP_GET), 0);
    printf("[http] send ret=%d\n", n);
}

static int http_recv_cb(struct net_layer *layer, void *msg, uint32_t len, int flags)
{
    printf("[http] recv[len=%d] : %s", len, (char *)msg);
}

static int http_handle_disconnect(struct net_layer *layer, int code)
{
    printf("[http] tcp disconnected, code=%d", code);
}

int main(int argc, char *argv[])
{
    int ret;

    struct net_layer test_http = {0};
    struct layer_tcp net_tcp = {0};

    select_init();

    test_http.lower = (struct net_layer *)&net_tcp;
    test_http.nconnect_cb = tcp_layer_conn_cb;
    test_http.nrcv_cb = http_recv_cb;
    test_http.ndisconnect_cb = http_handle_disconnect;

    tcp_init(&net_tcp);
    net_tcp.base.upper = &test_http;

    struct tcp_conn_param par = {0};
    par.host = "163.177.151.110";
    par.port = 80;

    ret = net_tcp.base.nconnect( test_http.lower, (struct net_conn_param *)&par);
    if( ret < 0 ) {
        printf("tcp connect ret=%d", ret);
    }

    while(1) {
        int count  = 0;
        count = select_process_events(5000);

        if( count > 0 ) {
            select_handle_fired_events();
        } else {
            printf("no evt in 5000 ms, count=%d\n", count);
        }
    }

    printf("\n[] exit test\n");

}