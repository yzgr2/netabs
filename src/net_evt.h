#ifndef EVT_H
#define EVT_H

#include <stdint.h>

#define EVT_READ    1
#define EVT_WRITE   (1<<1)
//TODO:
#define EVT_TIMER   (1<<2)

/**
 * @brief : @event_type is EVT_READ or EVT_WRITE
 */
struct event_common;
typedef void (*evt_fn)(int event_type, struct event_common *evt);

struct event_common {
    struct event_common *prev, *next;

    int     event_type;
    void    *args;
    evt_fn  evt_handle;

    int fd;
};

struct event_common *evt_allocate(int event_type, evt_fn func, int fd, void *arg);

void evt_free(struct event_common *evt);

//
int select_init();

int select_add_event(int event_type, struct event_common *evt);

int select_process_events(uint64_t timeout_ms);

int select_handle_fired_events();

int select_del_event(struct event_common *evt);

int select_evt_inlist(struct event_common *evt);


#endif
