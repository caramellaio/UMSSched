#include "ums_complist.h"
#include <linux/kfifo.h>
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/sched/task_stack.h>
#include <asm/processor.h>

struct ums_complist {
	ums_complist_id id;
	struct hlist_node list;
	struct kfifo busy_queue;
	struct kfifo ready_queue;
	struct semaphore elem_sem;
};

struct ums_compelem {
	ums_compelem_id id;
	ums_complist_id list_id;
	struct hlist_node list;
	
	/* list<ums_complist_id> */

	struct task_struct* elem_task;
};


static DEFINE_HASHTABLE(ums_complist_hash, UMS_COMPLIST_HASH_BITS);
static DEFINE_HASHTABLE(ums_compelem_hash, UMS_COMPELEM_HASH_BITS);

static atomic_t ums_complist_counter = ATOMIC_INIT(0);
static atomic_t ums_compelem_counter = ATOMIC_INIT(0);

static int new_complist(ums_complist_id comp_id,
			 struct ums_complist *complist);

static int new_compelement(ums_compelem_id elem_id,
			   ums_complist_id list_id,
			   struct ums_compelem *comp_elem);

static void get_from_compelem_id(ums_compelem_id id,
				struct ums_compelem** compelem);

static void get_from_complist_id(ums_complist_id id,
				struct ums_complist** complist);

static void register_compelem(struct ums_complist *complist,
			      struct ums_compelem *compelem);

static int reserve_compelem(struct ums_complist *complist,
			    struct ums_compelem **compelem,
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
	ums_compelem_id compelem_id;

	get_from_complist_id(id, &complist);

	if (! complist)
		return -1;

	hash_del(&complist->list);

	while (kfifo_out(&complist->ready_queue, &compelem_id, sizeof(compelem_id)))
		ums_compelem_remove(compelem_id);

	while (kfifo_out(&complist->busy_queue, &compelem_id, sizeof(compelem_id)))
		ums_compelem_remove(compelem_id);

	kfifo_free(&complist->busy_queue);
	kfifo_free(&complist->ready_queue);

	/* TODO: sema_free? */
	return 0;
}

int ums_compelem_add(ums_compelem_id* result,
		     ums_complist_id list_id)
{
	struct ums_compelem *compelem;
	struct ums_complist *complist;
	int res = 0;

	*result = atomic_inc_return(&ums_compelem_counter);

	get_from_complist_id(list_id, &complist);

	if (! complist) {
		printk(KERN_DEBUG "Complist %d not found!\n", list_id);
		return -1;
	}

	compelem = kmalloc(sizeof(struct ums_compelem), GFP_KERNEL);

	if (! compelem) 
		return -1;

	res = new_compelement(*result, list_id, compelem);

	register_compelem(complist, compelem);

	/* block completion element */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();

	return res;
}

int ums_compelem_remove(ums_compelem_id id)
{
	struct ums_compelem *compelem;

	get_from_compelem_id(id, &compelem);

	if (! compelem)
		return -1;

	hash_del(&compelem->list);

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

int ums_complist_exists(ums_complist_id comp_id)
{
	struct ums_complist *complist = NULL;

	get_from_complist_id(comp_id, &complist);

	return NULL != complist;
}

int ums_complist_reserve(ums_complist_id comp_id,
			 int to_reserve,
			 ums_compelem_id *ret_array,
			 int *size)
{
	int i;
	struct ums_complist *complist;
	struct ums_compelem *compelem_0;

	*size = 0;

	get_from_complist_id(comp_id, &complist);

	if (! complist)
		return -1;

	if (to_reserve == 0)
		return 0;

	if (unlikely(reserve_compelem(complist, &compelem_0, 1)))
		return -2;

	if (unlikely(! compelem_0))
		return -2;

	ret_array[0] = compelem_0->id;

	for (i = 1; i < to_reserve; i++) {
		struct ums_compelem *compelem_i = NULL;

		if (unlikely(reserve_compelem(complist, &compelem_i, 0)))
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
	struct ums_complist *complist = NULL;

	get_from_compelem_id(compelem_id, &compelem);

	if (! compelem)
		return -1;

	memcpy(compelem->elem_task, current_pt_regs(), sizeof(struct pt_regs));
	get_from_complist_id(compelem->list_id, &complist);

	if (unlikely(!complist)) {
		printk(KERN_ERR "compelem data structure contains non existing complist!\n");
		return -2;
	}

	register_compelem(complist, compelem);

	return 0;
}

int ums_compelem_exec(ums_compelem_id compelem_id)
{
	int i;
	ums_complist_id list_id;
	struct ums_compelem *compelem = NULL;
	struct ums_complist *complist = NULL;

	printk("Entering %s", __func__);
	get_from_compelem_id(compelem_id, &compelem);

	if (! compelem) {
		printk(KERN_ERR "Compelem not found");
		return -1;
	}

	list_id = compelem->list_id;

	get_from_complist_id(list_id, &complist);

	if (! complist) {
		printk(KERN_ERR "Complist not found");
		return -2;
	}
	
#if 0
	for (i = 0; i < reserved_count; i++) {
		struct ums_compelem *curr = NULL;
		ums_compelem_id res_i = reserved_list[i];

		get_from_compelem_id(res_i, &curr);

		if (! curr)
			return -1;

		if (unlikely(curr->list_id != list_id))
			return -2;

		if (res_i != compelem_id)
			register_compelem(complist, curr);
	}
#endif

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
	res = kfifo_alloc(&complist->busy_queue, PAGE_SIZE, GFP_KERNEL);

	printk("busy alloced!");

	if (res)
		goto new_complist_exit;

	res = kfifo_alloc(&complist->ready_queue, PAGE_SIZE, GFP_KERNEL);

	printk("ready alloced!");
	if (res) {
		kfifo_free(&complist->busy_queue);
		goto new_complist_exit;
	}

	sema_init(&complist->elem_sem, 0);

	hash_add(ums_complist_hash, &complist->list, complist->id);

new_complist_exit:
	return res;
}

static int new_compelement(ums_compelem_id elem_id,
			   ums_complist_id list_id,
			   struct ums_compelem *comp_elem)
{
	comp_elem->id = elem_id;

	comp_elem->elem_task = current;

	comp_elem->list_id = list_id;

	hash_add(ums_compelem_hash, &comp_elem->list, comp_elem->id);

	return 0;
}

static void get_from_complist_id(ums_complist_id id,
				struct ums_complist** complist)
{
	*complist = NULL;

	hash_for_each_possible(ums_complist_hash, *complist, list, id) {
		if ((*complist)->id == id)
			break;
	}
}

static void get_from_compelem_id(ums_compelem_id id,
				struct ums_compelem** compelem)
{
	*compelem = NULL;

	hash_for_each_possible(ums_compelem_hash, *compelem, list, id) {
		if ((*compelem)->id == id)
			break;
	}
}

/* TODO: use macro instead */
static void register_compelem(struct ums_complist *complist,
			      struct ums_compelem *compelem)
{
	/* check if lock is necessary for kfifo */
	kfifo_in(&complist->ready_queue, &compelem, sizeof(compelem));

	/* TODO: check if it is necessary to block interrupts */
	up(&complist->elem_sem);
}

static int reserve_compelem(struct ums_complist *complist,
			    struct ums_compelem **compelem,
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

	return 0;
}
