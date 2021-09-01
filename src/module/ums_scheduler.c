#include "ums_scheduler.h"
#include "ums_scheduler_internal.h"
#include "ums_complist.h"
#include "ums_complist_internal.h"
#include "ums_context_switch.h"

#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/hashtable.h>
#include <linux/list.h>

#define get_worker(sched) (*get_cpu_ptr(sched->workers))

static DEFINE_HASHTABLE(ums_sched_hash, UMS_SCHED_HASH_BITS);

/* get a `ums_sched_worker` from its pid */
static DEFINE_HASHTABLE(ums_sched_worker_hash, UMS_SCHED_HASH_BITS);

static atomic_t ums_sched_counter = ATOMIC_INIT(0);

static void init_ums_scheduler(struct ums_scheduler* sched, 
			       ums_sched_id id,
			       ums_complist_id comp_id);

static void deinit_ums_scheduler(struct ums_scheduler* sched);

/* TODO: turn this functions into macros */
static void get_sched_by_id(ums_sched_id id, 
			    struct ums_scheduler** sched);

static void get_worker_by_current(struct ums_sched_worker **worker);

int ums_sched_add(ums_complist_id comp_list_id, ums_sched_id* identifier)
{
	struct ums_scheduler* ums_sched = NULL;
	struct ums_complist * complist = NULL;

	__get_from_complist_id(comp_list_id, &complist);
       	
	if (! complist) {
		printk("Error in %s: complist %d does not exist", __func__, comp_list_id);
		return -1;
	}

	*identifier = atomic_inc_return(&ums_sched_counter);

	ums_sched = (struct ums_scheduler*) kmalloc(sizeof(struct ums_scheduler), GFP_KERNEL);

	init_ums_scheduler(ums_sched, *identifier, comp_list_id);
	
	/* TODO: add complist reader lock */
	__complist_add_sched(complist, &ums_sched->complist_head);
	return 0;
}


int ums_sched_register_sched_thread(ums_sched_id sched_id)
{
	int res = 0;
	struct ums_sched_worker *worker = NULL;
	struct ums_scheduler* sched;

       	get_sched_by_id(sched_id, &sched);

	if (unlikely(!sched)) {
		res = -1;
		goto register_thread_exit;
	}


	worker = get_worker(sched);


	printk(KERN_ERR "worker pid: %d, current cpu: %d", current->pid, task_cpu(current));
	/* Error: already registered */
	if (worker->worker) {
		res = -2; 
		goto register_thread_exit;
	}

	/* set cpu var to current. */
	worker->owner = sched;
	worker->worker = current;

	printk("get_ums_context");

	gen_ums_context(current, &worker->entry_ctx);
	printk("end get_ums_context");

	printk(KERN_DEBUG "thread pt_reg of pid= %d", current->pid);
	dump_pt_regs(worker->entry_ctx);

	put_cpu_ptr(sched->workers);

	hash_add(ums_sched_worker_hash, &worker->list, worker->worker->pid);

register_thread_exit:

	return res;
}

int ums_sched_wait(ums_sched_id sched_id)
{
	struct ums_scheduler *sched;
	struct ums_sched_wait *wait;

	get_sched_by_id(sched_id, &sched);

	if (! sched)
		return -1;

	/* TODO: this kmalloc is non-sense */
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

int ums_sched_yield(void)
{
	struct ums_sched_worker *worker;

	get_worker_by_current(&worker);

	if (unlikely(! worker))
		return -1;

	// worker = get_worker(sched);

	/* save compelem state if necessary */
	if (worker->current_elem)
		ums_compelem_store_reg(worker->current_elem);
	
	/* set current to entry_point */
	worker->current_elem = 0;

	printk(KERN_DEBUG "yield pt_reg of pid= %d", current->pid);
	dump_pt_regs(worker->entry_ctx);

	printk(KERN_DEBUG "yield pt_regs before change %d", current->pid);
	__dump_pt_regs(task_pt_regs(current));
	put_ums_context(current, &worker->entry_ctx);

	return 0;
}

int ums_sched_exec(ums_compelem_id elem_id)
{
	struct ums_sched_worker *worker;
	int res = 0;
	
	get_worker_by_current(&worker);

	if (unlikely(! worker))
		return -1;

	/* if executed by a worker restore */
	if (worker->current_elem)
		ums_compelem_store_reg(worker->current_elem);

	/* mark as the runner */
	worker->current_elem = elem_id;

	res = ums_compelem_exec(elem_id);

	return res;
}

int ums_sched_complist_by_current(ums_complist_id *res_id)
{
	struct ums_sched_worker *worker;

	get_worker_by_current(&worker);

	if (! worker)
		return -1;

	*res_id = worker->owner->comp_id;

	return 0;
}

int ums_sched_init(void)
{
	hash_init(ums_sched_hash);
	hash_init(ums_sched_worker_hash);
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

	sched->workers = alloc_percpu(struct ums_sched_worker*);

	/* Init as NULL */
	for_each_possible_cpu(cpu) {
		struct ums_sched_worker *worker;
		worker = kmalloc(sizeof(struct ums_sched_worker), GFP_KERNEL);

		worker->worker = NULL;
		(*per_cpu_ptr(sched->workers, cpu)) = worker;
	}

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
		struct ums_sched_worker *worker = *per_cpu_ptr(sched->workers, cpu);

		/* TODO: handle signals to safely die */
		send_sig(SIGKILL, worker->worker, 0);

		// TODO: set completion element status as `free`
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

static void get_worker_by_current(struct ums_sched_worker **worker)
{
	*worker = NULL;

	hash_for_each_possible(ums_sched_worker_hash, *worker, list, current->pid) {
		if ((*worker)->worker->pid == current->pid) break;
	}
}
