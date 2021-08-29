#include "ums_scheduler.h"
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/ptrace.h>
#include <asm/processor.h>

#define get_worker(sched) (*get_cpu_ptr(sched->workers))
struct ums_sched_worker {
	struct task_struct *worker;
	ums_compelem_id current_elem;
};

struct ums_scheduler {
	ums_sched_id				id;
	ums_complist_id				comp_id;
	struct hlist_node			list;
	struct ums_sched_worker __percpu	**workers;
	struct task_struct			*entry_point;
	struct list_head			wait_procs;
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

static void sched_switch(struct ums_sched_worker *worker,
			 struct task_struct *new_task,
			 ums_compelem_id new_compelem);

static void get_sched_by_id(ums_sched_id id, 
			    struct ums_scheduler** sched);

static void _check_caller(struct ums_sched_worker *worker);

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
	struct ums_sched_worker *worker = NULL;
	struct ums_scheduler* sched;

       	get_sched_by_id(sched_id, &sched);

	if (unlikely(!sched)) {
		res = -1;
		goto register_thread_exit;
	}


	worker = get_worker(sched);


	/* Error: already registered */
	if (worker->worker) {
		res = -2; 
		goto register_thread_exit;
	}

	/* set cpu var to current. */
	worker->worker = current;

	put_cpu_var(sched->workers);

	/* TODO: this is temporary, a proper sync mechanism will be implemented */
	if (!sched->entry_point) {
		res = -3;
		goto register_thread_exit;
	}

	sched_switch(worker, sched->entry_point, 0);
register_thread_exit:
	return res;
}

int ums_sched_register_entry_point(ums_sched_id sched_id)
{
	struct ums_scheduler* sched;

	get_sched_by_id(sched_id, &sched);

	printk(KERN_DEBUG "sched_id=%d, sched=%p", sched_id, sched);
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

int ums_sched_yield(ums_sched_id id)
{
	struct ums_scheduler *sched;
	struct ums_sched_worker *worker;
	struct task_struct *new_task;


	get_sched_by_id(id, &sched);

	if (! sched)
		return -1;

	new_task = sched->entry_point;

	worker = get_worker(sched);

	sched_switch(worker, new_task, 0);
	return 0;
}

int ums_sched_exec(ums_sched_id id,
		   ums_compelem_id elem_id)
{
	struct ums_scheduler *sched;
	struct ums_sched_worker *worker;
	int res = 0;
	
	get_sched_by_id(id, &sched);

	if (! sched)
		return -1;

	worker = get_worker(sched);

	/* if executed by a worker restore */
	if (worker->current_elem)
		ums_compelem_store_reg(worker->current_elem);

	/* mark as the runner */
	worker->current_elem = elem_id;

	res = ums_compelem_exec(elem_id);

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

	sched->workers = alloc_percpu(struct ums_sched_worker*);

	/* Init as NULL */
	for_each_possible_cpu(cpu) {
		struct ums_sched_worker *worker;
		worker = kmalloc(sizeof(struct ums_sched_worker), GFP_KERNEL);

		worker->worker = NULL;
		(*per_cpu_ptr(sched->workers, cpu)) = worker;
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

static void sched_switch(struct ums_sched_worker *worker,
			 struct task_struct *new_task,
			 ums_compelem_id new_compelem)
{
	struct pt_regs *current_regs, *target_regs;
	_check_caller(worker);

	printk(KERN_DEBUG "Entering %s", __func__);

	current_regs = current_pt_regs();
	target_regs = task_pt_regs(new_task);

	/* store current pt_regs in its  */
	/* current elem = 0 means we are running `entry_point` */
	if (worker->current_elem) {
		ums_compelem_store_reg(worker->current_elem);
	}

	worker->current_elem = new_compelem;
	/* TODO: notify complist! */

	memcpy(current_regs, target_regs, sizeof(struct pt_regs));
	printk(KERN_DEBUG "Exit %s", __func__);
}

static void _check_caller(struct ums_sched_worker *worker)
{

}
