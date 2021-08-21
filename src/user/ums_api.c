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
#include <sys/wait.h>
#include <fcntl.h>
#include "ll/list.h"

#define TASK_STACK_SIZE 65536
#define create_ums_complist(id)  ioctl(global_fd, UMS_REQUEST_NEW_COMPLETION_LIST, id)
#define create_ums_compelem(id)  ioctl(global_fd, UMS_REQUEST_REGISTER_COMPLETION_ELEM, id)
#define enter_ums_sched(id)      ioctl(global_fd, UMS_REQUEST_ENTER_UMS_SCHEDULING, id)
#define wait_ums_sched(id)       ioctl(global_fd, UMS_REQUEST_WAIT_UMS_SCHEDULER, id)
#define thread_yield(id)         ioctl(global_fd, UMS_REQUEST_YIELD, id)
#define exec_thread(id)          ioctl(global_fd, UMS_REQUEST_EXEC, id)
#define do_reg_entry_point(id)   ioctl(global_fd, UMS_REQUEST_REGISTER_ENTRY_POINT, id)
#define do_reg_thread(id)        ioctl(global_fd, UMS_REQUEST_REGISTER_SCHEDULER_THREAD, id)

#define create_thread(function, stack, args)				\
	clone(&function, stack + TASK_STACK_SIZE,			\
	      CLONE_VM | CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |	\
	      CLONE_FILES | SIGCHLD, args);

#define OPEN_GLOBAL_FD() \
	((void)(global_fd = global_fd ? global_fd : open("/dev/usermodscheddev", 0)))

static LIST_HEAD(thread_id_list);

static int global_fd = 0;

static void* do_gen_ums_sched(void *args);
/* TODO: Move to header */

struct sched_entry_point {
	ums_sched_id id;
	ums_function entry_point;
};

struct sched_thread {
	ums_sched_id id;
	int	     cpu;
};


struct id_elem {
	int thread_id;
	struct list_head list;
};

static void register_entry_point(ums_sched_id id,
				 ums_function entry_func);

static void register_threads(ums_sched_id sched_id);

static int __entry_point(void *sched_ep);

static int __reg_thread(void *sched_thread);

static int __reg_compelem(void *idxs);

static void new_id_elem(int thread_id);

int EnterUmsSchedulingMode(ums_function entry_point,
                           ums_complist_id complist_id,
			   ums_sched_id *result)
{
	int buff;
	int err;

	OPEN_GLOBAL_FD();

	buff = complist_id;

	err = enter_ums_sched(&buff);

	if (err) {
		fprintf(stderr, "Error: cannot create User Mode Scheduler thread!\n");
		return err;
	}

	*result = buff;

	/* TODO: add a way to get the result */
	register_entry_point(*result, entry_point);

	register_threads(*result);

	return err;
}

int WaitUmsScheduler(ums_sched_id sched_id)
{
	int err;

	OPEN_GLOBAL_FD();

	err = wait_ums_sched(&sched_id);

	return err;
}

int WaitUmsChildren(void)
{
	/* TODO: qui */
	struct list_head *iter, *tmp_iter;

	OPEN_GLOBAL_FD();

	list_for_each_safe(iter, tmp_iter, &thread_id_list) {
		struct id_elem *saved_id;
		int status;

		saved_id = list_entry(iter, struct id_elem, list);

		fprintf(stderr, "Waiting for %d\n", saved_id->thread_id);

		if (0 > waitpid(saved_id->thread_id, &status, __WCLONE))
			fprintf(stderr, "Error: during wait!\n");

		list_del(&saved_id->list);
	}

	return 0;
}

int CreateEmptyUmsCompletionList(ums_complist_id *id)
{
	OPEN_GLOBAL_FD();
	return create_ums_complist(id);
}

int CreateUmsCompletionList(ums_complist_id *id,
			    ums_function *list,
			    int list_count)
{
	int err, i;

	OPEN_GLOBAL_FD();
	err = create_ums_complist(id);

	if (err)
		return err;

	for (i = 0; i < list_count; i++) {
		int thread_id = 0;
		void *stack = malloc(TASK_STACK_SIZE);
		int *buff = malloc(sizeof(int));
		
		*buff = *id;
		thread_id = create_thread(__reg_compelem, stack, buff);
		      
		if (thread_id < 0) {
			fprintf(stderr, "Error: clone failed!\n");
			return -1;
		}
		fprintf(stderr, "New thread %d created\n", thread_id);

		new_id_elem(thread_id);

	}

	return 0;
}

int CreateUmsCompletionElement(ums_complist_id id,
		               ums_function func)
{
	void *stack;
	int thread_id;
	int *arg = malloc(sizeof(int));

	OPEN_GLOBAL_FD();
	stack = malloc(TASK_STACK_SIZE);

	*arg = id;

	thread_id = create_thread(__reg_compelem, stack, arg);

	if (thread_id < 0)
		return -1;

	new_id_elem(thread_id);

	return 0;
}

int ExecuteUmsThread(ums_sched_id sched_id,
                      ums_compelem_id next)
{
	int buff[2];

	OPEN_GLOBAL_FD();

	buff[0] = sched_id;
	buff[1] = next;

	exec_thread(buff);

	/* We will eventually return! */
	return 0;
}

int UmsThreadYield(ums_sched_id sched_id)
{
	OPEN_GLOBAL_FD();
	thread_yield(sched_id);

	/* We will eventually return! */
	return 0;
}


#if 0
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

static void register_entry_point(ums_sched_id id,
				 ums_function entry_func)
{
	int thread_id;

	void *stack = malloc(TASK_STACK_SIZE);
	struct sched_entry_point *sched_ep = malloc(sizeof(struct sched_entry_point));

	sched_ep->id = id;
	sched_ep->entry_point = entry_func;

	thread_id = create_thread(__entry_point, stack, sched_ep);

	if (thread_id < 0) {
		fprintf(stderr, "Fail creating thread in %s: internal error\n", __func__);
	}

	new_id_elem(thread_id);
}

static void register_threads(ums_sched_id sched_id)
{
	int i;
	int n_cpu = get_nprocs();

	for (i = 0; i < n_cpu; i++) {
		int thread_id;
		void *stack = malloc(TASK_STACK_SIZE);
		struct sched_thread *infos = malloc(sizeof(struct sched_thread));

		infos->id = sched_id;
		infos->cpu = i;

		thread_id = create_thread(__reg_thread, stack, infos);

		if (thread_id < 0) {
			fprintf(stderr, "Thread registration for CPU %d failed!", i);
			continue;
		}

		new_id_elem(thread_id);
	}
}

static int __entry_point(void *idx)
{
	int res;
	int id;
	struct sched_entry_point *sched_ep;
	ums_function entry_func;

	sched_ep = (struct sched_entry_point*)idx;
	id = sched_ep->id;
	entry_func = sched_ep->entry_point;
	free(sched_ep);

	res = do_reg_entry_point(&id);

	if (res) {
		fprintf(stderr, "Error in register_entry_point: %d\n", res);
		return res;
	}

	/* ums_sched module should block the process here. */
	/* The internal function will take the identifier as a parameter */
	entry_func(id);

	/* not reached */
	return 0;
}

static int __reg_thread(void *sched_thread)
{
	int res;
	int cpu;
	ums_sched_id id;
	cpu_set_t set;
	struct sched_thread* thread_info;

	thread_info = (struct sched_thread*)sched_thread;
	cpu = thread_info->cpu;
	id = thread_info->id;

	free(thread_info);

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	sched_setaffinity(0, sizeof(set), &set);

	res = do_reg_thread(&id);

	if (res)
		return res;
	/* not reached */
	return 0;
}

static int __reg_compelem(void *idxs)
{
	int res, id, *buff;

	buff = idxs;
	id = *buff;
	free(buff);

	// fprintf(stderr, "Registering new compelem to complist %d\n", id);

	res = create_ums_compelem(&id);

	if (res) {
		fprintf(stderr, "Error creating new compelem!\n");
	}

	/* not reached */
	return res;
}

static void new_id_elem(int thread_id)
{
	struct id_elem* elem;

	elem = (struct id_elem*) malloc(sizeof(struct id_elem));

	elem->thread_id = thread_id;

	list_add(&elem->list, &thread_id_list);
}
