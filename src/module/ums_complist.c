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

#define COMPELEM_NO_HOST 0
#define __set_reserved(elem, list)				\
	do {							\
		(elem)->reserve_head = list;			\
		list_add(&(elem)->reserve_list, list);		\
	 } while (0)

#define __set_released(elem)					\
	do {							\
		list_del(&(elem)->reserve_list);		\
		(elem)->reserve_head = NULL;			\
	} while (0)

#define __register_compelem(complist, compelem)			\
	do {							\
		/* check if lock is necessary for kfifo */	\
		kfifo_in(&(complist)->ready_queue,		\
			 &(compelem), sizeof(compelem));	\
		/* TODO: check if it is necessary		\
		 * to block interrupts */			\
		up(&complist->elem_sem);			\
	} while (0)

#define COMPELEM_FILE_MODE 0444
#define COMPLIST_DIR_NAME "completion_lists"

static DEFINE_HASHRWLOCK(ums_complist_hash, UMS_COMPLIST_HASH_BITS);

/* Completion element is always own by a single element (worker),
 * For that case we do not need rwlock */
static DEFINE_HASHTABLE(ums_compelem_hash, UMS_COMPELEM_HASH_BITS);

static atomic_t ums_complist_counter = ATOMIC_INIT(0);
static atomic_t ums_compelem_counter = ATOMIC_INIT(0);

/* procfs */
static struct proc_dir_entry *ums_complist_dir = NULL;

static ssize_t compelem_proc_read(struct file *file,
				  char __user *ubuf, 
				  size_t count,
				  loff_t *ppos);

static struct proc_ops ums_compelem_proc_ops =
{
	.proc_read = compelem_proc_read,
};

/* end procfs */


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

int ums_complist_add_scheduler(ums_complist_id id, 
			       struct list_head *sched_list)
{
	int res;
	int iter;
	struct id_rwlock *lock;
	struct ums_complist *complist;

	hashrwlock_find(ums_complist_hash, id, &lock);

	if (! lock)
		return -1;

	if (! lock->data)
		return -1;

	complist = lock->data;

	/* TODO: is this safe? */
	id_read_trylock_region(lock, iter, res) {
		/* TODO: add list spinlock! */
		list_add(sched_list, &complist->schedulers);
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

	return 0;
}

int ums_compelem_add(ums_compelem_id* result,
		     ums_complist_id list_id)
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

	/* block completion element */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();

	return res;
}

int ums_compelem_remove(ums_compelem_id id)
{
	struct ums_compelem *compelem;

	__get_from_compelem_id(id, &compelem);

	if (! compelem)
		return -1;

	hash_del(&compelem->list);

	if (compelem->reserve_head) {
		list_del(&compelem->reserve_list);
		if (list_empty(compelem->reserve_head))
			kfree(compelem->reserve_head);
	}

	/* remove from the list, if list is empty delete complist! */
	list_del(&compelem->complist_head);

	ums_proc_delete(compelem->proc_file);

	if (list_empty(&compelem->complist->compelems))
		delete_complist(compelem->complist);

	kfree(compelem);

	return 0;
}

int ums_complist_init(void)
{
	hash_init(ums_complist_hash);
	hash_init(ums_compelem_hash);

	return 0;
}

int ums_complist_proc_init(struct proc_dir_entry *ums_dir)
{
	ums_complist_dir = proc_mkdir(COMPLIST_DIR_NAME, ums_dir);

	return !ums_complist_dir;
}

void ums_complist_deinit(void)
{
	// TODO: implement
}

void ums_complist_proc_deinit(void)
{
	ums_proc_delete(ums_complist_dir);
	ums_complist_dir = NULL;
}

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

int ums_compelem_store_reg(ums_compelem_id compelem_id)
{
	struct ums_compelem *compelem = NULL;

	__get_from_compelem_id(compelem_id, &compelem);

	if (! compelem)
		return -1;

	get_ums_context(current, &compelem->entry_ctx);

	__register_compelem(compelem->complist, compelem);

	compelem->host_id = COMPELEM_NO_HOST;

	return 0;
}

int ums_compelem_exec(ums_compelem_id compelem_id)
{
	struct list_head *list_iter, *temp_head;

	struct ums_compelem *compelem = NULL;
	struct ums_complist *complist = NULL;

	printk("Entering %s", __func__);
	__get_from_compelem_id(compelem_id, &compelem);

	if (! compelem) {
		printk(KERN_ERR "Compelem not found");
		return -1;
	}

	/* completion element must be reserved */
	if (! compelem->reserve_head)
		return -1;

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


	kfree(compelem->reserve_head);

	/* Add set running macro */
	compelem->reserve_head = NULL;

	printk("calling execution context switch from %d:\n", current->pid);
	__dump_pt_regs(task_pt_regs(current));

	put_ums_context(current, &compelem->entry_ctx);
	printk("%s: setted pt_regs!\n", __func__);

	printk("Exit %s", __func__);
	/* exec is called from already reserved compelems */
	return 0;
}


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

	/* init proc directory */
	ums_proc_geniddir(complist->id, ums_complist_dir, &complist->proc_dir);

new_complist_exit:
	return res;
}

static int delete_complist(struct ums_complist *complist)
{
	struct list_head *iter, *safeiter;

	kfifo_free(&complist->ready_queue);

	/* TODO: we are assuming that complist is empty: ensure that */

	/* isolation is granted by already in use write_lock */
	list_for_each_safe(iter, safeiter, &complist->schedulers) {
		struct ums_scheduler *sched;

		sched = list_entry(iter, struct ums_scheduler, complist_head);

		if (likely(sched))
			ums_sched_remove(sched->id);
	}

	ums_proc_delete(complist->proc_dir);

	kfree(complist);

	return 0;
}

static int new_compelement(ums_compelem_id elem_id,
			   struct ums_complist *complist,
			   struct ums_compelem *comp_elem)
{
	char file_name[32];

	comp_elem->id = elem_id;
	comp_elem->elem_task = current;
	comp_elem->complist = complist;
	comp_elem->host_id = COMPELEM_NO_HOST;
	comp_elem->reserve_head = NULL;

	hash_add(ums_compelem_hash, &comp_elem->list, comp_elem->id);
	list_add(&comp_elem->complist_head, &complist->compelems);

	gen_ums_context(current, &comp_elem->entry_ctx);
	
	printk(KERN_DEBUG "ums_compelem %d context:\n", elem_id);

	dump_pt_regs(comp_elem->entry_ctx);

	sprintf(file_name, "%d", comp_elem->id);

	/* procfs initialization */
	ums_proc_genidfile(comp_elem->id, complist->proc_dir, 
			   &ums_compelem_proc_ops, comp_elem, 
			   &comp_elem->proc_file);

	return 0;
}

static ssize_t compelem_proc_read(struct file *file,
				  char __user *ubuf, 
				  size_t count,
				  loff_t *ppos)
{
        char buf[1024];
        int len = 0;

        if (*ppos > 0)
                return 0;

	/* TODO: this is temporary! */
        len += sprintf(buf, "this is a test proc file, pid=%d\n", current->pid);

	if (len > count)
		return -EFAULT;

        if (copy_to_user(ubuf, buf, len))
                return -EFAULT;

        *ppos = len;

        return len;
}

void __get_from_compelem_id(ums_compelem_id id,
	       		    struct ums_compelem** compelem)
{
	*compelem = NULL;

	hash_for_each_possible(ums_compelem_hash, *compelem, list, id) {
		if ((*compelem)->id == id)
			break;
	}
}

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

	/* TODO: use lock */
	/* TODO: assert result */
	if (! kfifo_out(&complist->ready_queue, compelem, 
			sizeof(struct ums_compelem*)))
		return -2;

	__set_reserved(*compelem, reserve_head);

	return 0;
}
