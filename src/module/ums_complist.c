#include "ums_complist.h"

struct ums_complist {
	ums_complist_id comp_id;
	struct kfifo *busy_queue;
	struct kfifo *ready_queue;
};

static DEFINE_HASHTABLE(ums_complist_hash, UMS_COMPLIST_HASH_BITS);
static DEFINE_HASHTABLE(ums_compelem_hash, UMS_COMPELEM_HASH_BITS);

static atomic_t ums_complist_counter = ATOMIC_INIT(0);
static atomic_t ums_compelem_counter = ATOMIC_INIT(0);

static void new_complist(ums_complist_id comp_id,
			 struct ums_complist *complist);

int ums_complist_add(ums_complist_id *result)
{
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
}

static int new_complist(ums_complist_id comp_id,
			struct ums_complist *complist)
{
	int res;

	complist->comp_id = comp_id;
	res = kfifo_alloc(complist->busy_queue, PAGE_SIZE, GFP_KERNEL);

	if (! res)
		kfifo_alloc(complist->ready_queue, PAGE_SIZE, GFP_KERNEL);

	return res;
}
