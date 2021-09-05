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

struct ums_complist {
	ums_complist_id id;
	struct hlist_node list;
	struct kfifo ready_queue;
	spinlock_t ready_lock;

	struct list_head compelems;
	spinlock_t compelems_lock;
	struct list_head schedulers;
	spinlock_t schedulers_lock;

	struct semaphore elem_sem;

	/* procfs directory */
	struct proc_dir_entry *proc_dir;
};

struct ums_compelem {
	ums_compelem_id id;
	/* The scheduler that is currently hosting the execution of compelem */
	ums_sched_id host_id;

	struct ums_complist *complist;

	struct hlist_node list;
	
	struct list_head complist_head;

	/* pointer to the header of the reservation list */
	struct list_head *reserve_head;

	/* element of the list */
	struct list_head reserve_list;

	struct task_struct* elem_task;

	struct ums_context entry_ctx;

	u64 switch_time;
	u64 total_time;
	unsigned int n_switch;

	/* procfs file */
	struct proc_dir_entry *proc_file;
};

void __get_from_compelem_id(ums_compelem_id id,
	       		    struct ums_compelem** compelem);

#endif /* __UMS_COMPLIST_INTERNAL_H__ */
