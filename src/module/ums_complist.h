/**
 * @author Alberto Bombardelli
 * @file Public header of the ums complist module
 *
 * Contains the publicly accessible function prototypes of the sub-module
 * complist.
 *
 * To enable the module:
 * @code
 * ums_complist_init();
 * ums_complist_proc_int(ums_entry);
 * @endcode
 *
 * To disable the module:
 * @code
 * ums_complist_proc_deinit();
 * ums_complist_deinit();
 * @endcode
 *
 * To create a new completion list:
 * @code
 * ums_complist_create()
 * @endcode
 *
 * To destroy a completion list:
 * @code
 * // Complist cannot be destroyed directly. Destroy all its compelem
 *
 * for (i = 0; i < n_compelems; i++)
 *	ums_compelem_remove(compelems[i]);
 * @endcode
 *
 * To create a completion element:
 * USERSPACE CODE:
 * @code
 * clone(..., __reg_compelem);
 *
 * int __reg_compelem(void *data) {
 *	func_t func = get_func_by_data(data);
 *	int buff;
 *	int complist = get_complist_by_data();
 *
 *	buff = complist;
 *	ioctl_compelem_new(&buff)
 *	func(buff);
 *	ioctl_complem_remove(buff);
 *	yield();
 * }
 * @endcode
 *
 * KERNEL SPACE CODE:
 *
 * @code
 * ums_compelem_add(complist, &id, &user_buff);
 * // stay frozen until the completion element gets destroyed
 * @endcode
 *
 * To remove a completion element:
 *
 * First of all a completion element should be removed only by himself at the
 * end of the execution of its function.
 *
 * @code
 * ums_compelem_remove(id);
 * @endcode
 *
 * The register completion elementents for the execution:
 * @code
 * int id = ..;
 * int n_res = ..;
 * int size;
 * int *buff = kmalloc(sizeof(int) * n_res, GFP_KERNEL);
 * ums_complist_reseve(id, n_res, buff, &size);
 * ...
 * ...
 * ...
 * // elem is one of the element of the reservation list
 * // host is the ums_scheduler that will host the execution
 * // NOTE: before calling this function you should have already setted the scheduler
 *
 * ums_compelem_execute(elem, host);
 * ...
 * ...
 * ...
 * // store the new context registers and mark elem as executable.
 * ums_compelem_store_reg(elem);
 * @endcode
 *
 * @sa ums_complist.c
 * @sa ums_complist_internal.h
*/
#ifndef __UMS_COMPLIST_H__
#define __UMS_COMPLIST_H__

/**
 * @brief type of the identifier of completion list
*/
typedef int ums_complist_id;

/**
 * @brief type of the identifier of completion element
*/
typedef int ums_compelem_id;
#include "ums_scheduler.h"
#include "ums_proc.h"
#include <linux/list.h>

extern struct proc_dir_entry *ums_proc_dir;

/* TODO: move in C or internal file */
#define UMS_COMPLIST_HASH_BITS 8
#define UMS_COMPELEM_HASH_BITS 8

int ums_complist_add(ums_complist_id *result);

int ums_complist_exists(ums_complist_id comp_id);

int ums_complist_reserve(ums_complist_id comp_id,
			 int to_reserve,
			 ums_compelem_id *ret_array,
			 int *size);

int ums_compelem_add(ums_compelem_id* result,
		     ums_complist_id list_id,
		     void * __user user_data);

int ums_complist_add_scheduler(ums_complist_id id, 
			       ums_sched_id sched_id);

int ums_compelem_remove(ums_compelem_id id);

int ums_compelem_store_reg(ums_compelem_id compelem_id);

int ums_compelem_exec(ums_compelem_id compelem_id,
		      ums_sched_id host_id);

int ums_complist_init(void);

int ums_complist_proc_init(struct proc_dir_entry *ums_dir);

void ums_complist_deinit(void);

void ums_complist_proc_deinit(void);

#endif /* __UMS_COMPLIST_H__ */
