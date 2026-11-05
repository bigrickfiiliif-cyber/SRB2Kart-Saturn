// SRB2Kart Saturn - Nintendo 3DS port
// i_threads.h implementation on top of libctru threads (via n3ds_hw).

#ifdef HAVE_THREADS

#include <stdlib.h>

#include "../i_threads.h"
#include "n3ds_hw.h"

typedef struct thread_s
{
	I_thread_fn      entry;
	void            *userdata;
	void            *handle;
	struct thread_s *next;
} thread_t;

static thread_t *thread_list = NULL;
static void     *thread_list_mutex = NULL;
static void     *anchor_mutex = NULL;
static volatile int threads_running = 1;

// Lazily create the object behind an anchor (double-checked).
static void *identity(void **anchor, void *(*create)(void))
{
	void *id = __atomic_load_n(anchor, __ATOMIC_ACQUIRE);
	if (!id)
	{
		N3DS_MutexLock(anchor_mutex);
		id = __atomic_load_n(anchor, __ATOMIC_ACQUIRE);
		if (!id)
		{
			id = create();
			if (!id)
				abort();
			__atomic_store_n(anchor, id, __ATOMIC_RELEASE);
		}
		N3DS_MutexUnlock(anchor_mutex);
	}
	return id;
}

static void worker(void *p)
{
	thread_t *th = (thread_t *)p;
	th->entry(th->userdata);
}

void I_spawn_thread(const char *name, I_thread_fn entry, void *userdata)
{
	thread_t *th = (thread_t *)malloc(sizeof *th);
	(void)name;

	if (!th)
		abort();

	th->entry = entry;
	th->userdata = userdata;

	N3DS_MutexLock(thread_list_mutex);
	if (threads_running)
	{
		// Network / HTTP helpers: run them on the system core when possible
		// so they don't steal time from the renderer.
		th->handle = N3DS_ThreadCreate(worker, th, 128 * 1024, 1, 1);
		if (!th->handle)
			abort();
		th->next = thread_list;
		thread_list = th;
	}
	else
		free(th);
	N3DS_MutexUnlock(thread_list_mutex);
}

int I_thread_is_stopped(void)
{
	return !threads_running;
}

void I_start_threads(void)
{
	thread_list_mutex = N3DS_MutexCreate();
	anchor_mutex = N3DS_MutexCreate();
	if (!thread_list_mutex || !anchor_mutex)
		abort();
}

void I_stop_threads(void)
{
	thread_t *th, *next;

	if (!threads_running)
		return;

	threads_running = 0;

	N3DS_MutexLock(thread_list_mutex);
	for (th = thread_list; th; th = next)
	{
		next = th->next;
		N3DS_ThreadJoin(th->handle);
		free(th);
	}
	thread_list = NULL;
	N3DS_MutexUnlock(thread_list_mutex);
}

void I_lock_mutex(I_mutex *anchor)
{
	N3DS_MutexLock(identity(anchor, N3DS_MutexCreate));
}

void I_unlock_mutex(I_mutex id)
{
	N3DS_MutexUnlock(id);
}

void I_hold_cond(I_cond *cond_anchor, I_mutex mutex_id)
{
	N3DS_CondWait(identity(cond_anchor, N3DS_CondCreate), mutex_id);
}

void I_wake_one_cond(I_cond *anchor)
{
	N3DS_CondSignal(identity(anchor, N3DS_CondCreate));
}

void I_wake_all_cond(I_cond *anchor)
{
	N3DS_CondBroadcast(identity(anchor, N3DS_CondCreate));
}

#endif // HAVE_THREADS
