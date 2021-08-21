#ifndef __UMS_SCHEDULER_H__
#define __UMS_SCHEDULER_H__

/* TODO: At the moment I just consider the typedef, I will eventually include
 * also the complist */
#include "ums_complist.h"

/* 256 buckets should be more than enought */
#define UMS_SCHED_HASH_BITS 8
/* TODO: Choose the type considering the constraints given by atomic_t */
typedef int ums_sched_id;


int ums_sched_init(void);

int ums_sched_add(ums_complist_id comp_list_id, ums_sched_id* identifier);

int ums_sched_wait(ums_sched_id sched_id);

int ums_sched_remove(ums_sched_id identifier);

int ums_sched_yield(ums_sched_id id);

int ums_sched_exec(ums_sched_id id,
		   ums_compelem_id elem_id);

int ums_sched_register_sched_thread(ums_sched_id sched_id);

int ums_sched_register_entry_point(ums_sched_id sched_id);

void ums_sched_cleanup(void);

#endif /* __UMS_SCHEDULER_H__ */
