/**
 * @author Alberto Bombardelli
 *
 * @file ums_api.c
 *
 * @brief User space ums scheduling API implementation
 *
 * This file contains the definition of all the user space functions used for
 * ums scheduling.
*/

#define _GNU_SOURCE
#include "ums_api.h"
#include "../module/ums_device.h"
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

/**
 * @brief Size of the stack of the threads created by clone syscalls
*/
#define TASK_STACK_SIZE 65536

/**
 * @brief Complist creation ioctl call
 *
 * @sa ums_device.h
 * @sa ums_complist_add
*/
#define create_ums_complist(id)  ioctl(global_fd, UMS_REQUEST_NEW_COMPLETION_LIST, id)

/**
 * @brief Compelem creation ioctl call
 *
 * @sa ums_device.h
 * @sa ums_compelem_add
*/
#define create_ums_compelem(id)  ioctl(global_fd, UMS_REQUEST_REGISTER_COMPLETION_ELEM, id)

/**
 * @brief UMS scheduler creation ioctl call
 *
 * @sa ums_device.h
 * @sa ums_sched_add
*/
#define enter_ums_sched(id)      ioctl(global_fd, UMS_REQUEST_ENTER_UMS_SCHEDULING, id)

#define wait_ums_sched(id)       ioctl(global_fd, UMS_REQUEST_WAIT_UMS_SCHEDULER, id)

/**
 * @brief yield ioctl call
 *
 * @sa ums_device.h
 * @sa ums_sched_yield
*/
#define thread_yield(id)         ioctl(global_fd, UMS_REQUEST_YIELD, id)

/**
 * @brief complist execution ioctl call
 *
 * @sa ums_device.h
 * @sa ums_sched_exec
 * @sa ums_compelem_exec
*/
#define exec_thread(id)          ioctl(global_fd, UMS_REQUEST_EXEC, id)

/**
 * @brief UMS scheduler thread creation ioctl call
 *
 * @sa ums_device.h
 * @sa ums_sched_register_sched_thread
*/
#define do_reg_thread(id)        ioctl(global_fd, UMS_REQUEST_REGISTER_SCHEDULER_THREAD, id)

/**
 * @brief dequeue ioctl call
 *
 * @sa ums_device.h
 * @sa ums_complist_reserve
*/
#define dequeue_complist(id)	 ioctl(global_fd, UMS_REQUEST_DEQUEUE_COMPLETION_LIST, id)

/**
 * @brief Delete completion element ioctl call
 *
 * @sa ums_device.h
 * @sa ums_compelem_remove
*/
#define delete_compelem(id)	 ioctl(global_fd, UMS_REQUEST_REMOVE_COMPLETION_ELEM, id)

/**
 * @brief Macro to create a new thread using clone
 *
 * The thread keeps the same memory map and tgid, but it does not share signals
 * with his parent. To correctly use wait on these threads use __WCLONE flag
*/
#define create_thread(function, stack, args)				\
	clone(&function, stack + TASK_STACK_SIZE,			\
	      CLONE_VM | CLONE_FS |	\
	      CLONE_FILES , args);

/**
 * @brief Macro to open the global ums sched device file descriptor
*/
#define OPEN_GLOBAL_FD() \
	((void)(global_fd = global_fd ? global_fd : open("/dev/usermodscheddev", 0)))

static LIST_HEAD(thread_id_list);

/**
 * @brief Declaration of global file descriptor
*/
static int global_fd = 0;

static void* do_gen_ums_sched(void *args);

/**
 * @struct sched_thread_args
 *
 * @brief Struct to pass sched_thread info through clone
*/
struct sched_thread_args {
	ums_sched_id id;
	int	     cpu;
	ums_function entry_point;
};

/**
 * @struct id_elem
 *
 * @brief List entry of user mode linked list for threads
 *
 * @todo: include their stacks!
*/
struct id_elem {
	int thread_id;
	void *stack;
	struct list_head list;
};

static void register_threads(ums_sched_id sched_id,
			     ums_function entry_point);

static int __entry_point(void *sched_ep);

static int __reg_thread(void *sched_thread);

static int __reg_compelem(void *idxs);

static void new_id_elem(int thread_id,
			void *stack);

/**
 * @brief Function to register an new scheduler with his threads
 *
 * @param[in] entry_point: Entry point function executed by the scheduler threads
 * @param[in] complist_id: completion list to be linked with the scheduler
 * @param[out] result: resulting scheduler identifier
 *
 * The function register the scheduler, creates new threads (one for each CPU)
 * that will be execute the complist jobs.
 * The scheduler will be automatically removed when all the job of the completion
 * element will be completed.
 *
 * @return 0 if no error non-zero otherwise
*/
int EnterUmsSchedulingMode(ums_function entry_point,
                           ums_complist_id complist_id,
			   ums_sched_id *result)
{
	int buff = 0;
	int err = 1;

	OPEN_GLOBAL_FD();

	buff = complist_id;

	err = enter_ums_sched(&buff);

	if (err) {
		fprintf(stderr, "Error: cannot create User Mode Scheduler thread!\n");
		return err;
	}

	*result = buff;

	register_threads(*result, entry_point);

	return err;
}

/**
 * @brief Block this thread until the ums_scheduler get destroyed
 *
 * @return 0 if the wait was successful, non-zero otherwise
*/
int WaitUmsScheduler(ums_sched_id sched_id)
{
	int err;

	OPEN_GLOBAL_FD();

	err = wait_ums_sched(&sched_id);

	return err;
}

/**
 * @brief Wait all the thread created by UMS user mode module
 *
 * This function must be call to avoid leaving open file descriptors and
 * memory leaks (stack of the created threads)
*/
int WaitUmsChildren(void)
{
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
		free(saved_id->stack);
	}

	/* close global /dev file */
	close(global_fd);

	return 0;
}

/**
 * @brief Create an empty completion list
 *
 * @param[out] id: resulting id
 *
 * @return 0 if no error occured, nonzero otherwise
 *
 * @sa CreateUmsCompletionList
 * @sa CreateUmsCompletionElement
*/
int CreateEmptyUmsCompletionList(ums_complist_id *id)
{
	OPEN_GLOBAL_FD();
	return create_ums_complist(id);
}

/**
 * @brief Create a completion list with functions as elements
 *
 * @param[out] id: resulting id
 * @param[in] list: functions to be turned into compelems
 * @param[in] list_count: size of the list
 *
 * @return 0 if no error occured, nonzero otherwise
 *
 * @sa CreateEmptyUmsCompletionList
 * @sa CreateUmsCompletionElement
*/
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

		new_id_elem(thread_id, stack);

	}

	return 0;
}

/**
 * @brief Create a completion element for a complist
 *
 * @param[in] id: completion list id
 * @param[in] func: function to be executed
 *
 * @return 0 if no error occured, nonzero otherwise
 * @note Process remains blocked until compelem ends
 *
 * @sa CreateUmsCompletionList
*/
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

	new_id_elem(thread_id, stack);

	return 0;
}

/**
 * @brief Execute a compelem thread
 *
 * @param[in] next: next compelem to be executed
 *
 * @note the thread must have been reserved with DequeueUmsCompletionListItems
 *
 * @return 0 if no error occured, nonzero otherwise
 *
 * @sa DequeueUmsCompletionListItems
*/
int ExecuteUmsThread(ums_compelem_id next)
{
	int err;

	OPEN_GLOBAL_FD();

	err = exec_thread(next);

	/* We will eventually return! */
	return err;
}

/**
 * @brief Yield from a worker to the scheduler thread execution
 *
 * Resume the entry point function by stopping the worker (compelem) execution.
 *
 * @return 0 if no error occured, nonzero otherwise
 *
 * @sa ExecuteUmsThread
*/
int UmsThreadYield(void)
{
	int err;

	OPEN_GLOBAL_FD();
	err = thread_yield(NULL);

	fprintf(stderr, "Returned from yield: %d\n", err);
	/* We will eventually return! */
	return err;
}


/**
 * @brief Reserve completion elements from a complist
 *
 * @param[in] max_elements: maximum elements gettable
 * @param[out] result_array: array with the reserved elements
 * @param[out] result_length: resulting length
 *
 * @return 0 if no error occured, nonzero otherwise
 *
 * @note Calling dequeue 2 times without any Execution in between might lead
 *	to deadlock
 *
 * @sa UmsThreadYield
 * @sa ExecuteUmsThread
*/
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

/**
 * @brief Internal function to register a thread using clone
 *
 * @param[in] sched_id: scheduler identifier
 * @param[in] entry_point: function that the scheduler thread will execute
 * Just uses clone and call __reg_thread.
 *
 * @sa __reg_thread
*/
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

		new_id_elem(thread_id, stack);
	}
}

/**
 * @brief Register current thread to cpu i and as scheduler thread
 *
 * Set thread cpu to the one passed by clone and call ioctl sched registration
 *
 * @return 0 if everything was OK (after scheduler thread completion) or non-zero
 *	if error
*/
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
		fprintf(stderr, "reg failed!!\n");

	res = -1;
	res = entry_point(id);

	fprintf(stderr, "%s: reached its end: %d!\n", __func__, res);
	/* not reached */
	return 0;
}

/**
 * @brief Register current thread as completion element then freeze process
 *
 * @return 0 if everything was OK (after compelem completion) or non-zero
 *	if error
*/
static int __reg_compelem(void *idxs)
{
	int res, id, *buff;
	ums_function func;

	id = *((int*) idxs);
	func = *((ums_function*) (idxs + sizeof(int)));

	free(idxs);

	// fprintf(stderr, "Registering new compelem to complist %d\n", id);

	res = create_ums_compelem(&id);

	fprintf(stderr, "I am going to execute func!\n");
	func(id);
	fprintf(stderr, "Calling delete_compelem: %d\n", id);
	delete_compelem(id);
	/* NOTE: after delete compelem 2 processes will try to get yield*/
	UmsThreadYield();

	if (res) {
		fprintf(stderr, "Error creating new compelem!\n");
	}

	fprintf(stderr, "%s: reached its end!\n", __func__);
	/* not reached */
	return res;
}

/**
 * @brief Add new thread to the one alloced
 *
 * @param[in] thread_id: thread identifier
 * @param[in] stack: stack allocated
 * @return void
*/
static void new_id_elem(int thread_id,
			void *stack)
{
	struct id_elem* elem;

	elem = (struct id_elem*) malloc(sizeof(struct id_elem));

	elem->thread_id = thread_id;
	elem->stack = stack;

	list_add(&elem->list, &thread_id_list);
}
