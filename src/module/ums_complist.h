#ifndef __UMS_COMPLIST_H__
#define __UMS_COMPLIST_H__

typedef int ums_complist_id;
typedef int ums_compelem_id;
#include "ums_scheduler.h"
#include "ums_proc.h"
#include <linux/list.h>

extern struct proc_dir_entry *ums_proc_dir;
#define UMS_COMPLIST_HASH_BITS 8
#define UMS_COMPELEM_HASH_BITS 8
int ums_complist_add(ums_complist_id *result);

int ums_complist_remove(ums_complist_id id);

int ums_complist_exists(ums_complist_id comp_id);

int ums_complist_reserve(ums_complist_id comp_id,
			 int to_reserve,
			 ums_compelem_id *ret_array,
			 int *size);

int ums_compelem_add(ums_compelem_id* result,
		     ums_complist_id list_id);

int ums_complist_add_scheduler(ums_complist_id id, 
			       struct list_head *sched_list);

int ums_compelem_remove(ums_compelem_id id);

int ums_compelem_store_reg(ums_compelem_id compelem_id);

int ums_compelem_exec(ums_compelem_id compelem_id);

int ums_complist_init(void);

int ums_complist_proc_init(struct proc_dir_entry *ums_dir);

void ums_complist_deinit(void);

void ums_complist_proc_deinit(void);
#endif /* __UMS_COMPLIST_H__ */
