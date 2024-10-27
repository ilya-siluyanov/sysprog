#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <printf.h>
#include <unistd.h>
#include <time.h>
#include <sys/errno.h>
#include <string.h>

enum {
    WAITING  = 0b001,
    TAKEN    = 0b010,
    FINISHED = 0b100,
};
struct thread_task {
    thread_task_f function;
    void *arg;

    pthread_mutex_t access_lock;
    pthread_cond_t join_cond;
    int state;
    void *result;
    struct thread_task *prev;
    struct thread_task *next;
};

struct task_pool {
    struct thread_task *tasks;
    int task_count;
};

struct thread_pool {
    pthread_t *threads;

    pthread_mutex_t access_lock;
    pthread_cond_t last_thread_left;
    int thread_count;
    int max_thread_count;
    bool deleted;

    struct task_pool *task_pool;
};

int task_pool_new(struct task_pool **pool) {
    struct task_pool *task_pool = calloc(1, sizeof(struct task_pool));
    if (task_pool == 0) {
        return -1;
    }
    *pool = task_pool;
    return 0;
}

int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
    /* IMPLEMENT THIS FUNCTION */
    if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    struct thread_pool *thread_pool = calloc(1, sizeof(struct thread_pool));
    if (thread_pool == 0) {
        return -1;
    }

    struct task_pool *task_pool;
    if (task_pool_new(&task_pool) != 0) {
        return -1;
    }
    thread_pool->task_pool = task_pool;

    pthread_mutex_init(&thread_pool->access_lock, NULL);
    pthread_cond_init(&thread_pool->last_thread_left, NULL);
    // FIXME: make it lazy
    thread_pool->threads = calloc(TPOOL_MAX_THREADS, sizeof(pthread_t));
    thread_pool->max_thread_count = max_thread_count;
    *pool = thread_pool;
    return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
    pthread_mutex_lock((pthread_mutex_t *)&pool->access_lock);
    int result = pool->thread_count;
    pthread_mutex_unlock((pthread_mutex_t *)&pool->access_lock);
    return result;
}

int
thread_pool_delete(struct thread_pool *pool)
{
    pthread_mutex_lock(&pool->access_lock);
    struct task_pool *task_pool = pool->task_pool;
    if (task_pool->task_count > 0) {
        pthread_mutex_unlock(&pool->access_lock);
        return TPOOL_ERR_HAS_TASKS;
    }

    pool->deleted = true;
    while (pool->thread_count > 0) {
        pthread_cond_wait(&pool->last_thread_left, &pool->access_lock);
    }
    free(pool->threads);
    free(pool->task_pool);
    free(pool);
    return 0;
}

int worker_iteration(struct thread_pool *pool) {
    // wait on a task to appear
    pthread_mutex_lock(&pool->access_lock);
    if (pool->deleted) {
        pool->thread_count--;
        if (pool->thread_count == 0) {
            pthread_cond_broadcast(&pool->last_thread_left);
        }
        pthread_mutex_unlock(&pool->access_lock);
        return -1;
    }
    struct task_pool *task_pool = pool->task_pool;
    // find the task
    struct thread_task *task_to_run = task_pool->tasks;
    while (task_to_run != NULL) {
        pthread_mutex_lock(&task_to_run->access_lock);
        if (task_to_run->state & WAITING) {
            task_to_run->state = TAKEN;
            pthread_mutex_unlock(&task_to_run->access_lock);
            break;
        }
        struct thread_task *next = task_to_run->next;
        pthread_mutex_unlock(&task_to_run->access_lock);
        task_to_run = next;
    }
    pthread_mutex_unlock(&pool->access_lock);

    if (task_to_run == NULL) {
        return 0;
    }

    void *result = task_to_run->function(task_to_run->arg);

    pthread_mutex_lock(&pool->access_lock);
    pthread_mutex_lock(&task_to_run->access_lock);

    task_to_run->result = result;
    task_to_run->state = FINISHED;

    if (task_to_run->prev != NULL)
        task_to_run->prev->next = task_to_run->next;
    else {
        task_pool->tasks = task_to_run->next;
    }
    if (task_to_run->next != NULL)
        task_to_run->next->prev = task_to_run->prev;

    pool->task_pool->task_count--;

    pthread_cond_broadcast(&task_to_run->join_cond);
    pthread_mutex_unlock(&task_to_run->access_lock);
    pthread_mutex_unlock(&pool->access_lock);
    return 0;
}

void *worker_routine(void *arg) {
    struct thread_pool *parent_pool = (struct thread_pool *) arg;
    while (true) {
        // printf("%ld starts iteration\n", tid);
        int result = worker_iteration(parent_pool);
        if (result != 0) {
            return NULL;
        }
    }
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
    pthread_mutex_lock(&pool->access_lock);
    pthread_mutex_lock(&task->access_lock);

    if (task->state & FINISHED) {
        pthread_mutex_unlock(&task->access_lock);
        pthread_mutex_unlock(&pool->access_lock);
        return 0;
    }

    struct task_pool *task_pool = pool->task_pool;
    bool allowed_to_push_task = task_pool->task_count < TPOOL_MAX_TASKS;
    if (!allowed_to_push_task) {
        pthread_mutex_unlock(&task->access_lock);
        pthread_mutex_unlock(&pool->access_lock);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }

    bool all_threads_busy = task_pool->task_count == pool->thread_count;
    bool allowed_to_spawn = pool->thread_count < pool->max_thread_count;

    if (all_threads_busy && allowed_to_spawn) {
        pthread_t tid;
        pthread_create(&tid, NULL, &worker_routine, pool);
        pool->threads[pool->thread_count++] = tid;
    }
    task->next = task_pool->tasks;
    if (task_pool->tasks != NULL) {
        task_pool->tasks->prev = task;
    }
    task_pool->tasks = task;
    task_pool->task_count++;
    task->state = WAITING;

    pthread_mutex_unlock(&task->access_lock);
    pthread_mutex_unlock(&pool->access_lock);
    return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
    /* IMPLEMENT THIS FUNCTION */

    struct thread_task *_task = calloc(1, sizeof(struct thread_task));
    pthread_mutex_init(&_task->access_lock, NULL);
    pthread_cond_init(&_task->join_cond, NULL);
    _task->function = function;
    _task->arg = arg;
    *task = _task;
    return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
    pthread_mutex_lock((pthread_mutex_t *)&task->access_lock);
    bool result = task->state & FINISHED;
    pthread_mutex_unlock((pthread_mutex_t *)&task->access_lock);

    return result;
}

bool
thread_task_is_running(const struct thread_task *task)
{
    /* IMPLEMENT THIS FUNCTION */
    pthread_mutex_lock((pthread_mutex_t *)&task->access_lock);
    bool result = task->state & TAKEN && !(task->state & FINISHED);
    pthread_mutex_unlock((pthread_mutex_t *)&task->access_lock);
    return result;
}

unsigned long long timespec_to_ns(struct timespec ts) {
    return (unsigned long long) ts.tv_sec * (long) 1e9 + ts.tv_nsec;
}

struct timespec ts_add(struct timespec ts, double v) {
    long sec_to_add = (long) v;
    long ns_to_add = (v - sec_to_add) * 1e9;

    unsigned long long total_ns = ts.tv_nsec + ns_to_add;
    sec_to_add += total_ns / (long) 1e9;
    total_ns %= (long) 1e9;

    struct timespec result = {
            .tv_sec = ts.tv_sec + sec_to_add,
            .tv_nsec = (long) total_ns
    };
    return result;
}

bool timer_expired(struct timespec expired_at, struct timespec now) {
    return timespec_to_ns(expired_at) < timespec_to_ns(now);
}

struct timespec ts_now() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts;
}
int
thread_task_join(struct thread_task *task, void **result)
{
    pthread_mutex_lock(&task->access_lock);
    if (!task->state) {
        pthread_mutex_unlock(&task->access_lock);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
    while (task->state != FINISHED) {
        pthread_cond_wait(&task->join_cond, &task->access_lock);
    }
    *result = task->result;
    pthread_mutex_unlock(&task->access_lock);
    return 0;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
    /* IMPLEMENT THIS FUNCTION */
    pthread_mutex_lock(&task->access_lock);
    if (!task->state) {
        pthread_mutex_unlock(&task->access_lock);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
    struct timespec start = ts_now();
    struct timespec expired_at = ts_add(start, timeout);
    while (task->state != FINISHED) {
        int rc = pthread_cond_timedwait(&task->join_cond, &task->access_lock, &expired_at);
        bool timedout = rc == ETIMEDOUT;
        bool expired = rc == EINVAL && timer_expired(expired_at, ts_now());
        if (timedout || expired) {
            pthread_mutex_unlock(&task->access_lock);
            return TPOOL_ERR_TIMEOUT;
        }
    }
    *result = task->result;
    pthread_mutex_unlock(&task->access_lock);
    return 0;
}

#endif

int
thread_task_delete(struct thread_task *task)
{
    pthread_mutex_lock(&task->access_lock);
    if (task->state & FINISHED || !task->state) {
        pthread_mutex_destroy(&task->access_lock);
        pthread_cond_destroy(&task->join_cond);
        free(task);
        return 0;
    }
    pthread_mutex_unlock(&task->access_lock);
    return TPOOL_ERR_TASK_IN_POOL;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif
