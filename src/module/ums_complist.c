#include "ums_complist.h"
#include "ums_complist_internal.h"
/* Only for delete part */
#include "ums_scheduler_internal.h"
#include "id_rwlock.h"
#include "ums_scheduler.h"
#include "ums_proc.h"

#include <linux/kfifo.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/sched/task_stack.h>
#include <asm/processor.h>

/**
 * @brief Constant that represent the fact that a compelem has no host
 *
 * It is stored in the host field of the ums_compelem struct when the 
 * completion element is not executed by any scheduler thread.
*/
#define COMPELEM_NO_HOST 0

/**
 * @brief Mark a completion element as reserved
 *
 * @param[in] elem: completion element to be marked
 * @param[in] list: list_head that contains the real list
 *
 * This macro set the completion element reserve list, each reserved in the 
 * same group shares the same reserve_list. This macro must be called
 * after getting from the ums_complist its free completion element.
 * The general reservation mechanism is described in ums_complist_reserve
 * and reserve_compelem.
 *
 * @note This macro assumes that the completion list can be
 *	safely reserved
 *
 * @warning Do not call without reserving it with the semaphore/queue
 *	mechanism
 *
 * @return No return value (do/while macro)
 *
 * @sa ums_complist
 * @sa ums_compelem
 * @sa __set_released
 * @sa __register_compelem
 * @sa reserve_compelem
 * @sa ums_complist_reserve
*/
#define __set_reserved(elem, list)				\
	do {							\
		(elem)->reserve_head = list;			\
		list_add(&(elem)->reserve_list, list);		\
	 } while (0)

/**
 * @brief unmark a completion element as reserved
 *
 * @param[in] elem: completion element to be marked
 *
 * This macro reset the completion element reserve list, each reserved in the 
 * same group shares the same reserve_list. This macro must be called
 * after getting from the ums_complist its free completion element.
 * Further indication on the release mechanism can be found in ums_compelem_exec
 *
 * @note This macro covers only a part of the release mechanism implemented
 *	in this module. 
 *
 * @warning Do not call without reserving it with the semaphore/queue
 *	mechanism
 *
 * @return No return value (do/while macro)
 *
 * @sa ums_complist
 * @sa ums_compelem
 * @sa __set_released
 * @sa __register_compelem
 * @sa ums_compelem_exec
 * @sa __register_compelem
*/
#define __set_released(elem)					\
	do {							\
		if (likely((elem)->reserve_list))		\
			list_del(&(elem)->reserve_list);	\
		else						\
			printk(KERN_ALERT			\
			       "Error in  __set_release")	\
		(elem)->reserve_head = NULL;			\
	} while (0)

/**
 * @brief Register the compelement in complist as ready
 *
 * @param complist: completion list in which compelem get registered as ready
 * @param compelem: completion elem to be marked as ready
 *
 * This function register the completion element inside the complist. It used
 * a locked kfifo to safely insert the completion element inside the queue,
 * then it triggers an up to the integer semaphore used by complist.
 *
 * This mechanism is the dual of the reservation mechanism that calls down
 * to ensure that he can access to the queue and then safely calls 
 * kfifo_in_spinlocked.
 *
 *
 * @return No return value (do/while macro)
 *
 * @note This function does not check that a compelem is inserted twice, 
 *	that is a responsability of the developer.
 * @sa ums_compelem_add
 * @sa ums_compelem_store_reg
 * @sa ums_complist
 * @sa ums_compelem
*/
#define __register_compelem(complist, compelem)			\
	do {							\
		/* check if lock is necessary for kfifo */	\
		kfifo_in_spinlocked(&(complist)->ready_queue,	\
				    &(compelem),		\
				    sizeof(compelem),		\
				    &(complist)->ready_lock);	\
		up(&complist->elem_sem);			\
	} while (0)

/**
 * @brief Constant for the idle string used in complem proc file
*/
#define COMPELEM_IDLE_STR "idl"

/**
 * @brief Constant for the running string used in complem proc file
*/
#define COMPELEM_RUNNING_STR "run"

/**
 * @brief Get the string value of the state of a ums_compelem
 *
 * @param[in] completion element used to get the state
 *
 * The state is determined by the current host scheduler: if the
 * element currently has an host then it is running, otherwise
 * it is in idle state.
 *
 * @return COMPELEM_RUNNING_STR if the element is running otherwise
 *	   COMPELEM_IDLE_STR.
*/
#define __str_state(compelem)					\
	((compelem)->host_id == COMPELEM_NO_HOST ?		\
		COMPELEM_IDLE_STR : COMPELEM_RUNNING_STR)

/**
 * @brief Calculate the total active time of a ums_compelem in ns
 *
 * @param[in] completion element that is used for the calculation
 *
 * The time is calculated in 2 ways: if the element is idle, then
 * the total time is compelem->total_time, if the element is
 * running it is necessary to sum total_time with the actual 
 * time - the switch time.
 *
 * @return time in ns (type u64
*/
#define __calc_time(compelem)					\
	((compelem)->host_id == COMPELEM_NO_HOST ?		\
	 (compelem)->total_time :				\
	  (compelem)->total_time + ktime_get_ns() -		\
	   (compelem)->switch_time)

/**
 * @brief constant for the access mode on proc compelem files
*/
#define COMPELEM_FILE_MODE 0444

/**
 * @brief constant for the name of compeltion lists proc top directory
*/
#define COMPLIST_DIR_NAME "completion_lists"

/**
 * @brief hash_rwlock of the completion lists
 *
 * Delete safe completion list hash to safely read and write to the
 * completion list. Maps identifiers to real ums_complist structures
 *
 *
 * \sa id_rwlock.h
 * \sa DEFINE_HASHRWLOCK
*/
static DEFINE_HASHRWLOCK(ums_complist_hash, UMS_COMPLIST_HASH_BITS);

/**
 * @brief Hash table of completion elements
 *
 * Maps compelem_id to ums_compelem. No need to use rwlocks since 
 * completion lists are accessed only by a thread at the same time.
 *
*/
static DEFINE_HASHTABLE(ums_compelem_hash, UMS_COMPELEM_HASH_BITS);

/**
 * @brief Atomic counter for the complist ids
 *
 * This counter is used to define concurrently new completion lists
 * using atomic_inc_return which atomically increments and return the
 * incremented value.
 *
 * @sa ums_complist_id
 * @sa ums_complist_hash
*/
static atomic_t ums_complist_counter = ATOMIC_INIT(0);

/**
 * @brief Atomic counter for the compelem ids
 *
 * This counter is used to define concurrently new completion lists
 * using atomic_inc_return which atomically increments and return the
 * incremented value.
 *
 * @sa ums_compelem_id
 * @sa ums_compelem_hash
*/
static atomic_t ums_compelem_counter = ATOMIC_INIT(0);

/**
 * @brief completion list proc top level directory `completion_lists`
 *
 * This is the entry of the proc directory /proc/ums/compl
 *
 * @sa ums_complist_proc_init
 * @sa ums_complist_proc_deinit
*/
static struct proc_dir_entry *ums_complist_dir = NULL;

static ssize_t compelem_proc_read(struct file *file,
				  char __user *ubuf, 
				  size_t count,
				  loff_t *ppos);

/**
 * @brief completion element legal operation for proc files
 *
 * The only allowed operation is read and is carried by a function
 * that turns the compelem data structure into stats and useful data.
 *
 * @sa compelem_proc_read
 * @sa ums_compelem
*/
static struct proc_ops ums_compelem_proc_ops =
{
	.proc_read = compelem_proc_read,
};

/* end procfs */

/**
 * @struct id_entry
 * @brief Internal complist identifier entry for list
 *
 * These identifiers are used to keep track of the currently used schedulers.
 * They are owned by ums_complist
 * @var id: scheduler identifier
 * @var list: list_head used by ums_complist
 *
 * @sa ums_complist
 * @sa ums_scheduler
*/
struct id_entry {
	int id;
	struct list_head list;
};

static int new_complist(ums_complist_id comp_id,
			 struct ums_complist *complist);

static int delete_complist(struct ums_complist *complist);

static int new_compelement(ums_compelem_id elem_id,
			   struct ums_complist *complist,
			   struct ums_compelem *comp_elem);


static int reserve_compelem(struct ums_complist *complist,
			    struct ums_compelem **compelem,
			    struct list_head *reserve_head,
			    int do_sleep);

/**
 *
 * @brief Add a new empty completion list
 *
 * @param[out] result: identifier of the new created list
 *
 * @return 0 if no error occured, non-zero otherwise
*/
int ums_complist_add(ums_complist_id *result)
{
	int res;
	struct ums_complist* ums_complist;
	struct id_rwlock *lock;

	*result = atomic_inc_return(&ums_complist_counter);

	ums_complist = (struct ums_complist*) kmalloc(sizeof(struct ums_complist),
						      GFP_KERNEL);

	if (! ums_complist) {
		return -1;
	}

	res = new_complist(*result, ums_complist);


	if (res) {
		kfree(ums_complist);
	}

	lock = kmalloc(sizeof(struct id_rwlock), GFP_KERNEL);
	id_rwlock_init(*result, ums_complist, lock);

	hashrwlock_add(ums_complist_hash, lock);

	return res;
}

/**
 * @brief Register a scheduler to the completion list
 *
 * @param[in] id: identifier of the completion list
 * @param[in] sched_id: identifier of the scheduler to be added
 *
 * Insert the index of the scheduler inside a list used to keep track of the
 * scheduler that are using this completion list.
 * 
 * @note The procedure uses spin_lock for the list and read lock for the 
 *	scheduler, hence, it is thread safe.
 *
 * @return 0 if no error occured, non-zero otherwise
*/
int ums_complist_add_scheduler(ums_complist_id id, 
			       ums_sched_id sched_id)
{
	int res;
	int iter;
	struct id_rwlock *lock;
	struct ums_complist *complist;
	struct id_entry *sched_list;

	hashrwlock_find(ums_complist_hash, id, &lock);

	if (! lock)
		return -1;

	if (! lock->data)
		return -1;

	res = 0;

	complist = lock->data;

	sched_list = kmalloc(sizeof(struct id_entry), GFP_KERNEL);

	sched_list->id = sched_id;

	id_read_trylock_region(lock, iter, res) {
		spin_lock(&complist->schedulers_lock);
		list_add(&sched_list->list, &complist->schedulers);
		spin_unlock(&complist->schedulers_lock);
	}

	return res;
}

/* TODO: Eventually set this function as static */
int ums_complist_remove(ums_complist_id id)
{
	int iter;
	struct ums_complist *complist;
	struct id_rwlock *lock;

	hashrwlock_find(ums_complist_hash, id, &lock);

	if (! lock)
		return -1;

	if (! lock->data)
		return -1;

	id_write_lock_region(lock, iter)
		delete_complist(complist);

	/* TODO: remove entry!!! */

	return 0;
}

/**
 * @brief Add a new completion list
 *
 * @param[out] result: the new compelem identifier
 * @param[in] list_id: the already existing complist that will contains compelem
 * @param user_data: A user mode pointer in which is going to be stored the result id
 *
 * This function check if the completion list exists, 
 * then creates safely the new completion element, 
 * register it in the completion list as executable, copy the new id to user
 * and then set task state as interruptible.
 *
 * @sa ums_compelem_remove
 * @sa ums_complist_create
 * @sa __register_compelem
 *
 * @return EFAULT if the function fails, otherwise it gets stuck until remove is called.
*/
int ums_compelem_add(ums_compelem_id* result,
		     ums_complist_id list_id,
		     void * __user user_data)
{
	struct ums_compelem *compelem;
	struct ums_complist *complist;
	struct id_rwlock *lock;
	int iter, locked;
	int res = 0;

	*result = atomic_inc_return(&ums_compelem_counter);

	hashrwlock_find(ums_complist_hash, list_id, &lock);

	if (! lock) {
		printk(KERN_DEBUG "Complist %d not found!\n", list_id);
		return -1;
	}

	if (! lock->data) {
		printk(KERN_DEBUG "Complist %d has been deleted!\n", list_id);
		return -1;
	}

	id_read_trylock_region(lock, iter, locked) {
		complist = lock->data;
		compelem = kmalloc(sizeof(struct ums_compelem), GFP_KERNEL);

		if (! compelem) 
			return -1;

		res = new_compelement(*result, complist, compelem);

		__register_compelem(complist, compelem);
	}

	/* lock failed: complist is getting destroyed */
	if (! locked)
		return -1;

	/* copy to the user the current id before sleeping forever */
	/* When a scheduler thread will wake up he will have the correct 
	 * informations */
	if (copy_to_user(user_data, result, sizeof(ums_compelem_id)))
		return -1;

	/* block completion element */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();

	return res;
}

/**
 * @brief Remove a completion element
 *
 * @param[in] id: completion element identifier
 *
 * This function is in charge of removing the completion element.
 * It has also another effect: if it find out that the completion list
 * after the removal of "self" is empty, it then triggers delete_complist
 *
 * @warning This function is not thread safe. Is the developer that is responsible
 * of enacting a single use policy of this function (when the same id is the
 * parameter)
 *
 * @return 0 if everything is ok, non-zero otherwise
*/
int ums_compelem_remove(ums_compelem_id id)
{
	struct ums_compelem *compelem;

	__get_from_compelem_id(id, &compelem);

	if (! compelem)
		return -EFAULT;

	hash_del(&compelem->list);

	if (compelem->reserve_head) {
		list_del(&compelem->reserve_list);
		if (list_empty(compelem->reserve_head))
			kfree(compelem->reserve_head);
	}


	printk(KERN_DEBUG "Delete compelem %d proc file", id);

	/* critical list region */
	spin_lock(&compelem->complist->compelems_lock);

	/* remove from the list, if list is empty delete complist! */
	list_del(&compelem->complist_head);

	if (list_empty(&compelem->complist->compelems))
		delete_complist(compelem->complist);

	spin_unlock(&compelem->complist->compelems_lock);

	ums_proc_delete(compelem->proc_file);

	wake_up_process(compelem->elem_task);
	kfree(compelem);

	return 0;
}

/**
 * @brief Initialize the completion list sub-module
 *
 * @return 0 if the module is successfully created, non-zero otherwise
*/
int ums_complist_init(void)
{
	/* TODO: use hash_rwlock_init */
	hash_init(ums_complist_hash);
	hash_init(ums_compelem_hash);

	return 0;
}

/**
 * @brief Initialize completion list proc submodule
 *
 * @param ums_dir ums directory, which is going to be the parent dir of completion_list dir
 * Initialized the proc directory used in this file to handle proc filesystem info.
 *
 * @sa COMPLIST_DIR_NAME
 * @sa ums_complist_dir
 *
 * @return 0 if the directory is succesfully created, non-zero otherwise
 *
*/
int ums_complist_proc_init(struct proc_dir_entry *ums_dir)
{
	ums_complist_dir = proc_mkdir(COMPLIST_DIR_NAME, ums_dir);

	return !ums_complist_dir;
}

/**
 * @brief Deinitialize ums completion list sub-module
 * 
 * Remove completion list and completion elements hash and structures that
 * were used in this sub-module
 *
 * @sa ums_complist_proc_init
 * @sa ums_complist_deinit
 * @sa ums_complist_init
 * @sa ums_complist_dir
 *
 * @return void
*/
void ums_complist_deinit(void)
{
	// TODO: implement
}

/**
 * @brief Deinitialize ums completion list proc sub-module
 * 
 * Remove `completion_list` folder entry.
 *
 * @sa ums_complist_proc_init
 * @sa ums_complist_deinit
 * @sa ums_complist_init
 * @sa ums_complist_dir
 *
 * @return void
*/
void ums_complist_proc_deinit(void)
{
	ums_proc_delete(ums_complist_dir);
	ums_complist_dir = NULL;
}

/**
 * @brief Reserve a list of completion element
 *
 * @param[in] comp_id: identifier of the completion list 
 * @param[in] to_reserve: the maximum number of completion element to be reserved
 * @param[out] ret_array: a pointer to an already initialized array that stores the result
 * @param[out] size: resulting size of ret_array
 *
 * This function is in charge of reserving completion elements to the threads 
 * that wants to execute them. The semantinc is the following: the function
 * will try to get at-least one element. 
 *
 * The first element is reserved using a down_interruptible semaphore instruction,
 * i.e. if there is no free element the process get put on wait. 
 * Eventually a completion element will get free'd and then it will get out
 * of this lock state. It that does not occurs, it is sufficient to wake-up the
 * process using and SIGINT signal.
 *
 *
 * @todo Fix the return values
 *
 * @sa ums_compelem_exec
 * @sa reserve_compelem
 *
 * @return 0 if everything is ok, non-zero othewise. 
 * Failures can be due: internal errors, interruptions during wait, 
 *	absense of completion list
*/
int ums_complist_reserve(ums_complist_id comp_id,
			 int to_reserve,
			 ums_compelem_id *ret_array,
			 int *size)
{
	int i;
	int res;
	struct ums_complist *complist;
	struct ums_compelem *compelem_0;
	struct id_rwlock *lock;
	struct list_head *reserve_list;

	res = 0;
	*size = 0;

	/* Even if at the moment lock is not necessary for this feature it is 
	 * better to leave it active */
	hashrwlock_find(ums_complist_hash, comp_id, &lock);

	if (! lock)
		return -1;

	if (! lock->data)
		return -1;

	complist = lock->data;

	if (! id_read_trylock(lock))
		return -1;

	/* reserve list is destroyed in Execute function by the choosen compelem */
	reserve_list = kmalloc(sizeof(struct list_head), GFP_KERNEL);

	if (unlikely(! reserve_list)) {
		res = -2;
		goto complist_reserve_exit;
	}

	INIT_LIST_HEAD(reserve_list);

	if (to_reserve == 0) {
		res = 0;
		goto complist_reserve_exit;
	}

	if (unlikely(reserve_compelem(complist, &compelem_0, reserve_list, 1))) {
		res = -2;
		goto complist_reserve_exit;
	}

	if (unlikely(! compelem_0)) {
		res = -2;
		goto complist_reserve_exit;
	}

	ret_array[0] = compelem_0->id;

	for (i = 1; i < to_reserve; i++) {
		struct ums_compelem *compelem_i = NULL;

		if (unlikely(reserve_compelem(complist, &compelem_i,
					      reserve_list, 0))) {
			res = -2;
			goto complist_reserve_exit;
		}

		if (! compelem_i)
			break;
		ret_array[i] = compelem_i->id;
	}

	*size = i;

complist_reserve_exit:
	id_read_unlock(lock);
	return res;
}

/***
 * @brief Update the context of the compelem
 *
 * @param compelem_id: completion element identifier
 *
 * This procedure set the context using current, mark compelem as free and
 * the executable by another scheduler thread. Also the statistics are updated.
 * 
 * @note This function must be called by the worker that did execute the compelem.
 *
 * @return 0 if everything is OK, -EFAULT if the completion 
 * element does not exist
*/
int ums_compelem_store_reg(ums_compelem_id compelem_id)
{
	struct ums_compelem *compelem = NULL;

	__get_from_compelem_id(compelem_id, &compelem);

	if (! compelem)
		return -EFAULT;

	get_ums_context(current, &compelem->entry_ctx);

	__register_compelem(compelem->complist, compelem);

	compelem->total_time += ktime_get_ns() - compelem->switch_time;
	compelem->host_id = COMPELEM_NO_HOST;

	return 0;
}

/**
 * @brief Execute a reserved completion element
 *
 * @todo Add a way to check compelem real owner!!
*/
int ums_compelem_exec(ums_compelem_id compelem_id,
		      ums_sched_id host_id)
{
	struct list_head *list_iter, *temp_head;

	struct ums_compelem *compelem = NULL;
	struct ums_complist *complist = NULL;

	__get_from_compelem_id(compelem_id, &compelem);

	if (! compelem) {
		return -EFAULT;
	}

	/* completion element must be reserved */
	if (! compelem->reserve_head)
		return -EFAULT;

	/* release the other reserved compelems */
	/* By construction this function can be accessed only by one at the 
	 * same time */
	list_for_each_safe(list_iter, temp_head, compelem->reserve_head) {
		struct ums_compelem *to_release;

		to_release = list_entry(list_iter, struct ums_compelem, 
					reserve_list);

		if (to_release != compelem) {
			__set_released(to_release);
			__register_compelem(complist, to_release);
		}

	}	

	/* remove the reservation list */
	kfree(compelem->reserve_head);
	compelem->reserve_head = NULL;

	put_ums_context(current, &compelem->entry_ctx);

	/* update proc stats data */
	compelem->n_switch++;
	compelem->host_id = host_id;
	compelem->switch_time = ktime_get_ns();

	return 0;
}

/**
 * @brief Find a compelem from the completion element hash table
 *
 * @param[in] id: identifier
 * @param[out] compelem: ref to the resulting compelem
 *
 * @return void
 * @note If no element was found set compelem to NULL
 * @todo Move into a private macro.
*/
void __get_from_compelem_id(ums_compelem_id id,
	       		    struct ums_compelem** compelem)
{
	*compelem = NULL;

	hash_for_each_possible(ums_compelem_hash, *compelem, list, id) {
		if ((*compelem)->id == id)
			break;
	}
}

/**
 * @brief Initialize ums_complist structure
 * 
 * Initialize lists, spin locks, semaphore and the proc directory entry.
 *
 * @return 0 if everything is OK, otherwise an error code
*/
static int new_complist(ums_complist_id comp_id,
				struct ums_complist *complist)
{
	int res;

	printk("calling: %s", __func__);
	complist->id = comp_id;


	res = kfifo_alloc(&complist->ready_queue, PAGE_SIZE, GFP_KERNEL);

	if (res) {
		goto new_complist_exit;
	}

	sema_init(&complist->elem_sem, 0);

	INIT_LIST_HEAD(&complist->compelems);
	INIT_LIST_HEAD(&complist->schedulers);

	spin_lock_init(&complist->ready_lock);
	spin_lock_init(&complist->schedulers_lock);
	spin_lock_init(&complist->compelems_lock);
	/* init proc directory */
	ums_proc_geniddir(complist->id, ums_complist_dir, &complist->proc_dir);

new_complist_exit:
	return res;
}

/**
 * @brief Delete the completion list data
 * 
 * Initialize lists, spin locks, semaphore and the proc directory entry.
 *
 * @note This function assumes that it is running in isolation and that no
 *	completion element is present in the list.
 * @return 0 if everything is OK, otherwise an error code
*/
static int delete_complist(struct ums_complist *complist)
{
	struct list_head *iter, *safeiter;

	kfifo_free(&complist->ready_queue);

	/* isolation is granted by already in use write_lock */
	list_for_each_safe(iter, safeiter, &complist->schedulers) {
		struct id_entry *sched_entry;

		sched_entry = list_entry(iter, struct id_entry, list);

		if (likely(sched_entry)) {
			ums_sched_remove(sched_entry->id);
			kfree(sched_entry);
		}
	}

	ums_proc_delete(complist->proc_dir);

	kfree(complist);

	return 0;
}

/**
 * @brief Initialize ums_compelem data structure
 *
 * @param[in] elem_id: new identifier for the compelem
 * @param[in] complist: completion list that owns the new element
 * @param[out] comp_elem: completion element initialized
 *
 * @return 0 if no error occurs, and error code otherwise
*/
static int new_compelement(ums_compelem_id elem_id,
			   struct ums_complist *complist,
			   struct ums_compelem *comp_elem)
{
	comp_elem->id = elem_id;
	comp_elem->elem_task = current;
	comp_elem->complist = complist;
	comp_elem->host_id = COMPELEM_NO_HOST;
	comp_elem->reserve_head = NULL;

	/* TODO: check this add instructions!!! */
	hash_add(ums_compelem_hash, &comp_elem->list, comp_elem->id);
	list_add(&comp_elem->complist_head, &complist->compelems);

	gen_ums_context(current, &comp_elem->entry_ctx);
	
	comp_elem->n_switch = 0;
	comp_elem->switch_time = 0;
	comp_elem->total_time = 0;
	/* procfs initialization */
	ums_proc_genidfile(comp_elem->id, complist->proc_dir, 
			   &ums_compelem_proc_ops, comp_elem, 
			   &comp_elem->proc_file);

	return 0;
}

/**
 * @brief proc_ops completion element read function
 *
 * The compelem proc read function is in charge in parsing the compelem data
 * into a readable char buffer to be readed in userspace.
 *
 * The information passed to the user are: 
 * - actual state of the completion element: running/idle
 * - actual total active time of the completion element (in nanoseconds)
 * - scheduler that is executing the completion element
 *
 *   
 * @return length copied to user, -EFAULT if an error occured
*/
static ssize_t compelem_proc_read(struct file *file,
				  char __user *ubuf, 
				  size_t count,
				  loff_t *ppos)
{
	struct ums_compelem *compelem;
        char buf[512];
        int len = 0;

        if (*ppos > 0)
                return 0;

	compelem = PDE_DATA(file_inode(file));

	if (! compelem)
		return -EFAULT;

	len += sprintf(buf, "state=%s\n", __str_state(compelem));

	if (len > count || len < 0)
		return -EFAULT;

	len += sprintf(buf + len, "act_time=%llu\n", __calc_time(compelem));

	if (len > count || len < 0)
		return -EFAULT;

	len += sprintf(buf + len, "runner=%d\n", compelem->host_id);

	if (len > count || len < 0)
		return -EFAULT;

        if (copy_to_user(ubuf, buf, len))
                return -EFAULT;

        *ppos = len;

        return len;
}

/**
 * @brief Try to reserve an ums_compelem from a ums_complist in a reserve list
 *
 * @param[in] complist: completion list that will retrieve the compelem
 * @param[out] compelem: ref to the compelem that will be returned to 
*/
static int reserve_compelem(struct ums_complist *complist,
			    struct ums_compelem **compelem,
			    struct list_head *reserve_head,
			    int do_sleep)
{
	*compelem = NULL;

	if (do_sleep) {
		int down_res = down_interruptible(&complist->elem_sem);


		if (down_res)
			return down_res;
	}	
	else {
		if (down_trylock(&complist->elem_sem))
			return 0;
	}

	if (! kfifo_out_spinlocked(&complist->ready_queue, compelem, 
				   sizeof(struct ums_compelem*),
				   &complist->ready_lock))
		return -EFAULT;

	__set_reserved(*compelem, reserve_head);

	return 0;
}
