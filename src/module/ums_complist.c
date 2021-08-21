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

static void complist_register_compelem(struct ums_complist *complist,
				       struct ums_compelem *compelem);

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

	complist_register_compelem(complist, compelem);
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

int ums_compelem_store_reg(ums_compelem_id compelem_id)
{
	struct ums_compelem *compelem = NULL;

	get_from_compelem_id(compelem_id, &compelem);

	if (! compelem)
		return -1;

	*task_pt_regs(compelem->elem_task) = *current_pt_regs();

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

static void complist_register_compelem(struct ums_complist *complist,
				       struct ums_compelem *compelem)
{
	kfifo_in(&complist->ready_queue, &compelem, sizeof(compelem));
}
