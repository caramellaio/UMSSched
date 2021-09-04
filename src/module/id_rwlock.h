#ifndef __ID_RWLOCK_H__
#define __ID_RWLOCK_H__

#include <linux/hashtable.h>
#include <linux/rwlock.h>

struct id_rwlock {
	int id;
	struct hlist_node list;
	rwlock_t lock;
	void *data;
};

#define DEFINE_HASHRWLOCK(name, bits)					\
	(DEFINE_HASHTABLE(name, bits))

#define init_hashrwlock(table)						\
	(hash_init(table))

#define add_id_rwlock(hash_lock, lock)					\
	(hash_add_rcu((hash_lock), (lock)->list, (lock)->id))

#define find_id_rwlock(hash_lock, id, lock_ref)				\
	do {								\
		*(lock_ref) = NULL;					\
		hash_for_each_possible_rcu((hash_lock), *(lock_ref),	\
					   list, id) {			\
			if ((*(lock_ref))->id == id) break;		\
		}							\
	} while (0)

#define remove_id_rwlock(lock_ref)					\
	(hash_del_rcu((lock_ref)->list))

#define init_id_rwlock(id, data, lock)					\
	do {								\
		(lock)->id = id;					\
		(lock)->data = data;					\
		rwlock_init(&(lock)->lock;				\
	} while (0)

#define id_rwlock_read(lock)						\
	(read_lock((lock)->lock))

#define id_rwlock_tryread(lock)						\
	(read_trylock((lock)->lock))

#define id_rwlock_write(lock)						\
	(write_lock((lock)->lock))

#define id_rwlock_trywrite(lock)					\
	(write_trylock((lock)->lock))

#endif /* __ID_RWLOCK_H__ */
