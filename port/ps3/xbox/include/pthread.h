/* pthread.h of the Xbox layer (newlib has none on the PS3): the few calls internet play makes (p2p.c), on
ps3_host.h's lv2 locks and threads (ps3_pthread.c) */

#ifndef __HALO_PS3_PTHREAD_H
#define __HALO_PS3_PTHREAD_H

#include <stdint.h>

struct ps3_host_lock;

/* (its lock made at the first lock) */
typedef struct
{
	struct ps3_host_lock *volatile lock;
} pthread_mutex_t;

typedef uint64_t pthread_t;
typedef int pthread_attr_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }

/* 0 not run, 1 running, 2 done (log_address.c) */
typedef struct
{
	volatile int state;
} pthread_once_t;

#define PTHREAD_ONCE_INIT { 0 }

int pthread_once(pthread_once_t *once, void (*routine)(void));

struct ps3_host_condition;
struct timespec;

/* (its condition made at the first wait, on the mutex's lock: thread_stop.c always holds the mutex) */
typedef struct
{
	struct ps3_host_condition *condition;
} pthread_cond_t;

#define PTHREAD_COND_INITIALIZER { 0 }

/* deadline: CLOCK_REALTIME */
int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex, const struct timespec *deadline);
int pthread_cond_broadcast(pthread_cond_t *condition);

int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
/* attributes must be NULL: a detached thread of PTHREAD_STACK_SIZE */
int pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*entry)(void *), void *argument);
int pthread_detach(pthread_t thread);

#endif
