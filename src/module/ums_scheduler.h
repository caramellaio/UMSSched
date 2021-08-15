#ifndef __UMS_SCHEDULER_H__
#define __UMS_SCHEDULER_H__

/* TODO: At the moment I just consider the typedef, I will eventually include
 * also the complist */
//#include "ums_complist.h"
#include <linux/percpu.h>

/* TODO: Choose the type considering the constraints given by atomic_t */
typedef int ums_sched_id;

/* TODO: This typedef will be removed once ums_complist will be implemented */
typedef int comp_list_id;

struct ums_scheduler {
	ums_sched_id	id;
	comp_list_id	comp_id;
	struct task_struct __percpu **workers;
};

int ums_sched_new(comp_list_id comp_list_id, ums_sched_id* identifier);

int ums_sched_deinit(ums_sched_id identifier);


#endif /* __UMS_SCHEDULER_H__ */
