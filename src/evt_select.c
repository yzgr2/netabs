
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/select.h>

#include "utlist.h"
#include "net_evt.h"
#include "net_common.h"
#include "net_timer.h"

static fd_set read_fd_set;
static fd_set work_read_fd_set;

static fd_set write_fd_set;
static fd_set work_write_fd_set;

static volatile int max_fd;

struct event_common *evt_list = NULL;

struct event_common *evt_rd_ready = NULL;
struct event_common *evt_wr_ready = NULL;

int select_init()
{
    max_fd = -1;
    FD_ZERO(&read_fd_set);
    FD_ZERO(&write_fd_set);

    return 0;
}

int select_add_event(int event_type, struct event_common *evt)
{
    if( event_type & EVT_READ) {
        FD_SET( evt->fd, &read_fd_set);
    }

    if( event_type & EVT_WRITE) {
        FD_SET(evt->fd, &write_fd_set);
    }

    NET_LOG_DBG("[EVT] add evt, event_type=%d event_type=%d evt->fd=%d", event_type, evt->event_type, evt->fd);

    if ( max_fd == -1) {
        max_fd = evt->fd;
    } else if ( max_fd < evt->fd ) {
        max_fd = evt->fd;
    }

    //if evt already in evt_list, do nothing.
    if( !select_evt_inlist(evt)) {
        DL_APPEND(evt_list, evt);
    }

    return 0;
}


int select_evt_inlist(struct event_common *evt)
{
    if( !evt ) {
        return 0;
    }

    if( evt->prev ) {
        return 1;
    } else {
        return 0;
    }
}

int select_del_event(struct event_common *evt)
{
    if( evt->event_type & EVT_READ ) {
        FD_CLR(evt->fd, &read_fd_set);
    }

    if( evt->event_type & EVT_WRITE) {
        FD_CLR(evt->fd, &write_fd_set);
    }

    if( max_fd == evt->fd ) {
        max_fd = -1;
    }

    NET_LOG_DBG("[EVT]: del evt, max_fd=%d evt->fd=%d evt->prev:%p \n", max_fd, evt->fd, evt->prev);

    if( select_evt_inlist(evt) ) {
        DL_DELETE(evt_list, evt);
    }

    return 0;
}


int select_process_events(uint64_t timeout_ms)
{
    struct event_common *evt, *tmp;
    int cnt = 0;

    NET_LOG_DBG("select_process_events max_fd=%d Before CHECK.\n", max_fd);

    if( max_fd == -1) {
        DL_FOREACH(evt_list, evt) {
            if( max_fd < evt->fd ) {
                max_fd = evt->fd;
            }
            cnt++;
        }
    }

    NET_LOG_DBG("select_process_events max_fd=%d, evt in list count = %d\n", max_fd, cnt);

    struct timeval tv, *tp;

    uint64_t nearest_timer = gw_process_timers();

    if( timeout_ms > nearest_timer ) {
        timeout_ms = nearest_timer;
    }

    if( timeout_ms == -1 ) {
        tp = NULL;
    } else {
        tv.tv_sec =  timeout_ms/1000;
        tv.tv_usec = (timeout_ms % 1000 ) * 1000;
        tp = &tv;
    }

    work_read_fd_set    = read_fd_set;
    work_write_fd_set = write_fd_set;

    int rc = select( max_fd + 1, &work_read_fd_set,
                                 &work_write_fd_set,
                                 NULL, tp);
    if( rc == -1 ) {
        //error occurs.
        return NET_ERROR;
    } else if( rc == 0 ) {
        //timeout
        return 0;
    }

    // NET_LOG_DBG("\nevt occurs.\n");

    int count = 0;

    DL_FOREACH_SAFE(evt_list, evt, tmp) {
        if( FD_ISSET(evt->fd, &work_write_fd_set) ) {
            select_del_event(evt);   //remove event from list.
            DL_APPEND(evt_wr_ready, evt);
            ++count;

            NET_LOG_DBG("WRITE evt triggered.");
        } else if( FD_ISSET(evt->fd, &work_read_fd_set)) {
            select_del_event(evt);
            DL_APPEND(evt_rd_ready, evt);
            ++count;
            NET_LOG_DBG("READ evt triggered.");
        }
    }

    return count;
}

int select_handle_fired_events()
{
    struct event_common *evt, *tmp;

    DL_FOREACH_SAFE(evt_rd_ready, evt, tmp) {
        DL_DELETE(evt_rd_ready, evt);

        //TODO: evt-> callback
        evt->evt_handle(EVT_READ, evt);
    }

    DL_FOREACH_SAFE(evt_wr_ready, evt, tmp) {
        DL_DELETE(evt_wr_ready, evt);

        //TODO: evt-> callback
        evt->evt_handle(EVT_WRITE, evt);
    }

    return 0;
}