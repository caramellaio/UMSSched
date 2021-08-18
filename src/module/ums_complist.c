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

	/* TODO: as it is or as a pointer? */
	struct ums_complist_id list_ids;
};


static DEFINE_HASHTABLE(ums_complist_hash, UMS_COMPLIST_HASH_BITS);
static DEFINE_HASHTABLE(ums_compelem_hash, UMS_COMPELEM_HASH_BITS);

static atomic_t ums_complist_counter = ATOMIC_INIT(0);
static atomic_t ums_compelem_counter = ATOMIC_INIT(0);

static int new_complist(ums_complist_id comp_id,
			 struct ums_complist *complist);

int ums_complist_add(ums_complist_id *result)
{
	int res;
	struct ums_complist* ums_complist;

	*result = atomic_inc_return(&ums_complist_counter);

	ums_complist = (struct ums_complist*) kmalloc(sizeof(struct ums_complist),
						      GFP_KERNEL);

	if (! ums_complist) {
		kfree(ums_complist);	
		res =-1;
		goto exit_complist_add;
	}

	res = new_complist(*result, ums_complist);

exit_complist_add:
	return res;
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
