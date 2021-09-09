/**
 * @author Alberto Bombardelli
 *
 * @file Implementation file of scheduler sub-module
 *
 * This file contains the function definition and the internal variables of
 * the scheduler sub-module.
 *
 * @sa ums_scheduler.h
 * @sa ums_scheduler_internal.h
*/
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

/**
 * @brief get the currently running worker
 *
 * @param[in] sched: scheduler that owns the worker
 * 
 * @note It is necessary to call put_cpu_ptr when you do not need the
 *	worker anymore.
*/
#define get_worker(sched) (*get_cpu_ptr(sched->workers))

/** 
 * @brief name of the file with worker info
*/
#define WORKER_INFO_FILE "info"

/** 
 * @brief file mode for the `info` file
*/
#define WORKER_FILE_MODE 0444

/**
 * @brief Name of the the toplevel scheduler proc dir
*/
#define SCHEDULER_DIR_NAME "schedulers"

/**
 * @brief Safe hashtable for ums_scheduler
 *
 * The hash table map ums_sched_id to id_rwlock* 
 * where data is a pointer to ums_scheduler
 *
 * @sa ums_sched_add
 * @sa ums_sched_remove
*/
static DEFINE_HASHRWLOCK(ums_sched_hash, UMS_SCHED_HASH_BITS);

/** 
 * @brief Hash table to get a worker by its pid
 *
 * Given the thread pid, returns the corresponding worker
 *
 * @sa ums_sched_register_sched_thread
*/
static DEFINE_HASHTABLE(ums_sched_worker_hash, UMS_SCHED_HASH_BITS);

/**
 * @brief Atomic variable to get a new scheduler id
 *
 * @sa ums_sched_add
 * @sa ums_complist_counter
 * @sa ums_compelem_counter
*/
static atomic_t ums_sched_counter = ATOMIC_INIT(0);

static ssize_t sched_worker_proc_read(struct file *file,
				      char __user *ubuf, 
				      size_t count,
				      loff_t *ppos);

/**
 * @brief scheduler worker legal operation for proc files
 *
 * The only allowed operation is read and is carried by a function
 * that turns the worker data structure into stats and useful data.
 *
 * @sa sched_worker_proc_read
 * @sa ums_compelem_proc_ops
*/
static struct proc_ops ums_sched_worker_proc_ops =
{
	.proc_read = sched_worker_proc_read,
};

/**
 * @brief schedulers proc top level directory `schedulers`
 *
 * This is the entry of the proc directory /proc/ums/schedulers
 *
 * @sa ums_scheduler_proc_init
 * @sa ums_scheduler_proc_deinit
*/
static struct proc_dir_entry *ums_scheduler_dir_entry = NULL;

static void init_ums_scheduler(struct ums_scheduler* sched, 
			       ums_sched_id id,
			       ums_complist_id comp_id);

static void deinit_ums_scheduler(struct ums_scheduler* sched);

static void get_worker_by_current(struct ums_sched_worker **worker);

/**
 * @brief Add a new scheduler without registering his workers
 *
 * @param[in] comp_list_id: completion list which will be linked to this scheduler
 * @param[out] identifier: identifier of the new created scheduler
 *
 * This function creates a new scheduler safely and it then calls 
 * `ums_complist_add_scheduler` to link the scheduler to the completion list.
 * @return 0 if no error occured, non-zero otherwise
*/
int ums_sched_add(ums_complist_id comp_list_id, ums_sched_id* identifier)
{
	struct ums_scheduler* ums_sched = NULL;

	*identifier = atomic_inc_return(&ums_sched_counter);

	ums_sched = (struct ums_scheduler*) kmalloc(sizeof(struct ums_scheduler), GFP_KERNEL);

	if (unlikely(ums_sched)) 
		return -EFAULT;

	init_ums_scheduler(ums_sched, *identifier, comp_list_id);
	
	/* This function has 2 important goals:
	 * check if complist with `comp_list_id` exists
	 * append the current scheduler entry in the list 
	*/
	if (ums_complist_add_scheduler(comp_list_id, ums_sched->id, 
				       ums_sched->tgid)) {
		ums_sched_remove(ums_sched->id);
		return -EFAULT;
	}

	return 0;
}

/**
 * @brief Register a new scheduler thread
 *
 * @param sched_id: scheduler id which will own the worker
 *
 * This function register, create and initialized the ums_sched_worker struct.
 *
 * Creates the struct, generate the ums_context from current and register
 * the worker in both scheduler and worker hash.
 *
 * @note This function ASSUMES that the thread will run ONLY on a specific
 *	cpu, to guarantee that the developer should use sched_set_affinity 
 *	accordingly before calling this function.
 *
 * @return 0 if everything is OK, non-zero if either an error occured or
 *	either the scheduler was not registered or another worker has
 *	been registered for that CPU.
*/
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
		worker->complist_id = sched->comp_id;
		worker->worker = current;
		worker->n_switch = 0;
		worker->switch_time = 0;

		gen_ums_context(current, &worker->entry_ctx);
		put_cpu_ptr(sched->workers);

		hash_add_rcu(ums_sched_worker_hash, &worker->list, worker->worker->pid);

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

/**
 * \todo Should I keep this function?
*/
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

/**
 * @brief Remove an user mode scheduler
 *
 * Safely deinit the ums_scheduler structure, send sigint to his stuck worker.
 * This function neither change completion list nor completion elements linked
 * with it.
 *
 * @note The id_rwlock does not get destroyed to prevent concurrent access
 *	to invalid memory regions. The kfree call of id_rwlock is performed
 *	by ums_sched_deinit
 *
 * @sa ums_scheduler
 * @sa id_rwlock.h
 * @sa ums_sched_add
*/
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
	
	return 0;
}

/**
 * @brief Function to return to the original ums_sched_worker context
 *
 * This function pass from the context of a completion element to the context
 * of the scheduler worker. It stores the completion element context and then
 * it put the worker context.
 *
 *
 * @note Calling this function from a worker context has no effect
 *
 * @return 0 if the switch succeed, non-zero if an error occured or the calling
 *	thread is not linked to an existing worker.
 *
 * @sa ums_sched_exec
*/
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

/**
 * @brief Execute a completion element by switching context
 *
 * @param[elem_id] Completion element to execute
 *
 * Execute the current worker with the completion element context. 
 * The function first store the actual context (if it was running another compelem
 * by using ums_compelem_store_reg, otherwise updating its own ums_context)
 * and then trigger the context switch using ums_compelem_exec. If the switch
 * was successful then update the proc statistics
 * 
 * @return 0 if the switch succeed, non-zero if either an internal error occured
 *	or the thread was not authorized to run the completion element
 *
 * @sa ums_sched_yield
 * @sa ums_compelem_exec
*/
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

	if (likely(! res)) {
		worker->switch_time = ktime_get_ns() - act_time;
		worker->n_switch++;
	}

	return res;
}

/**
 * @brief Get the completion element used by the current sched worker
 *
 * @param[out] res_id: resulting completion list identifier
 *
 * The completion element might be use to trigger a dequeue call
 *
 * @return 0 if complist was found, non-zero otherwise
*/
int ums_sched_complist_by_current(ums_complist_id *res_id)
{
	struct ums_sched_worker *worker;

	get_worker_by_current(&worker);

	if (! worker)
		return -EFAULT;

	*res_id = worker->complist_id;

	return ! *res_id;
}

/**
 * @brief Initialize ums scheduler sub-module
 *
 * Initialized the data structures work the schedulers and scheduler workers
 *
 * @return 0 if everything was initialized successfully
*/
int ums_sched_init(void)
{
	hashrwlock_init(ums_sched_hash);
	hash_init(ums_sched_worker_hash);
	return 0;
}

/**
 * @brief Initialize ums sched proc submodule
 *
 * @param[in] ums_dir: the parent directory
 *
 * Creates the new schedulers top-level proc
*/
int ums_sched_proc_init(struct proc_dir_entry *ums_dir)
{
	ums_scheduler_dir_entry = proc_mkdir(SCHEDULER_DIR_NAME, ums_dir);

	return ! ums_scheduler_dir_entry;
}

/**
 * @brief Deinit the scheduler module
 *
 * Cleanup memory and destroy scheduler sub-module data.
 *
 * @return void
*/
void ums_sched_deinit(void)
{
	/* TODO: here various things should be done! */
}

/**
 * @brief Deinit scheduler proc module
 *
 * Remove the schedulers directory 
 *
 * @return void
*/
void ums_sched_proc_deinit(void)
{
	ums_proc_delete(ums_scheduler_dir_entry);
	ums_scheduler_dir_entry = NULL;
}

/**
 * @brief Initialize a new ums_scheduler
 *
 * @param[in, out] sched: scheduler to be initialized
 * @param[in] id: new scheduler id
 * @param[in] comp_id: completion list linked to the scheduler
 *
 * Initialize the workers, set the data and the id_rwlock.
 *
 * @return void
*/
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
	sched->tgid = current->tgid;

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

/**
 * @brief Internal function to deinit ums_scheduler structure
 *
 * @param[in] sched: pointer to the scheduler
 *
 * Set to invalid values the struct fields, send INT signal to workers
 * (that might be stuck in the complist semaphore), remove proc related
 * data and release waiting processes
 *
 * @return void
*/
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

		/* Remove worker from the hash */
		hash_del_rcu(&worker->list);
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

/**
 * @brief proc_ops sched worker read function
 *
 * The scheduler worker proc read function is in charge of parsing the
 * worker data into a readable char buffer to be readed in userspace
 *
 * The information passed to the user:
 * - worker pid 
 * - worker thread state [in integer]
 * - number of switches
 * - completion list linked to it
 * - current completion element
 * - time required in the last switch
*/
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
	len += sprintf(buf + len, "complist=%u\n", worker->complist_id);

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

/**
 * @brief Utility function to get a worker by current pid
 *
 * @param[out] worker: resulting scheduler worker, [NULL] if worker is not found
 *
 * Search for registered ums_sched_worker from the pid 
 *
 * @return void
 * @sa ums_sched_worker_hash
 * @sa ums_sched_worker
 *
*/
static void get_worker_by_current(struct ums_sched_worker **worker)
{
	*worker = NULL;

	hash_for_each_possible(ums_sched_worker_hash, *worker, list, current->pid) {
		if ((*worker)->worker->pid == current->pid) break;
	}
}
