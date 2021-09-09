/**
 * @author Alberto Bombardelli
 *
 * @file ums_scheduler_internal.h
 *
 * @brief File with the internal structures of ums scheduler sub-module
 *
 * @sa ums_scheduler.c
 * @sa ums_scheduler.h
*/
#ifndef __UMS_SCHEDULER_INTERNAL_H__
#define __UMS_SCHEDULER_INTERNAL_H__

#include "ums_scheduler.h"
#include "ums_complist.h"
#include "ums_context_switch.h"

#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/proc_fs.h>

/**
 * @struct ums_sched_worker
 *
 * @brief Struct the represent the scheduler thread
 *
 * This struct represents a scheduler thread owned by a ums_scheduler.
 * The scheduler thread MUST run on a specific CPU. 
 *
 * This struct also handles proc file system informations/
 *
*/
struct ums_sched_worker {
	/** @brief scheduler owner of the worker
	 * 
	 * This field permit to have a direct access to the scheduler struct
	*/
	struct ums_scheduler *owner;

	/**
	 * @brief Currently running completion element
	 *
	 * Initialized at 0 (=scheduler function running)
	*/
	ums_compelem_id current_elem;

	/**
	 * @brief Linked completion list
	 *
	 * Is the same one of the owner scheduler
	*/
	ums_complist_id complist_id;

	/**
	 * @brief entry for pid to sched_worker hashtable
	*/
	struct hlist_node list;

	/** 
	 * @brief Worker task that executes completion elements and entry function
	 *
	 * This task is the only task that can manipulate worker directly.
	*/
	struct task_struct *worker;

	/**
	 * @brief The scheduler thread context (user-mode)
	 *
	 * This field contains the data of the registers of the scheduler thread.
	 * This field is used to store the status of the scheduler during context
	 * switches (yield/execute)
	*/
	struct ums_context entry_ctx;

	/** 
	 * @brief Stat on the time required for context switch
	*/
	u64 switch_time;

	/**
	 * @brief Counter to the switch performed by the worker
	*/
	unsigned int n_switch;

	/** 
	 * @brief procfs directory 
	 *
	 * This directory has the name of the assigned CPU and contains the
	 * info file
	*/
	struct proc_dir_entry *proc_dir;

	/**
	 * @brief procfs info file 
	 *
	 * This files contains various info on this scheduler thread
	*/
	struct proc_dir_entry *proc_info_file;
};

struct ums_scheduler {
	/**
	 * @brief Unique identifier of the scheduler
	 *
	 * Scheduler unique identifier
	*/
	ums_sched_id id;

	/**
	 * @brief Memory mapping used by the schedulers thread
	*/
	struct mm_struct *mm;

	/**
	 * @brief Completion list linked to the scheduler
	 *
	 * The completion list will give to this (and possibly other) schedulers
	 * his completion elements to be executed
	*/
	ums_complist_id	comp_id;

	/** 
	 * @brief hash table list node
	 *
	 * used by ums_sched_hash hash table
	*/
	struct hlist_node			list;

	/**
	 * @brier sched worker threads
	 *
	 * Workers that will run on the CPUs
	*/
	struct ums_sched_worker __percpu	**workers;

	/* TODO: remove? */
	struct list_head			wait_procs;

	/**
	 * @brief procfs directory 
	 *
	 * Directory child of schedulers directory, with id as name.
	 * This directory will contains a dir for each sched worker
	*/
	struct proc_dir_entry			*proc_dir;
};

struct ums_sched_wait {
	struct task_struct *task;
	struct list_head list;
};

#endif /* __UMS_SCHEDULER_INTERNAL_H__ */
