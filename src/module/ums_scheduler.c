#include "ums_scheduler.h"
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/hashtable.h>
#include <linux/list.h>

struct ums_scheduler {
	ums_sched_id			id;
	ums_complist_id			comp_id;
	struct hlist_node		list;
	struct task_struct __percpu  	**workers;
	struct task_struct		*entry_point;
	struct list_head		wait_procs;
};

struct ums_sched_wait {
	struct task_struct *task;
	struct list_head list;
};

static DEFINE_HASHTABLE(ums_sched_hash, UMS_SCHED_HASH_BITS);

static atomic_t ums_sched_counter = ATOMIC_INIT(0);

static void init_ums_scheduler(struct ums_scheduler* sched, 
			       ums_sched_id id,
			       ums_complist_id comp_id);

static void deinit_ums_scheduler(struct ums_scheduler* sched);

static void get_sched_by_id(ums_sched_id id, 
			    struct ums_scheduler** sched);

int ums_sched_add(ums_complist_id comp_list_id, ums_sched_id* identifier)
{
	struct ums_scheduler* ums_sched = NULL;

	if (! ums_complist_exists(comp_list_id)) {
		printk("Error in %s: complist %d does not exist", __func__, comp_list_id);
		return -1;
	}
	//	return ERROR_MISSING_COMPLIST;

	*identifier = atomic_inc_return(&ums_sched_counter);

	ums_sched = (struct ums_scheduler*) kmalloc(sizeof(struct ums_scheduler), GFP_KERNEL);

	init_ums_scheduler(ums_sched, *identifier, comp_list_id);
	return 0;
}


int ums_sched_register_sched_thread(ums_sched_id sched_id)
{
	int res = 0;
	struct task_struct *worker = NULL;
	struct ums_scheduler* sched;

       	get_sched_by_id(sched_id, &sched);

	if (unlikely(!sched)) {
		res = -1;
		goto register_thread_exit;
	}

	worker = *get_cpu_ptr(sched->workers);

	/* Error: already registered */
	if (worker) {
		res = -2; 
		goto register_thread_exit;
	}

	/* set cpu var to current. */
	*get_cpu_ptr(sched->workers) = current;


	put_cpu_var(sched->workers);

register_thread_exit:
	return res;
}

int ums_sched_register_entry_point(ums_sched_id sched_id)
{
	struct ums_scheduler* sched;

	get_sched_by_id(sched_id, &sched);

	if (!sched)
		return -1;

	sched->entry_point = current;

	/* block the process */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();

	return 0;
}

int ums_sched_wait(ums_sched_id sched_id)
{
	struct ums_scheduler *sched;
	struct ums_sched_wait *wait;

	get_sched_by_id(sched_id, &sched);

	if (! sched)
		return -1;

	wait = kmalloc(sizeof(struct ums_sched_wait), GFP_KERNEL);

	if (! wait)
		return -1;

	wait->task = current;
	list_add(&wait->list, &sched->wait_procs);

	set_current_state(TASK_INTERRUPTIBLE);
	schedule();

	return 0;
}

int ums_sched_remove(ums_sched_id id)
{
	struct ums_scheduler *sched;

	get_sched_by_id(id, &sched);

	if (! sched)
		return -1;

	deinit_ums_scheduler(sched);

	kfree(sched);
	
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

	sched->entry_point = NULL;

	INIT_LIST_HEAD(&sched->wait_procs);
}

static void deinit_ums_scheduler(struct ums_scheduler* sched)
{
	struct list_head *list_iter;
	struct list_head *safe_temp;
	int cpu;

	sched->id = -1;
	sched->comp_id = -1;

	hash_del(&sched->list);

	/* kill all the workers */
	for_each_possible_cpu(cpu) {
		struct task_struct *worker = *per_cpu_ptr(sched->workers, cpu);

		/* TODO: not sure about the privilege */
		send_sig(SIGKILL, worker, 0);
	}

	free_percpu(sched->workers);

	list_for_each_safe(list_iter, safe_temp, &sched->wait_procs) {
		struct ums_sched_wait *wait;

		wait = list_entry(list_iter, struct ums_sched_wait, list);

		/* TODO: wakeup_process o wakepu_state? */
		wake_up_process(wait->task);

		list_del(&wait->list);

		kfree(wait);
	}
}

static void get_sched_by_id(ums_sched_id id, 
			    struct ums_scheduler** sched)
{
	*sched = NULL;

	hash_for_each_possible(ums_sched_hash, *sched, list, id) {
		if ((*sched)->id == id) break;
	}
}
