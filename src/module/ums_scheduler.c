#include "ums_scheduler.h"
#include "ums_scheduler_internal.h"
#include "ums_complist.h"
#include "ums_complist_internal.h"
#include "ums_context_switch.h"
#include "id_rwlock.h"
#include "ums_proc.h"

#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/timekeeping.h>

#define get_worker(sched) (*get_cpu_ptr(sched->workers))

#define WORKER_INFO_FILE "info"
#define WORKER_FILE_MODE 0444
#define SCHEDULER_DIR_NAME "schedulers"

static DEFINE_HASHRWLOCK(ums_sched_hash, UMS_SCHED_HASH_BITS);

/* get a `ums_sched_worker` from its pid */
static DEFINE_HASHTABLE(ums_sched_worker_hash, UMS_SCHED_HASH_BITS);

static atomic_t ums_sched_counter = ATOMIC_INIT(0);

/* procfs */
static ssize_t sched_worker_proc_read(struct file *file,
				      char __user *ubuf, 
				      size_t count,
				      loff_t *ppos);

static struct proc_ops ums_sched_worker_proc_ops =
{
	.proc_read = sched_worker_proc_read,
};

static struct proc_dir_entry *ums_scheduler_dir_entry = NULL;

static void init_ums_scheduler(struct ums_scheduler* sched, 
			       ums_sched_id id,
			       ums_complist_id comp_id);

static void deinit_ums_scheduler(struct ums_scheduler* sched);

static void get_worker_by_current(struct ums_sched_worker **worker);

int ums_sched_add(ums_complist_id comp_list_id, ums_sched_id* identifier)
{
	struct ums_scheduler* ums_sched = NULL;

	*identifier = atomic_inc_return(&ums_sched_counter);

	ums_sched = (struct ums_scheduler*) kmalloc(sizeof(struct ums_scheduler), GFP_KERNEL);

	init_ums_scheduler(ums_sched, *identifier, comp_list_id);
	
	/* This function has 2 important goals:
	 * check if complist with `comp_list_id` exists
	 * append the current scheduler entry in the list 
	*/

	/* TODO: use id instead of list (otherwise mess with locking) */
	ums_complist_add_scheduler(comp_list_id, &ums_sched->complist_head);

	return 0;
}


int ums_sched_register_sched_thread(ums_sched_id sched_id)
{
	int res = 0, try_res = 0, iter = 0;
	struct ums_sched_worker *worker = NULL;
	struct ums_scheduler* sched;
	struct id_rwlock *lock;

	hashrwlock_find(ums_sched_hash, sched_id, &lock);

	if (! lock) {
		res = -1;
		goto register_thread_exit;
	}

	if (! lock->data) {
		res = -1;
		goto register_thread_exit;
	}

	id_read_trylock_region(lock, iter, try_res) {
		sched = lock->data;

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
		worker->owner = sched;
		worker->worker = current;
		worker->n_switch = 0;
		worker->switch_time = 0;

		gen_ums_context(current, &worker->entry_ctx);
		put_cpu_ptr(sched->workers);

		hash_add(ums_sched_worker_hash, &worker->list, worker->worker->pid);

		/* generate proc directory */
		ums_proc_geniddir(get_cpu(), sched->proc_dir, &worker->proc_dir);

		/* create proc file */
		worker->proc_info_file = proc_create_data(WORKER_INFO_FILE,
							  WORKER_FILE_MODE,
							  worker->proc_dir,
							  &ums_sched_worker_proc_ops,
							  worker);
	}

	if (! try_res)
		res = -1;
register_thread_exit:

	return res;
}

int ums_sched_wait(ums_sched_id sched_id)
{
	int res, iter;
	struct ums_scheduler *sched;
	struct ums_sched_wait *wait;
	struct id_rwlock *lock;

	hashrwlock_find(ums_sched_hash, sched_id, &lock);

	if (! lock)
		return -1;

	if (! lock->data)
		return -1;

	id_read_trylock_region(lock, iter, res) {
		sched = lock->data;
		wait = kmalloc(sizeof(struct ums_sched_wait), GFP_KERNEL);

		if (! wait)
			return -1;

		wait->task = current;
		list_add(&wait->list, &sched->wait_procs);

		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}

	return !res;
}

int ums_sched_remove(ums_sched_id id)
{
	int iter;
	struct id_rwlock *lock;
	struct ums_scheduler *sched;

	hashrwlock_find(ums_sched_hash, id, &lock);

	if (! lock)
		return -1;

	if (!lock->data)
		return -1;

	id_write_lock_region(lock, iter) {
		sched = lock->data;
		deinit_ums_scheduler(sched);
		lock->data = NULL;
		hashrwlock_remove(lock);
	}

	kfree(sched);
	/* TODO: add rwlock to reclaim list!!! */
	
	return 0;
}

int ums_sched_yield(void)
{
	u64 act_time;
	struct ums_sched_worker *worker;

	get_worker_by_current(&worker);

	if (unlikely(! worker))
		return -1;

	if (! worker->current_elem)
		/* Yield triggered by an entry_point function is an NOP operation */
		return 0;

	act_time = ktime_get_ns();

	/* save compelem state */
	ums_compelem_store_reg(worker->current_elem);
	
	/* set current to entry_point */
	worker->current_elem = 0;

	put_ums_context(current, &worker->entry_ctx);

	worker->switch_time = ktime_get_ns() - act_time;
	worker->n_switch++;

	return 0;
}

int ums_sched_exec(ums_compelem_id elem_id)
{
	struct ums_sched_worker *worker;
	int res = 0;
	u64 act_time;
	printk(KERN_DEBUG "Calling exec on compelem: %d", elem_id);
	get_worker_by_current(&worker);

	if (unlikely(! worker))
		return -1;

	act_time = ktime_get_ns();
	/* if executed by a worker restore */
	if (worker->current_elem)
		ums_compelem_store_reg(worker->current_elem);
	else
		get_ums_context(current, &worker->entry_ctx);

	/* mark as the runner */
	worker->current_elem = elem_id;

	res = ums_compelem_exec(elem_id, worker->owner->id);

	worker->switch_time = ktime_get_ns() - act_time;
	worker->n_switch++;

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
	hashrwlock_init(ums_sched_hash);
	hash_init(ums_sched_worker_hash);
	return 0;
}

int ums_sched_proc_init(struct proc_dir_entry *ums_dir)
{
	ums_scheduler_dir_entry = proc_mkdir(SCHEDULER_DIR_NAME, ums_dir);

	return ! ums_scheduler_dir_entry;
}

void ums_sched_deinit(void)
{
	/* TODO: here various things should be done! */
}

void ums_sched_proc_deinit(void)
{
	ums_proc_delete(ums_scheduler_dir_entry);
	ums_scheduler_dir_entry = NULL;
}

static void init_ums_scheduler(struct ums_scheduler* sched, 
			       ums_sched_id id,
			       ums_complist_id comp_id) 
{
	int cpu;
	struct id_rwlock *lock;

	lock = kmalloc(sizeof(struct id_rwlock), GFP_KERNEL);

	id_rwlock_init(id, sched, lock);

	sched->id = id;
	sched->comp_id = comp_id;

	if (! id_write_trylock(lock))
		printk(KERN_ERR "Expecting lock to be free!\n");

	hashrwlock_add(ums_sched_hash, lock);

	sched->workers = alloc_percpu(struct ums_sched_worker*);

	/* Init as NULL */
	for_each_possible_cpu(cpu) {
		struct ums_sched_worker *worker;
		worker = kmalloc(sizeof(struct ums_sched_worker), GFP_KERNEL);

		worker->worker = NULL;
		(*per_cpu_ptr(sched->workers, cpu)) = worker;
	}

	INIT_LIST_HEAD(&sched->wait_procs);

	ums_proc_geniddir(id, ums_scheduler_dir_entry, &sched->proc_dir);

	id_write_unlock(lock);
}

static void deinit_ums_scheduler(struct ums_scheduler* sched)
{
	struct list_head *list_iter;
	struct list_head *safe_temp;
	int cpu;

	sched->id = -1;
	sched->comp_id = -1;

	/* kill all the workers */
	for_each_possible_cpu(cpu) {
		struct ums_sched_worker *worker = *per_cpu_ptr(sched->workers, cpu);

		send_sig(SIGINT, worker->worker, 0);

		/* remove procfs data */
		ums_proc_delete(worker->proc_info_file);
		ums_proc_delete(worker->proc_dir);
	}

	free_percpu(sched->workers);

	list_for_each_safe(list_iter, safe_temp, &sched->wait_procs) {
		struct ums_sched_wait *wait;

		wait = list_entry(list_iter, struct ums_sched_wait, list);

		wake_up_process(wait->task);

		list_del(&wait->list);

		kfree(wait);
	}

	/* remove scheduler directory */
	ums_proc_delete(sched->proc_dir);
}

static ssize_t sched_worker_proc_read(struct file *file,
				      char __user *ubuf,
				      size_t count,
				      loff_t *ppos)
{
	struct ums_sched_worker *worker;
        char buf[512];
        int len = 0;

        if (*ppos > 0)
                return 0;

	worker = PDE_DATA(file_inode(file));

	if (! worker)
		return -EFAULT;

	/* print pid: */
	len += sprintf(buf + len, "pid=%d\n", worker->worker->pid);

	if (len > count || len < 0)
		return -EFAULT;

	/* print task_state */
	len += sprintf(buf + len, "state=%u\n", task_state_index(worker->worker));

	if (len > count || len < 0)
		return -EFAULT;

	/* print switches */
	len += sprintf(buf + len, "n_switch=%u\n", worker->n_switch);

	if (len > count || len < 0)
		return -EFAULT;

	/* print completion list */
	len += sprintf(buf + len, "complist=%u\n", worker->owner->comp_id);

	if (len > count || len < 0)
		return -EFAULT;
	/* print actual worker */

	len += sprintf(buf + len, "worker=%d\n", worker->current_elem);
	if (len > count || len < 0)
		return -EFAULT;

	/* average switch time */
	len += sprintf(buf + len, "last_switch_t=%llu\n", worker->switch_time);
	if (len > count)
		return -EFAULT;

        if (copy_to_user(ubuf, buf, len))
                return -EFAULT;

        *ppos = len;

        return len;
}

static void get_worker_by_current(struct ums_sched_worker **worker)
{
	*worker = NULL;

	hash_for_each_possible(ums_sched_worker_hash, *worker, list, current->pid) {
		if ((*worker)->worker->pid == current->pid) break;
	}
}
