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
#define create_ums_complist(fd, id) ioctl(fd, UMS_REQUEST_NEW_COMPLETION_LIST, id)
#define create_ums_compelem(fd, id) ioctl(fd, UMS_REQUEST_REGISTER_COMPLETION_ELEM, id)

#define create_thread(function, stack, args)				\
	clone(&function, stack + TASK_STACK_SIZE,			\
	      CLONE_VM | CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |	\
	      CLONE_FILES | SIGCHLD, args);

static LIST_HEAD(thread_id_list);

static void* do_gen_ums_sched(void *args);
/* TODO: Move to header */

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


struct id_elem {
	int thread_id;
	struct list_head list;
};

static void register_entry_point(struct sched_entry_point sched_ep);

static void register_threads(int fd,
			     ums_sched_id sched_id);

static int __entry_point(void *sched_ep);

static int __reg_thread(void *sched_thread);

static int __reg_compelem(void *idxs);

static void new_id_elem(int thread_id);

int EnterUmsSchedulingMode(ums_function entry_point,
                           ums_complist_id complist_id,
			   ums_sched_id *result)
{
	int buff;
	int err, fd;
	struct sched_entry_point sched_ep;

	/* TODO: check this 0 */
	fd = open("/dev/usermodscheddev", 0);

	buff = complist_id;

	fprintf(stderr, "buff = %d\n", buff);

	err = ioctl(fd, UMS_REQUEST_ENTER_UMS_SCHEDULING, &buff);

	if (err) {
		fprintf(stderr, "Error: cannot create User Mode Scheduler thread!\n");
		return err;
	}

	*result = buff;

	sched_ep.fd = fd;
	sched_ep.id = *result;
	sched_ep.entry_point = entry_point;

	/* TODO: add a way to get the result */
	register_entry_point(sched_ep);

	register_threads(fd, *result);

	close(fd);

	return err;
}

int WaitUmsScheduler(ums_sched_id sched_id)
{
	int err, fd;

	fd = open("/dev/usermodscheddev", 0);

	err = ioctl(fd, UMS_REQUEST_WAIT_UMS_SCHEDULER, &sched_id);

	return err;
}

int WaitUmsChildren(void)
{
	/* TODO: qui */
	struct list_head *iter, *tmp_iter;

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
	int fd, err;

	fd = open("/dev/usermodscheddev", 0);

	create_ums_complist(fd, id);
	err = create_ums_complist(fd, id);

	close(fd);

	return err;
}

int CreateUmsCompletionList(ums_complist_id *id,
			    ums_function *list,
			    int list_count)
{
	int fd, i;
	int data[2];


	fd = open("/dev/usermodscheddev", 0);
	create_ums_complist(fd, id);

	data[0] = fd;
	data[1] = *id;

	for (i = 0; i < list_count; i++) {
		int thread_id = 0;
		void *stack = malloc(TASK_STACK_SIZE);
		
		thread_id = create_thread(__reg_compelem, stack, data);
		      
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
	int fd;
	int data[2];
	void *stack;
	int thread_id;

	fd = open("/dev/usermodscheddev", 0);

	data[0] = fd;
	data[1] = id;

	stack = malloc(TASK_STACK_SIZE);

	thread_id = create_thread(__reg_compelem, stack, data);

	if (thread_id < 0)
		return -1;

	new_id_elem(thread_id);

	return 0;
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
	int thread_id;
	void *stack;

	stack = malloc(TASK_STACK_SIZE);

	thread_id = create_thread(__entry_point, stack, &sched_ep);

	if (thread_id < 0) {
		fprintf(stderr, "Fail creating thread in %s: internal error\n", __func__);
	}

	new_id_elem(thread_id);
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
		int thread_id;
		void *stack = malloc(TASK_STACK_SIZE);

		thread_info.cpu = i;
		thread_id = create_thread(__reg_thread, stack, &thread_info);	

		if (thread_id < 0) {
			fprintf(stderr, "Thread registration for CPU %d failed!", i);
			continue;
		}

		new_id_elem(thread_id);
	}
}

static int __entry_point(void *sched_ep)
{
	int res;

	struct sched_entry_point *_sched_ep = (struct sched_entry_point*)sched_ep;
	
	fprintf(stderr, "Register entry point!\nfd=%d, id=%d", _sched_ep->fd, _sched_ep->id);

	res = ioctl(_sched_ep->fd, UMS_REQUEST_REGISTER_ENTRY_POINT, &_sched_ep->id);

	if (res) {
		fprintf(stderr, "Error in register_entry_point: %d\n", res);
		return res;
	}

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
		    &thread_info->id);

	if (res)
		return res;
	/* not reached */
	return 0;
}

static int __reg_compelem(void *idxs)
{
	int res, fd, id;
	int *data;

	data = (int*)idxs;

	fd = data[0];
	id = data[1];

	fprintf(stderr, "Registering new compelem to complist %d\n", id);

	res = create_ums_compelem(fd, &id);

	if (res) {
		fprintf(stderr, "Error creating new compelem!\n");
	}

	id = *data;

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
