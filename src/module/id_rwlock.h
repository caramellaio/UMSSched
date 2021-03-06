/**
 * @author Alberto Bombardelli
 *
 * @file id_rwlock.h
 *
 * @brief File containing the implementation of the id_rwlock and hashrwlock mechanisms
 *
 * The functions are quite straightforward, this module just combines the 
 * hashtables with rwlocks. Refer to them for a further documentation
 *
*/
#ifndef __ID_RWLOCK_H__
#define __ID_RWLOCK_H__

#include <linux/hashtable.h>
#include <linux/rwlock.h>
#include <linux/list.h>

static struct list_head id_rwlock_reclaim_list;

/**
 * @struct id_rwlock
 *
 * @brief Identifier read/write lock
 *
 * This struct is a read/write lock applied to hash table and storing internal
 * data that should be accessed through rwlock.
*/
struct id_rwlock {
	/** 
	 * @brief Identifier for the hashrwlock
	 *
	 * This is the identifier used to get the rwlock from the hashrwlock
	*/
	int id;

	/**
	 * @brief list node used by hashrwlock
	*/
	struct hlist_node list;

	struct list_head rec_list;

	/**
	 * @brief The real lock used to access to data
	 *
	*/
	rwlock_t lock;

	/** 
	 * @brief Data that must be protected by lock
	*/
	void *data;
};

/**
 * @brief Macro to define the hashrwlock
 *
 * @param[in] name: name for the variable
 * @param[in] bits: number of bits used for buckets
 *
 * just a call to DEFINE_HASHTABLE
*/
#define DEFINE_HASHRWLOCK(name, bits)					\
	DEFINE_HASHTABLE(name, bits)

/**
 * @brief Macro to initialize the hashrwlock
 *
 * @param[in, out] table: table to be initialized
 *
 * just a call to hash_init
*/
#define hashrwlock_init(table)						\
	(hash_init(table))

/**
 * @brief Add a new lock to the lock hash
 *
 * @param[in] hash_lock: hashtable in which the lock gen added
 * @param[in] lock: lock to add to the hashtable with id as key
 *
 * just a call to hash_add_rcu
*/
#define hashrwlock_add(hash_lock, lock)					\
	(hash_add_rcu((hash_lock), &(lock)->list, (lock)->id))

/**
 * @brief Find a lock using his id
 *
 * @param[in] hash_lock: hashrwlock
 * @param[in] _id: identifier to find the lock
 * @param[out] lock_ref: lock to be found, setted to NULL if none is found
 *
 * @return no return (do while macro)
*/
#define hashrwlock_find(hash_lock, _id, lock_ref)			\
	do {								\
		*(lock_ref) = NULL;					\
		hash_for_each_possible_rcu((hash_lock), *(lock_ref),	\
					   list, _id) {			\
			if ((*(lock_ref))->id == _id) break;		\
		}							\
	} while (0)

/**
 * @brief Remove a id_rwlock from and hashrwlock
 *
 * @param[in] lock_ref: lock to be removed
 *
 * uses RCU hash
 * @return no return
*/
#define hashrwlock_remove(lock_ref)					\
	(hash_del_rcu(&(lock_ref)->list))

/**
 * @brief Initialize id_rwlock structure
 *
 * @param[in] _id: new id
 * @param[in] _data: pointer linked to the lock
 * @param[out] _lock: out id_rwlock res
 *
 * Add also it to the reclaim list
 *
 * @return None: do/while macro
*/
#define id_rwlock_init(_id, _data, _lock)				\
	do {								\
		(_lock)->id = _id;					\
		(_lock)->data = _data;					\
		list_add(&(_lock)->rec_list, &id_rwlock_reclaim_list);	\
		rwlock_init(&(_lock)->lock);				\
	} while (0)

/**
 * @brief call read_lock on this lock
 *
 * @param[in] lock: lock to be locked
*/
#define id_read_lock(lock)						\
	(read_lock(&(lock)->lock))

/**
 * @brief call read_trylock on this lock
 *
 * @param[in] lock: lock to be locked
 *
 * @return see read_trylock
*/
#define id_read_trylock(lock)						\
	(read_trylock(&(lock)->lock))

/**
 * @brief call read_unlock on this lock
 *
 * @param[in] lock: lock to be unlocked
 *
 * @return see read_unlock
*/
#define id_read_unlock(lock)						\
	(read_unlock(&(lock)->lock))

/**
 * @brief call write_lock on this lock
 *
 * @param[in] lock: lock to be locked
*/
#define id_write_lock(lock)						\
	(write_lock(&(lock)->lock))

/**
 * @brief call write_trylock on this lock
 *
 * @param[in] lock: lock to be locked
 *
 * @return see write_trylock
*/
#define id_write_trylock(lock)						\
	(write_trylock(&(lock)->lock))

/**
 * @brief call write_unlock on this lock
 *
 * @param[in] lock: lock to be unlocked
 *
 * @return see write_unlock
*/
#define id_write_unlock(lock)						\
	(write_unlock(&(lock)->lock))


#define id_rwlock_init_mod()						\
	(INIT_LIST_HEAD(&id_rwlock_reclaim_list))

#define id_rwlock_deinit_mod(iter, safe_iter, tmp_rwlock, deinit_data)	\
	do {								\
		list_for_each_safe((iter), (safe_iter),			\
				   &id_rwlock_reclaim_list) {		\
			tmp_rwlock = list_entry(iter,			\
					        struct id_rwlock,	\
						rec_list);		\
			if (tmp_rwlock->data)				\
				deinit_data(tmp_rwlock->data);		\
			kfree(tmp_rwlock);				\
		}							\
	} while (0)
#endif /* __ID_RWLOCK_H__ */
