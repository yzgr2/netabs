#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_common.h"
#include "net_evt.h"

struct event_common *evt_allocate(int event_type, evt_fn func, int fd, void *arg)
{
    struct event_common *evt;
    evt = (struct event_common *)malloc(sizeof(struct event_common));
    if( evt == NULL ) {
        NET_LOG_ERR("OOM, evt_allocate size=%lu", sizeof(struct event_common));
        return NULL;
    }

    memset(evt, 0, sizeof(struct event_common));

    evt->event_type = event_type;
    evt->args = arg;
    evt->evt_handle = func;
    evt->fd = fd;

    return evt;
}

void evt_free(struct event_common *evt)
{
    if( evt ) {
        free(evt);
    }
}
