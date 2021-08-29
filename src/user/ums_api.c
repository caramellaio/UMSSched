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
#include <string.h>

#define TASK_STACK_SIZE 65536
#define create_ums_complist(id)  ioctl(global_fd, UMS_REQUEST_NEW_COMPLETION_LIST, id)
#define create_ums_compelem(id)  ioctl(global_fd, UMS_REQUEST_REGISTER_COMPLETION_ELEM, id)
#define enter_ums_sched(id)      ioctl(global_fd, UMS_REQUEST_ENTER_UMS_SCHEDULING, id)
#define wait_ums_sched(id)       ioctl(global_fd, UMS_REQUEST_WAIT_UMS_SCHEDULER, id)
#define thread_yield(id)         ioctl(global_fd, UMS_REQUEST_YIELD, id)
#define exec_thread(id)          ioctl(global_fd, UMS_REQUEST_EXEC, id)
#define do_reg_thread(id)        ioctl(global_fd, UMS_REQUEST_REGISTER_SCHEDULER_THREAD, id)
#define dequeue_complist(id)	 ioctl(global_fd, UMS_REQUEST_DEQUEUE_COMPLETION_LIST, id)

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

struct sched_thread_args {
	ums_sched_id id;
	int	     cpu;
	ums_function entry_point;
};


/* TODO: remove */
struct id_elem {
	int thread_id;
	struct list_head list;
};

static void register_threads(ums_sched_id sched_id,
			     ums_function entry_point);

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
	register_threads(*result, entry_point);

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
		/* Initialise the buffer for the sub-thread */
		void *buff = malloc(sizeof(int) + sizeof(ums_function));
		
		/* assign the complist id + function to execute */
		memcpy(buff, id, sizeof(*id));
		memcpy(buff+sizeof(int), &list[i], sizeof(list[i]));

		thread_id = create_thread(__reg_compelem, stack, buff);
		      
		if (thread_id < 0) {
			fprintf(stderr, "Error: clone failed!\n");
			return -1;
		}
		fprintf(stderr, "New thread %d created\n", thread_id);

		/* TODO: check if this part is really useful. Probably a kernel
		 * side solution is more principled */
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
	int err;
	int buff[2];

	OPEN_GLOBAL_FD();

	buff[0] = sched_id;
	buff[1] = next;

	err = exec_thread(buff);

	/* We will eventually return! */
	return err;
}

int UmsThreadYield(void)
{
	int err;

	OPEN_GLOBAL_FD();
	err = thread_yield(NULL);

	/* We will eventually return! */
	return 0;
}


int DequeueUmsCompletionListItems(int max_elements,
				  ums_compelem_id *result_array,
				  int *result_length)
{
	int res;

	OPEN_GLOBAL_FD();

	/* the first expected element is the size */
	*result_array = max_elements;
	res = dequeue_complist(result_array);

	if (res)
		return res;

	*result_length = *result_array;

	*result_array = *(result_array + *result_length);

	/* zero terminate the list */
	/* 0 is ok because idx are always > 0 */
	*(result_array + *result_length) = 0;

	return 0;
}


static void register_threads(ums_sched_id sched_id,
			     ums_function entry_point)
{
	int i;
	int n_cpu = get_nprocs();

	fprintf(stderr, "n_cpus: %d\n", n_cpu);

	for (i = 0; i < n_cpu; i++) {
		int thread_id;
		void *stack = malloc(TASK_STACK_SIZE);
		struct sched_thread_args *info;
	       
		info = malloc(sizeof(struct sched_thread_args));

		info->id = sched_id;
		info->cpu = i;
		info->entry_point = entry_point;

		thread_id = create_thread(__reg_thread, stack, info);

		if (thread_id < 0) {
			fprintf(stderr, "Thread registration for CPU %d failed!", i);
			continue;
		}

		new_id_elem(thread_id);
	}
}

static int __reg_thread(void *sched_thread)
{
	int res;
	int cpu;
	ums_sched_id id;
	cpu_set_t set;
	struct sched_thread_args* thread_info;
	ums_function entry_point;

	thread_info = (struct sched_thread_args*)sched_thread;
	cpu = thread_info->cpu;
	id = thread_info->id;
	entry_point = thread_info->entry_point;

	free(thread_info);

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	sched_setaffinity(0, sizeof(set), &set);

	fprintf(stderr, "registering thread for cpu: %d\n", cpu);
	res = do_reg_thread(&id);

	if (res)
		return res;

	entry_point(id);

	fprintf(stderr, "%s: reached its end!\n", __func__);
	/* not reached */
	return 0;
}

static int __reg_compelem(void *idxs)
{
	int res, id, *buff;
	ums_function func;

	id = *((int*) idxs);
	func = *((ums_function*) (idxs + sizeof(int)));

	free(buff);

	// fprintf(stderr, "Registering new compelem to complist %d\n", id);

	res = create_ums_compelem(&id);

	func(id);
	if (res) {
		fprintf(stderr, "Error creating new compelem!\n");
	}

	fprintf(stderr, "%s: reached its end!\n", __func__);
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
