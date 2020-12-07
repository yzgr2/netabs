#include <string.h>
#include <stdlib.h>
#include <sys/time.h> //gettimeofday

#include "utlist.h"
#include "net_timer.h"

/**
 * @brief : internal struct to handle all timers.
 */
struct timer_mgmt
{
    gw_timer_t   *timer_list;

    clock_time_t last_time;
};

static struct timer_mgmt g_timers ;

/**
 * @brief : init timer module
 */
void gw_timer_init()
{
    memset( &g_timers, 0, sizeof(struct timer_mgmt));
}

/**
 * @brief :  check if time @a later than @b
 * @param a
 * @param b
 * @return int
 */
static int time_cmp(gw_timer_t *a, gw_timer_t *b)
{
    if( a->ticks < b->ticks ) {
        return -1;
    } else if( a->ticks == b->ticks ) {
        return 0;
    } else {
        return 1;
    }
}

static void gw_timer_insert(gw_timer_t * t)
{
    if( g_timers.timer_list == NULL ) {
        DL_APPEND( g_timers.timer_list, t);
    } else {
        DL_INSERT_INORDER( g_timers.timer_list, t, time_cmp );  //link timer in sequence
    }
}

/**
 * @brief       : allocate a timer struct but not start it.
 * @param name  : name of this timer for trace.
 * @param arg   : argument for timer callback function
 * @param callback : function called when time is out.
 * @return gw_timer_t * : timer allocated, return NULL if fail.
 */
gw_timer_t* gw_create_timer(char *name, void *arg, gw_timer_cb_t callback)
{
    gw_timer_t * handler = malloc(sizeof(gw_timer_t));
    if( !handler ) {
        return NULL;
    }

    memset( handler, 0, sizeof(gw_timer_t) );

    handler->arg = arg;
    handler->cb = callback;

    return handler;
}

clock_time_t gw_current_time_ms()
{
    struct timeval te;

    gettimeofday(&te, NULL); // get current time

    clock_time_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000; // calculate milliseconds

    return milliseconds;
}

int gw_timer_in_list(gw_timer_t * ptimer)
{
    return (ptimer->prev != NULL) ? 1 : 0;
}

/**
 * @brief            : start the timer created by gw_create_timer()
 * @param ptimer     : timer handler.
 * @param timeout_ms : trigger the timer after timeout_ms.
 * @return void
 */
void gw_start_timer(gw_timer_t * ptimer, int periodic, uint64_t timeout_ms)
{
    //TODO: lock protect....

    if( ptimer->prev != NULL ) {
        //timer already in list. remove it.
        DL_DELETE( g_timers.timer_list, ptimer);
    }

    ptimer->timeout_ms = timeout_ms;
    ptimer->periodic   = periodic;
    ptimer->ticks = gw_current_time_ms() + timeout_ms;

    gw_timer_insert( ptimer );
}

/**
 * @brief        : stop the timer
 * @param ptimer : timer handler
 */
void gw_stop_timer(gw_timer_t * ptimer)
{
    //should check whether timer is in list. if yes, delete it.
    if( ptimer->prev ) {
        DL_DELETE(g_timers.timer_list, ptimer);
        ptimer->prev = NULL;
        ptimer->next = NULL;
    } else {
        //ptimer not in list, not remove it again.
    }
}

/**
 * @brief        : delete and free the timer.
 * @param ptimer : timer handler
 */
void gw_del_timer(gw_timer_t * ptimer)
{
    gw_stop_timer(ptimer);

    free(ptimer);
}

/**
 * @brief               : check and process the expired timer, only called by main poll function
 * @return clock_time_t : how many time left to the nearest timer.
 */
clock_time_t  gw_process_timers()
{
    clock_time_t now = gw_current_time_ms();
    gw_timer_t *cur, *n;
    gw_timer_t *process_list = NULL;

    DL_FOREACH_SAFE( g_timers.timer_list, cur, n) {
        if( now >= cur->ticks ) {
            DL_DELETE( g_timers.timer_list, cur );
            cur->prev = NULL; //init pointer again.
            cur->next = NULL;

            DL_APPEND(process_list, cur);
        } else {
            //since all timers are sorted, skip left timers.
            break;
        }
    }

    //process all the timer in process list.
    DL_FOREACH_SAFE( process_list, cur, n)
    {
        DL_DELETE( process_list, cur );
        cur->prev = NULL; //init pointer again.
        cur->next = NULL;

        // if( cur->periodic ) {
        //     gw_start_timer(cur, cur->periodic, cur->timeout_ms);
        // }

        if( cur->cb ) {
            int rc;
            rc = cur->cb( cur->arg );

            if( rc != 0 ) {  //usr can free time in the callback func. should return (value != 0)
                continue;
            }

            // if( cur->periodic ) {    //cur could be freed.
            //     gw_start_timer(cur, cur->periodic, cur->timeout_ms);
            // }
        }
    }

    if(  g_timers.timer_list == NULL ) {
        return -1;
    } else {
        cur  = g_timers.timer_list;
        return (cur->ticks - now <= 0) ? 1 : (cur->ticks - now) ;
    }
}
