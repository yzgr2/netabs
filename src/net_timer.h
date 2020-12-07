#ifndef NET_TIMER_H
#define NET_TIMER_H

#include "net_common.h"

/**
 * @brief : timer callback function
 */
typedef int (*gw_timer_cb_t)(void *arg);

typedef struct gw_timer_s
{
    struct gw_timer_s *prev, *next;

    clock_time_t  ticks;    //inner data, execute at this tick.
    void          *arg;
    gw_timer_cb_t  cb;

    clock_time_t   timeout_ms;
    int            periodic;  //repeat timer.
}gw_timer_t;


clock_time_t  gw_process_timers();

gw_timer_t* gw_create_timer(char *name, void *arg, gw_timer_cb_t callback);

void gw_start_timer(gw_timer_t *ptimer, int periodic, uint64_t timeout_ms);

void gw_del_timer(gw_timer_t * ptimer);


clock_time_t  gw_process_timers();



#endif
