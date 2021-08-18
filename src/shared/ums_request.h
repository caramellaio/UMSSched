#ifndef __UMS_REQUEST_H__
#define __UMS_REQUEST_H__

#include "../module/ums_scheduler.h"
#define UMS_REQUEST_ENTER_UMS_SCHEDULING 0

#define UMS_REQUEST_REGISTER_SCHEDULER_THREAD 1

#define UMS_REQUEST_NEW_COMPLETION_LIST 2

#define UMS_REQUEST_REGISTER_COMPLETION_ELEM 3

#define UMS_REQUEST_REMOVE_COMPLETION_LIST 4

#define UMS_REQUEST_REMOVE_COMPLETION_ELEM 5

#define UMS_REQUEST_REGISTER_ENTRY_POINT 6
struct reg_sched_thread_msg {
	ums_sched_id sched_id;
	int cpu;
};

#endif /* __UMS_REQUEST_H__ */
