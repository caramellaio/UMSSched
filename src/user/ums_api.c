#define _GNU_SOURCE
#include "ums_api.h"
#include "../shared/ums_request.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sched.h>
#include <sys/sysinfo.h>
#include <fcntl.h>

#define TASK_STACK_SIZE 65536

static void* do_gen_ums_sched(void *args);
/* TODO: Move to header */
typedef int (*ums_function)(int);

struct sched_entry_point {
	ums_sched_id id;
	int	     fd;
	ums_function entry_point;
};

struct sched_thread {
	ums_sched_id id;
	int	     fd;
	int	     cpu;
};

static void register_entry_point(struct sched_entry_point sched_ep);

static void register_threads(int fd,
			     ums_sched_id sched_id);

static int __entry_point(void *sched_ep);

static int __reg_thread(void *sched_thread);


int EnterUmsSchedulingMode(ums_function entry_point,
                           ums_complist_id complist_id,
			   ums_sched_id *result)
{
	int err, buff, fd;
	struct sched_entry_point sched_ep;

	buff = complist_id;

	/* TODO: check this 0 */
	fd = open("/dev/usermodscheddev", 0);
	err = ioctl(fd, UMS_REQUEST_ENTER_UMS_SCHEDULING, buff);

	if (err) {
		fprintf(stderr, "Error: cannot create User Mode Scheduler thread!\n");
	}

	*result = buff;

	sched_ep.fd = fd;
	sched_ep.id = *result;
	sched_ep.entry_point = entry_point;

	register_entry_point(sched_ep);

	register_threads(fd, *result);

	close(fd);

	return err;
}

#if 0
void ExecuteUmsThread(struct ums_scheduler* scheduler,
                      struct ums_worker* new_worker)
{
  return;
}

void UmSThreadYield(struct ums_scheduler* scheduler,
                    struct ums_worker* act_worker)
{
  return;
}


struct ums_worker* DequeueUmsCompletionListItems(struct ums_scheduler* scheduler)
{
  return NULL;
}

static void* do_gen_ums_sched(void *args)
{
  printf("Executing function: %s\n", __func__);

  return 0;
}

#endif

static void register_entry_point(struct sched_entry_point sched_ep)
{
	int thread_pid;
	void *stack;

	stack = malloc(TASK_STACK_SIZE);

	thread_pid = clone(__entry_point, stack + TASK_STACK_SIZE, 
			   CLONE_VM | CLONE_THREAD, &sched_ep);
}

static void register_threads(int fd,
			     ums_sched_id sched_id)
{
	struct sched_thread thread_info;
	int i;
	int n_cpu = get_nprocs();


	thread_info.fd = fd;
	thread_info.id = sched_id;

	for (i = 0; i < n_cpu; i++) {
		void *stack = malloc(TASK_STACK_SIZE);

		thread_info.cpu = i;
		clone(__reg_thread, stack + TASK_STACK_SIZE,
		      CLONE_VM | CLONE_THREAD, &thread_info);	
	}
}

static int __entry_point(void *sched_ep)
{
	int res;

	struct sched_entry_point *_sched_ep = (struct sched_entry_point*)sched_ep;
	
	res = ioctl(_sched_ep->fd, UMS_REQUEST_REGISTER_ENTRY_POINT, _sched_ep->id);

	if (res)
		return res;

	/* ums_sched module should block the process here. */
	/* The internal function will take the identifier as a parameter */
	_sched_ep->entry_point(_sched_ep->id);

	/* not reached */
	return 0;
}

static int __reg_thread(void *sched_thread)
{
	int res;
	cpu_set_t set;
	struct sched_thread* thread_info;

	thread_info = (struct sched_thread*)sched_thread;

	CPU_ZERO(&set);
	CPU_SET(thread_info->cpu, &set);
	sched_setaffinity(0, sizeof(set), &set);

	res = ioctl(thread_info->fd, UMS_REQUEST_REGISTER_SCHEDULER_THREAD, 
		    thread_info->id);

	if (res)
		return res;
	/* not reached */
	return 0;
}
