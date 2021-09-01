#ifndef __UMS_COMPLIST_INTERNAL_H__
#define __UMS_COMPLIST_INTERNAL_H__

#include <linux/kfifo.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include "ums_complist.h"
#include "ums_scheduler.h"
#include "ums_context_switch.h"

#define __complist_add_sched(complist, sched_list_head)			\
	(list_add(sched_list_head, &complist->schedulers))

struct ums_complist {
	ums_complist_id id;
	struct hlist_node list;
	struct kfifo ready_queue;

	struct list_head compelems;
	struct list_head schedulers;

	struct semaphore elem_sem;
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

	/* TODO: might remove the task */
	struct task_struct* elem_task;

	struct ums_context entry_ctx;
};

void __get_from_compelem_id(ums_compelem_id id,
	       		    struct ums_compelem** compelem);

void __get_from_complist_id(ums_complist_id id,
	      		    struct ums_complist** complist);
#endif /* __UMS_COMPLIST_INTERNAL_H__ */
