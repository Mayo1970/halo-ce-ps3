/* include/pthread.h's calls on lv2 (ps3_host.h): internet play's mutexes and its detached threads, with the
Xbox layer's thread state (GetLastError, the heartbeat's waits) as the game's threads have it. */

#include <errno.h>
#include <pthread.h>
#include <time.h>

#define PTHREAD_STACK_SIZE 0x80000

struct pthread_start
{
	void *(*entry)(void *);
	void *argument;
};

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	struct ps3_host_lock *lock = mutex->lock;

	if (!lock)
	{
		struct ps3_host_lock *made = ps3_host_lock_create(0);

		if (!made)
			return ENOMEM;
		/* (another thread may have made one first) */
		if (!__sync_bool_compare_and_swap(&mutex->lock, (struct ps3_host_lock *)NULL, made))
			ps3_host_lock_destroy(made);
		lock = mutex->lock;
	}
	ps3_host_lock_enter(lock);
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	if (!mutex->lock)
		return EPERM;
	ps3_host_lock_leave(mutex->lock);
	return 0;
}

int pthread_once(pthread_once_t *once, void (*routine)(void))
{
	if (__sync_bool_compare_and_swap(&once->state, 0, 1))
	{
		routine();
		__sync_synchronize();
		once->state = 2;
		return 0;
	}
	while (once->state != 2)
		ps3_host_thread_yield();
	__sync_synchronize();
	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex, const struct timespec *deadline)
{
	struct timespec now;
	__int64 microseconds;

	if (!condition->condition)
	{
		condition->condition = ps3_host_condition_create(mutex->lock);
		if (!condition->condition)
			return ENOMEM;
	}
	clock_gettime(CLOCK_REALTIME, &now);
	microseconds = (__int64)(deadline->tv_sec - now.tv_sec) * 1000000 + (deadline->tv_nsec - now.tv_nsec) / 1000;
	if (microseconds <= 0)
		return ETIMEDOUT;
	return ps3_host_condition_wait(condition->condition, (uint64_t)microseconds) ? ETIMEDOUT : 0;
}

int pthread_cond_broadcast(pthread_cond_t *condition)
{
	/* (none made: nothing has waited yet) */
	if (condition->condition)
		ps3_host_condition_broadcast(condition->condition);
	return 0;
}

static void pthread_main(void *context)
{
	struct pthread_start start = *(struct pthread_start *)context;

	free(context);
	start.entry(start.argument);
	platform_thread_exited();
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*entry)(void *), void *argument)
{
	struct pthread_start *start = malloc(sizeof(*start));

	if (attributes || !start)
	{
		free(start);
		return EAGAIN;
	}
	start->entry = entry;
	start->argument = argument;
	if (ps3_host_thread_create(pthread_main, start, PTHREAD_STACK_SIZE, PS3_HOST_PRIORITY_NORMAL, "p2p") != 0)
	{
		free(start);
		return EAGAIN;
	}
	if (thread)
		*thread = 0;
	return 0;
}

/* (ps3_host.h's threads are detached) */
int pthread_detach(pthread_t thread)
{
	(void)thread;
	return 0;
}
