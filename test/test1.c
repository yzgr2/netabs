#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "net_layer.h"
#include "layer_tcp.h"
#include "layer_tls.h"
#include "net_evt.h"
#include "net_timer.h"


extern int tcp_init(struct layer_tcp *ctx);

char *HTTP_GET = "GET / HTTP/1.1\r\n\
Host: www.baidu.com\r\n\
User-Agent: Mozilla/5.0\r\n\
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n\
Accept-Language: en-US,en;q=0.5\r\n\
Accept-Encoding: gzip, deflate\r\n\r\n";

char *ca_pem = "-----BEGIN CERTIFICATE-----\r\n\
MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G\r\n\
A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\r\n\
Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\r\n\
MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\r\n\
A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\r\n\
hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\r\n\
RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\r\n\
gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\r\n\
KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\r\n\
QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ\r\n\
XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw\r\n\
DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o\r\n\
LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU\r\n\
RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp\r\n\
jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK\r\n\
6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\r\n\
mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\r\n\
Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\r\n\
WD9f\r\n\
-----END CERTIFICATE-----";

static int tls_layer_conn_cb(struct net_layer *layer, CONN_RESULT status)
{
    printf("[http] test1: tcp connect result: %d\n", status);

    if( status == CONN_SUCCESS ) {
        printf("[http] send test data.\n");

        struct net_layer *lower = layer->lower;
        int n = lower->nsend(lower, HTTP_GET, strlen(HTTP_GET), 0);

        printf("[http] send ret=%d\n", n);
    }

    return 0;
}

static int http_recv_cb(struct net_layer *layer, void *msg, uint32_t len, int flags)
{
    if( !msg ) {
        printf("lower layer need us call lower->nread() to get data directly.");
    } else {
        printf("[http] recv[len=%d] : %s", len, (char *)msg);
    }

    return 0;
}

static int http_handle_disconnect(struct net_layer *layer, int code)
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

#define NON_BLOCKING_MODE   0

int main(int argc, char *argv[])
{
    int ret;

    struct net_layer test_http = {0};
    struct layer_tls net_tls = {0};
    struct layer_tcp net_tcp = {0};

    select_init();
    tcp_init(&net_tcp);
    tls_init(&net_tls);

    net_tcp.base.upper = &net_tls;

    net_tls.base.lower = (struct net_layer *)&net_tcp;
    net_tls.base.upper = &test_http;

    test_http.lower = (struct net_layer *)&net_tls;

    test_http.nconnect_cb = tls_layer_conn_cb;
    test_http.nrcv_cb    = http_recv_cb;
    test_http.ndisconnect_cb = http_handle_disconnect;

    struct tls_conn_param par = {0};
    par.host = "www.baidu.com";
    par.port = 443;
    par.ca_cert = ca_pem;
    par.ca_cert_len = strlen(ca_pem) + 1;
    par.our_cert = NULL;
    par.our_cert_len = 0;
    par.base.non_block = NON_BLOCKING_MODE;
    net_tls.base.nconfig(&net_tls, &par);

    struct tcp_conn_param par2 = {0};
    par2.host = par.host;
    par2.port = par.port;
    par2.base.non_block = par.base.non_block;
    net_tcp.base.nconfig( &net_tcp, (struct net_conn_param *)&par2);

    create_demo_timer();

    ret = net_tls.base.nconnect( &net_tls );
    if( ret < 0 ) {
        printf("[test] tls connect ret=%d", ret);
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

    net_tls.base.nsend(&net_tls, HTTP_GET, strlen(HTTP_GET), 0);

    uint8_t buf[1024 + 1];
    int len = 1024;

    while(1) {
        ret = net_tls.base.nread(&net_tls, buf, len, 0);
        if( ret > 0 ) {
            buf[ret] = '\0';
            printf("[test] ret=%d %s", ret, buf);
        } else {
            printf("[tls] read return %d\n", ret);
            net_tls.base.nclose(&net_tls);
            break;
        }
    }
#endif

    printf("\n[] exit test\n");

    return 0;
}