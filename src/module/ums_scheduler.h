#ifndef __UMS_SCHEDULER_H__
#define __UMS_SCHEDULER_H__

typedef int ums_sched_id;

#include "ums_complist.h"
#include <linux/proc_fs.h>

/* 256 buckets should be more than enought */
#define UMS_SCHED_HASH_BITS 8

int ums_sched_init(void);

void ums_sched_deinit(void);

int ums_sched_proc_init(struct proc_dir_entry *ums_dir);

void ums_sched_proc_deinit(void);

int ums_sched_add(ums_complist_id comp_list_id, ums_sched_id* identifier);

int ums_sched_wait(ums_sched_id sched_id);

int ums_sched_remove(ums_sched_id identifier);

int ums_sched_yield(void);

int ums_sched_exec(ums_compelem_id elem_id);

int ums_sched_register_sched_thread(ums_sched_id sched_id);

int ums_sched_complist_by_current(ums_complist_id *res_id);


#endif /* __UMS_SCHEDULER_H__ */
