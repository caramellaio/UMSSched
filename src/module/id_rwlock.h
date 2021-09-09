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
 * @return None: do/while macro
*/
#define id_rwlock_init(_id, _data, _lock)				\
	do {								\
		(_lock)->id = _id;					\
		(_lock)->data = _data;					\
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

/**
 * @brief read trylock region (for loop)
 *
 * @param[in] lock: id_rwlock used
 * @param[in] iter: integer iterator
 * @param[out] res: result of trylock
 * This macro provides a read-safe zone using id_read_trylock and
 * id_read_unlock.
 *
 * @sa id_read_lock
 * @sa id_read_unlock
 * @sa id_write_lock
 * @sa id_write_unlock
*/
#define id_read_trylock_region(lock, iter, res)				\
	for (iter = 0, res = id_read_trylock(lock);			\
	     res && iter < 1;						\
	     iter++, id_read_unlock(lock))

/**
 * @brief Write lock region (for loop)
 *
 * @param[in] lock: id_rwlock used
 * @param[in] iter: integer iterator
 *
 * This macro provides a read-safe zone using id_read_lock and 
 * id_read_unlock
 *
 * @sa id_read_lock
 * @sa id_read_unlock
 * @sa id_write_lock
 * @sa id_write_unlock
*/
#define id_read_lock_region(lock, iter)					\
	for (iter = 0, id_read_lock(lock);				\
	     iter < 1;							\
	     iter++, id_read_unlock(lock))	

/**
 * @brief Write lock region (for loop)
 *
 * @param[in] lock: id_rwlock used
 * @param[in] iter: integer iterator
 * @param[out] res: result of trylock
 * This macro provides a write-safe zone using id_write_trylock and
 * id_write_unlock
 *
 * @sa id_read_lock
 * @sa id_read_unlock
 * @sa id_write_lock
 * @sa id_write_unlock
*/
#define id_write_trylock_region(lock, iter, res)			\
	for (iter = 0, res = id_write_trylock(lock);			\
	     res && iter < 1;						\
	     iter++, id_write_unlock(lock))

/**
 * @brief Write lock region (for loop)
 *
 * @param[in] lock: id_rwlock used
 * @param[in] iter: integer iterator
 *
 * This macro provides a write-safe zone using id_write_lock and
 * id_write_unlock
 *
 * @sa id_read_lock
 * @sa id_read_unlock
 * @sa id_write_lock
 * @sa id_write_unlock
*/
#define id_write_lock_region(lock, iter)				\
	for (iter = 0, id_write_lock(lock);				\
	     iter < 1;							\
	     iter++, id_write_unlock(lock))

#endif /* __ID_RWLOCK_H__ */
