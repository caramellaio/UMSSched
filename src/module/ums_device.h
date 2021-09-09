/**
 * @author Alberto Bombardelli
 *
 * @file ums_device.h
 *
 * @brief Public header of the module
 *
 * This header contains the requests and it is public also for user-space
 * programs
*/
#ifndef __UMS_DEVICE_H__
#define __UMS_DEVICE_H__

/**
 * @brief Request for registering an new scheduler
 *
 * The ioctl call creates a new scheduler (without worker threads) and
 * then return through the buffer its identifier. It requires as input
 * parameter the pointer to an existing completion list.
 *
 * @note assumes user buffer at list sizeof(int)
 *
 * @note Expect the same thread group id of the completion list
*/
#define UMS_REQUEST_ENTER_UMS_SCHEDULING 1

/**
 * @brief Register a new scheduler thread for a ums scheduler
 *
 * This ioctl call register the current thread as the scheduler thread for the
 * CPU i (current CPU).
 *
 * @warning This call assumes that the thread is register ONLY for one CPU
 *
 * @note Expect to get a direct value of the scheduler id (not the pointer to it)
*/
#define UMS_REQUEST_REGISTER_SCHEDULER_THREAD 3

/**
 * @brief Register a new completion list. 
 *
 * The ioctl call creates a new empty completion list and
 * then return through the buffer its identifier
 *
 * @note assume user buffer at list sizeof(int)
*/
#define UMS_REQUEST_NEW_COMPLETION_LIST 4

/**
 * @brief Register a new completion element using current 
 *
 * The ioctl call creates a new completion element to by attached to its complist
 * (specified in the buffer), then triggers copy_to_user to store the compelem
 * id (that will be used by a switched scheduler thread) and then block
 * until the deletion of the completion element is performed.
 *
 * @note assume user buffer at list sizeof(int)
 *
 * @note Freeze the calling thread until the delete is called
 *
 * @note Expect the same tgid of the completion list
*/
#define UMS_REQUEST_REGISTER_COMPLETION_ELEM 5

/**
 * @brief Remove a completion element freeing its original thread
 *
 * @note Can be called only by the current executor (i.e. it must be in the
 * codeflow of the completion list)
 *
 * @note pass the completion element directly as an integer value (not pointer)
*/
#define UMS_REQUEST_REMOVE_COMPLETION_ELEM 7

#define UMS_REQUEST_WAIT_UMS_SCHEDULER 8

/**
 * @brief Yields a work and return to the scheduler thread previous status
 *
 * @note no need for parameters
 *
 * @note this function DOES not restart entry_point function. If the scheduler
 * thread interrupted its execution at line x to switch to a worker, then
 * he will return to line x.
*/
#define UMS_REQUEST_YIELD 9

/**
 * @brief Execute a completion element
 * 
 * @note The thread must have already registered it
 *
 * @note pass directly the int value of the compelem to be executed
*/
#define UMS_REQUEST_EXEC 10

/**
 * @brief Dequeue a completion list with at-most n elements
 *
 * To trigger the dequeue the user must specify the number of elements that
 * he wants (in the buffer), obviously the memory that the request expect from
 * the user buffer is (n + 1) integers.
 *
 * A setup to prepare the call is the following:
 * @code
 * int num = 10;
 * int *buff;
 *
 * buff = malloc(sizeof(int) * (num + 1));
 *
 * if (buff)
 *	ioctl(fd, UMS_REQUEST_DEQUEUE_COMPLETION_LIST, buff);
 * @endcode
 *
 * The resulting array as the following structure
 *
 * len | el1 | el2 | el3 | el_{n-1} | el0
 *
 * the first element is at res+len.
 *
 *
 * This call has the al least one semantic, which means that len is always 
 * >= 1 (for obvious reasons also <= n)
 *
 * @warning Check the memory, not following the correct indication might cause
 *	several problems to your own system.
*/
#define UMS_REQUEST_DEQUEUE_COMPLETION_LIST 11

#endif /* __UMS_DEVICE_H__ */
