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
	DEFINE_HASHTABLE(name, bits)

#define hashrwlock_init(table)						\
	(hash_init(table))

#define hashrwlock_add(hash_lock, lock)					\
	(hash_add_rcu((hash_lock), &(lock)->list, (lock)->id))

#define hashrwlock_find(hash_lock, _id, lock_ref)			\
	do {								\
		*(lock_ref) = NULL;					\
		hash_for_each_possible_rcu((hash_lock), *(lock_ref),	\
					   list, _id) {			\
			if ((*(lock_ref))->id == _id) break;		\
		}							\
	} while (0)

#define hashrwlock_remove(lock_ref)					\
	(hash_del_rcu(&(lock_ref)->list))

#define id_rwlock_init(_id, _data, _lock)				\
	do {								\
		(_lock)->id = _id;					\
		(_lock)->data = _data;					\
		rwlock_init(&(_lock)->lock);				\
	} while (0)

#define id_read_lock(lock)						\
	(read_lock(&(lock)->lock))

#define id_read_trylock(lock)						\
	(read_trylock(&(lock)->lock))

#define id_read_unlock(lock)						\
	(read_unlock(&(lock)->lock))

#define id_write_lock(lock)						\
	(write_lock(&(lock)->lock))

#define id_write_trylock(lock)						\
	(write_trylock(&(lock)->lock))

#define id_write_unlock(lock)						\
	(write_unlock(&(lock)->lock))

#define id_read_trylock_region(lock, iter, res)				\
	for (iter = 0, res = id_read_trylock(lock);			\
	     res && iter < 1;						\
	     iter++, id_read_unlock(lock))

#define id_read_lock_region(lock, iter)					\
	for (iter = 0, id_read_lock(lock);				\
	     iter < 1;							\
	     iter++, id_read_unlock(lock))	

#define id_write_trylock_region(lock, iter, res)			\
	for (iter = 0, res = id_write_trylock(lock);			\
	     res && iter < 1;						\
	     iter++, id_write_unlock(lock))

#define id_write_lock_region(lock, iter)				\
	for (iter = 0, id_write_lock(lock);				\
	     iter < 1;							\
	     iter++, id_write_unlock(lock))

#endif /* __ID_RWLOCK_H__ */
