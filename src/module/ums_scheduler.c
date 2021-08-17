#include "ums_scheduler.h"
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/hashtable.h>

struct ums_scheduler {
	ums_sched_id			id;
	ums_complist_id			comp_id;
	struct hlist_node		list;
	struct task_struct __percpu  	**workers;
};

static DEFINE_HASHTABLE(ums_sched_hash, UMS_SCHED_HASH_BITS);

static atomic_t ums_sched_counter = ATOMIC_INIT(0);

static void init_ums_scheduler(struct ums_scheduler* sched, 
			       ums_sched_id id,
			       ums_complist_id comp_id);

static void get_sched_by_id(ums_sched_id id, 
			    struct ums_scheduler** sched);

int ums_sched_add(ums_complist_id comp_list_id, ums_sched_id* identifier)
{
	struct ums_scheduler* ums_sched = NULL;

	// if (! ums_comp_list_get(comp_list_id))
	//	return ERROR_MISSING_COMPLIST;

	*identifier = atomic_inc_return(&ums_sched_counter);

	ums_sched = (struct ums_scheduler*) kmalloc(sizeof(struct ums_scheduler), GFP_KERNEL);

	init_ums_scheduler(ums_sched, *identifier, comp_list_id);
	return 0;
}


int ums_sched_register_sched_thread(ums_sched_id sched_id, int cpu)
{
	int res = 0;
	struct task_struct *worker = NULL;
	struct ums_scheduler* sched;

       	get_sched_by_id(sched_id, &sched);

	if (unlikely(!sched)) {
		res = -1;
		goto register_thread_exit;
	}

	worker = *per_cpu_ptr(sched->workers, cpu);

	/* Error: already registered */
	if (worker) {
		res = -2; 
		goto register_thread_exit;
	}

	/* set the task_struct* to the current one. */
	*per_cpu_ptr(sched->workers, cpu) = current;

	set_task_cpu(current, cpu);
register_thread_exit:
	return res;
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

static void init_ums_scheduler(struct ums_scheduler* sched, 
			       ums_sched_id id,
			       ums_complist_id comp_id) 
{
	int cpu;

	sched->id = id;
	sched->comp_id = comp_id;

	/* entries are unique, no need for locks */
	hash_add(ums_sched_hash, &sched->list, sched->id);

	sched->workers = alloc_percpu(struct task_struct*);

	/* Init as NULL */
	for_each_possible_cpu(cpu) {
		*per_cpu_ptr(sched->workers, cpu) = NULL;
	}
}

static void get_sched_by_id(ums_sched_id id, 
			    struct ums_scheduler** sched)
{
	hash_for_each_possible(ums_sched_hash, *sched, list, id) {
	}
}
