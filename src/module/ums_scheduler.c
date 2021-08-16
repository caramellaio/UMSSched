#include "ums_scheduler.h"
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/hashtable.h>

struct ums_scheduler {
	ums_sched_id			id;
	comp_list_id			comp_id;
	struct hlist_node		list;
	struct task_struct __percpu  	**workers;
};

static DEFINE_HASHTABLE(ums_sched_hash, UMS_SCHED_HASH_BITS);

static atomic_t ums_sched_counter = ATOMIC_INIT(0);

int ums_sched_add(comp_list_id comp_list_id, ums_sched_id* identifier)
{
	struct ums_scheduler* ums_sched = NULL;

	// if (! ums_comp_list_get(comp_list_id))
	//	return ERROR_MISSING_COMPLIST;

	*identifier = atomic_inc_return(&ums_sched_counter);

	ums_sched = (struct ums_scheduler*) kmalloc(sizeof(struct ums_scheduler), GFP_KERNEL);

	ums_sched->id = *identifier;
	ums_sched->comp_id = comp_list_id;

	/* entries are unique, no need for locks */
	hash_add(ums_sched_hash, &ums_sched->list, ums_sched->id);

	return 0;
}


int ums_sched_init(void)
{
	hash_init(ums_sched_hash);
	return 0;
}

void ums_sched_cleanup(void)
{
	/* TODO: here various things should be done! */
}
