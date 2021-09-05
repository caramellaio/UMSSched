#ifndef __UMS_SCHEDULER_INTERNAL_H__
#define __UMS_SCHEDULER_INTERNAL_H__

#include "ums_scheduler.h"
#include "ums_complist.h"
#include "ums_context_switch.h"

#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/proc_fs.h>

struct ums_sched_worker {
	struct ums_scheduler *owner;
	ums_compelem_id current_elem;

	struct hlist_node list;

	struct task_struct *worker;
	struct ums_context entry_ctx;

	u64					switch_time;
	unsigned int				n_switch;
	/* procfs directory */
	struct proc_dir_entry			*proc_dir;

	/* info file */
	struct proc_dir_entry			*proc_info_file;
};

struct ums_scheduler {
	ums_sched_id				id;
	ums_complist_id				comp_id;
	/* used by hash */
	struct hlist_node			list;

	struct ums_sched_worker __percpu	**workers;
	/* TODO: remove? */
	struct list_head			wait_procs;

	/* completion list has also a list of affiliated schedulers */
	struct list_head			complist_head;

	/* procfs directory */
	struct proc_dir_entry			*proc_dir;
};

struct ums_sched_wait {
	struct task_struct *task;
	struct list_head list;
};

#endif /* __UMS_SCHEDULER_INTERNAL_H__ */
