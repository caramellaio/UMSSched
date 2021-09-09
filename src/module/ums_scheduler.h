/**
 * @author Alberto Bombardelli
 *
 * @file ums_scheduler.h
 *
 * @brief Public header for ums scheduler module
 *
 * This file contains the public accessible prototypes functions of the scheduler
 * sub-module
 *
 * To enable module:
 * @code
 * ums_sched_init();
 * ums_sched_proc_init(ums_dir);
 * @endcode
 *
 * To deinit module:
 * @code 
 * ums_sched_proc_deinit(ums_dir);
 * ums_sched_deinit();
 * @endcode
 *
 *
 * To create a new scheduler (without registered threads):
 * @code
 * ums_sched_add(complist_id, &id);
 * @endcode
 *
 * To create a new scheduler with threads:
 *
 * To do that it is necessary to have multiple threads created from user space:
 * note that every thread must share the same mm i.e. they must be thread of the
 * same process [this is an hard constraint]. Another strict constraint is that
 * the threads MUST always run on a single CPU (see sched_set_affinity).
 * 
 * To create a new scheduler worker:
 * @code
 * // USERMODE THREAD1
 * CPU_ZERO(&set);
 * CPU_SET(cpu, &set);
 * sched_setaffinity(0, sizeof(set), &set);
 * ioctl_register_cpu(sched);
 * entry_point_function();
 * return 0;
 *
 * // KERNELMODE THREAD1
 * ums_sched_register_sched_thread(sched_id);
 * @endcode
 *
 * To execute a completion element from a registered worker:
 * @code
 * ums_sched_exec(elem_id)
 * @endcode
 * NOTE: elem_id should have been registered with ums_complist_register by
 * the same thread
 *
 * To return to the scheduler worker context (i.e. return to entry_function):
 * @code
 * ums_sched_yield();
 * @endocode
*/
#ifndef __UMS_SCHEDULER_H__
#define __UMS_SCHEDULER_H__

/**
 * @brief Type of the scheduler id
*/
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
