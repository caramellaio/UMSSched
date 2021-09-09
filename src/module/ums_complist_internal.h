/**
 * @file ums_complist_internal.h
 *
 * @brief Internal data structures of complist submodule definitions.
 *
 * This file contains info concerning the completion list and the completion
 * elements data structures and how they work. Details of their implementation
 * can be file in file ums_complist.c.
 *
 * @sa ums_complist.c
 * @sa ums_complist.h
*/
#ifndef __UMS_COMPLIST_INTERNAL_H__
#define __UMS_COMPLIST_INTERNAL_H__

#include <linux/kfifo.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>

#include "ums_complist.h"
#include "ums_scheduler.h"
#include "ums_context_switch.h"

/**
 * @struct ums_complist
 *
 * @brief Structure that defines the logical entity of completion list
 *
 * @var scheduler_lock: lock of the list of schedulers
 * @var elem_sem: semaphore used as async counter of ready_queue
 * @var proc_dir: completion list proc directory entry
 *
 * This structure is the logical list of completion elements. Its duties are
 * to give access to completion elements in a safe way (i.e. using locking 
 * mechanism to ensure that a completion element is used by one thread at
 * the time) and to trigger the ums_scheduler delete when all the elements
 * have finished.
*/
struct ums_complist {
	/** unique identifier, created with atomic_t var to ensure 
	 * that it has no duplicates */
	ums_complist_id id;

	/** list used for hash_add/del operations of the hashtable */
	struct hlist_node list;

	/** This queue is used to store the completion lists (ums_compelem)
	 *  that are neither in execution nor reserved */
	struct kfifo ready_queue;

	/** the lock for the ready queue. This lock is used during
	 * kfifo_in_spinlocked and kfifo_out_spinlocked */
	spinlock_t ready_lock;

	/**
	 * This field is the thread group id for the completion list. We keep
	 * track of it because schedulers and completion lists with different
	 * tgid cannot interract with each other.
	*/
	pid_t tgid;

	/** list of completion elements pointers (ums_compelem*) that are owned
	 * and managed by this completion list. */
	struct list_head compelems;

	/** The lock used to ensure that the list is accessed by one thread
	 * per time
	*/
	spinlock_t compelems_lock;

	/** list of schedulers that are registered to the completion list.
	 * When the completion list is empty, the remove function will trigger
	 * the scheduler delete. This list is indirect i.e. contains indices */
	struct list_head schedulers;

	/** Lock to access to the schedulers list in isolation */
	spinlock_t schedulers_lock;

	/** Semaphore used to reserve the kfifo elements and block callers if
	 * there are no available completion elements */
	struct semaphore elem_sem;

	/* procfs directory */
	struct proc_dir_entry *proc_dir;
};

/**
 * @struct ums_compelem
 *
 * @brief Logical structure representing the thread to be executed
 *
 * This structure represents the thread to be executed by the scheduler thread.
 * This structure is not thread safe, hence it is necessary to access it through
 * completion list (that guarantees isolate executions of completion elems).
 *
 * The function to execute of the completion element is implicit, it is the
 * thread itself.
*/
struct ums_compelem {
	/** unique identifier, created with atomic_t var to ensure 
	 * that it has no duplicates */
	ums_compelem_id id;

	/* The scheduler that is currently hosting the execution of compelem */
	ums_sched_id host_id;

	/** parent completion list that manage this completion element */
	struct ums_complist *complist;

	/** list entry for the compelem hash */
	struct hlist_node list;
	
	/** entry for the completion list, list of completion elements */
	struct list_head complist_head;

	/* pointer to the header of the reservation list */
	struct list_head *reserve_head;

	/** entry of the shared reservation list */
	struct list_head reserve_list;

	/**  task_struct that created this completion element. This task
	 * is used to generate the ums_context. During the creation the
	 * element is blocked and it get released only during delete. 
	*/
	struct task_struct* elem_task;

	/** The execution context of the completion element. This field is 
	 * essential for the execution of the original task (elem_task) through
	 * a scheduler thread. */
	struct ums_context entry_ctx;

	/** Time where the last context switch occured (we consider entering in
	 * worker thread not exit) */
	u64 switch_time;

	/** The total amount of time in which the completion element was active
	 * in millisecond. This value is reliable only when the completion 
	 * element is not in execution, otherwise __calc_time gives a precise
	 * value of it. */
	u64 total_time;

	/** Number of switch (we consider entering in the worker thread) have
	 * been executed by the completion element */
	unsigned int n_switch;

	/** procfs file that will contain the infos and stats of the compelem */
	struct proc_dir_entry *proc_file;
};

#endif /* __UMS_COMPLIST_INTERNAL_H__ */
