#include "ums_complist.h"
#include "ums_complist_internal.h"
/* Only for delete part */
#include "ums_scheduler_internal.h"

#include "ums_scheduler.h"

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


static DEFINE_HASHTABLE(ums_complist_hash, UMS_COMPLIST_HASH_BITS);
static DEFINE_HASHTABLE(ums_compelem_hash, UMS_COMPELEM_HASH_BITS);

static atomic_t ums_complist_counter = ATOMIC_INIT(0);
static atomic_t ums_compelem_counter = ATOMIC_INIT(0);

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

	return res;
}

int ums_complist_remove(ums_complist_id id)
{
	struct ums_complist *complist;
	__get_from_complist_id(id, &complist);

	if (! complist)
		return -1;

	delete_complist(complist);

	return 0;
}

int ums_compelem_add(ums_compelem_id* result,
		     ums_complist_id list_id)
{
	struct ums_compelem *compelem;
	struct ums_complist *complist;
	int res = 0;

	*result = atomic_inc_return(&ums_compelem_counter);

	__get_from_complist_id(list_id, &complist);

	if (! complist) {
		printk(KERN_DEBUG "Complist %d not found!\n", list_id);
		return -1;
	}

	compelem = kmalloc(sizeof(struct ums_compelem), GFP_KERNEL);

	if (! compelem) 
		return -1;

	res = new_compelement(*result, complist, compelem);

	__register_compelem(complist, compelem);

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

	if (list_empty(&compelem->complist->compelems))
		delete_complist(compelem->complist);

	kfree(compelem);

	return 0;
}

int ums_complist_init(void)
{
	hash_init(ums_complist_hash);
	hash_init(ums_compelem_hash);

	/* TODO: check hash_init retval */
	return 0;
}

int ums_complist_reserve(ums_complist_id comp_id,
			 int to_reserve,
			 ums_compelem_id *ret_array,
			 int *size)
{
	int i;
	struct ums_complist *complist;
	struct ums_compelem *compelem_0;
	struct list_head *reserve_list;

	*size = 0;

	__get_from_complist_id(comp_id, &complist);

	/* reserve list is destroyed in Execute function by the choosen compelem */
	reserve_list = kmalloc(sizeof(struct list_head), GFP_KERNEL);

	if (unlikely(! reserve_list))
		return -2;

	INIT_LIST_HEAD(reserve_list);

	if (! complist)
		return -1;

	if (to_reserve == 0)
		return 0;

	if (unlikely(reserve_compelem(complist, &compelem_0, reserve_list, 1)))
		return -2;

	if (unlikely(! compelem_0))
		return -2;

	ret_array[0] = compelem_0->id;

	for (i = 1; i < to_reserve; i++) {
		struct ums_compelem *compelem_i = NULL;

		if (unlikely(reserve_compelem(complist, &compelem_i,
					      reserve_list, 0)))
			return -2;

		if (! compelem_i)
			break;
		ret_array[i] = compelem_i->id;
	}

	*size = i;

	return 0;
}

int ums_compelem_store_reg(ums_compelem_id compelem_id)
{
	struct ums_compelem *compelem = NULL;

	/* TODO: reset host_id */
	__get_from_compelem_id(compelem_id, &compelem);

	if (! compelem)
		return -1;

	memcpy(compelem->elem_task, current_pt_regs(), sizeof(struct pt_regs));

	__register_compelem(compelem->complist, compelem);

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
	list_for_each_safe(list_iter, temp_head, compelem->reserve_head) {
		struct ums_compelem *to_release;

		to_release = list_entry(list_iter, struct ums_compelem, 
					reserve_list);

		if (to_release != compelem) {
			__set_released(to_release);
			__register_compelem(complist, to_release);
		}

	}	


	printk("Freed stuff\n");

	kfree(compelem->reserve_head);

	/* Add set running macro */
	compelem->reserve_head = NULL;
	// compelem->

	memcpy(current_pt_regs(), task_pt_regs(compelem->elem_task), sizeof(struct pt_regs));
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

	hash_add(ums_complist_hash, &complist->list, complist->id);

new_complist_exit:
	return res;
}

static int delete_complist(struct ums_complist *complist)
{
	struct list_head *iter, *safeiter;
	/* TODO: writelock() */

	hash_del(&complist->list);

	kfifo_free(&complist->ready_queue);

	list_for_each_safe(iter, safeiter, &complist->schedulers) {
		struct ums_scheduler *sched;

		sched = list_entry(iter, struct ums_scheduler, complist_head);

		if (likely(sched))
			/* TODO: use macro */
			ums_sched_remove(sched->id);
	}

	kfree(complist);

	return 0;
}

static int new_compelement(ums_compelem_id elem_id,
			   struct ums_complist *complist,
			   struct ums_compelem *comp_elem)
{
	comp_elem->id = elem_id;
	comp_elem->elem_task = current;
	comp_elem->complist = complist;
	comp_elem->host_id = COMPELEM_NO_HOST;
	comp_elem->reserve_head = NULL;

	hash_add(ums_compelem_hash, &comp_elem->list, comp_elem->id);
	list_add(&comp_elem->complist_head, &complist->compelems);

	return 0;
}

void __get_from_complist_id(ums_complist_id id,
	      		    struct ums_complist** complist)
{
	*complist = NULL;

	hash_for_each_possible(ums_complist_hash, *complist, list, id) {
		if ((*complist)->id == id)
			break;
	}
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
