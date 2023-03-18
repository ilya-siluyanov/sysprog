#ifndef LIBCORO_INCLUDED
#define LIBCORO_INCLUDED

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <setjmp.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include "types.h"

typedef int (*coro_f)(void *);

struct coro_stats {
    ll switch_count;
    /** Amount of microseconds the coro spent CPU **/
    ld work_time;
    /** `busy_time` + amount of microseconds the coro was in `waiting` state **/
    ld total_time;

    ld _last_finished_at;
    ld _last_started_at;
};
/** Main coroutine structure, its context. */
struct coro {
    /** A value, returned by func. */
    int ret;
    /** Stack, used by the coroutine. */
    void *stack;
    /** An argument for the function func. */
    void *func_arg;
    /** A function to call as a coroutine. */
    coro_f func;
    /** Last remembered coroutine context. */
    sigjmp_buf ctx;
    /** True, if the coroutine has finished. */
    bool is_finished;

    struct coro_stats *stats;
    /** Links in the coroutine list, used by scheduler. */
    struct coro *next, *prev;
};


/** Make current context scheduler. */
void
coro_sched_init(void);

/**
 * Block until any coroutine has finished. It is returned. NULl,
 * if no coroutines.
 */
struct coro *
coro_sched_wait(void);

/** Currently working coroutine. */
struct coro *
coro_this(void);

/**
 * Create a new coroutine. It is not started, just added to the
 * scheduler.
 */
struct coro *
coro_new(coro_f func, void *func_arg);

/** Return result of the coroutine. */
void *
coro_status(const struct coro *c);

long long
coro_switch_count(const struct coro *c);

/** Check if the coroutine has finished. */
bool
coro_is_finished(const struct coro *c);

/** Free coroutine stack and it itself. */
void
coro_delete(struct coro *c);

/** Switch to another not finished coroutine. */
void
coro_yield(void);

#endif /* LIBCORO_INCLUDED */
