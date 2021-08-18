#include "ums_complist.h"
#include <linux/kfifo.h>
#include <linux/hashtable.h>
#include <linux/slab.h>

struct ums_complist {
	ums_complist_id comp_id;
	struct hlist_node list;
	struct kfifo *busy_queue;
	struct kfifo *ready_queue;
};

struct ums_complist_id {
	ums_complist_id id;
	struct list_head list;
};

struct ums_compelem {
	ums_compelem_id id;
	struct hlist_node list;
	
	/* list<ums_complist_id> */
	struct list_head complist_id_list;

	struct task_struct* elem_task;
};


static DEFINE_HASHTABLE(ums_complist_hash, UMS_COMPLIST_HASH_BITS);
static DEFINE_HASHTABLE(ums_compelem_hash, UMS_COMPELEM_HASH_BITS);

static atomic_t ums_complist_counter = ATOMIC_INIT(0);
static atomic_t ums_compelem_counter = ATOMIC_INIT(0);

static int new_complist(ums_complist_id comp_id,
			 struct ums_complist *complist);

static int new_compelement(ums_compelem_id elem_id,
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
	return 0;
}

int ums_complist_map(ums_complist_id list_id,
		     ums_compelem_id elem_id)
{
	return 0;
}

int ums_compelem_add(ums_compelem_id* result)
{
	struct ums_compelem *comp_elem;
	int res = 0;

	*result = atomic_inc_return(&ums_compelem_counter);

	comp_elem = (struct ums_compelem*) kmalloc(sizeof(struct ums_compelem),
						  GFP_KERNEL);

	if (! comp_elem) {
		return -1;
	}

	res = new_compelement(*result, comp_elem);

	return res;
}

int ums_compelem_remove(ums_compelem_id id)
{
	return 0;
}

int ums_complist_init(void)
{
	hash_init(ums_complist_hash);
	hash_init(ums_compelem_hash);

	/* TODO: check hash_init retval */
	return 0;
}

static int new_complist(ums_complist_id comp_id,
			struct ums_complist *complist)
{
	int res;

	complist->comp_id = comp_id;
	res = kfifo_alloc(complist->busy_queue, PAGE_SIZE, GFP_KERNEL);

	if (res)
		goto new_complist_exit;

	res = kfifo_alloc(complist->ready_queue, PAGE_SIZE, GFP_KERNEL);

	if (res) {
		kfifo_free(complist->busy_queue);
		goto new_complist_exit;
	}
	hash_add(ums_complist_hash, &complist->list, complist->comp_id);

new_complist_exit:
	return res;
}

static int new_compelement(ums_compelem_id elem_id,
			   struct ums_compelem *comp_elem)
{
	comp_elem->id = elem_id;

	INIT_LIST_HEAD(&comp_elem->complist_id_list);

	comp_elem->elem_task = current;

	hash_add(ums_compelem_hash, &comp_elem->list, comp_elem->id);
	/* TODO: set a correct return value */
	return 0;
}
