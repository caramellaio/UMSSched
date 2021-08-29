#ifndef __UMS_LINUX_H__
#define __UMS_LINUX_H__

#include "../module/ums_scheduler.h"
#include "../module/ums_complist.h"
typedef int (*ums_function)(int);

int EnterUmsSchedulingMode(ums_function entry_point,
                           ums_complist_id complist_id,
			   ums_sched_id *result);

int WaitUmsScheduler(ums_sched_id sched_id);

int WaitUmsChildren(void);

int CreateEmptyUmsCompletionList(ums_complist_id *id);

int CreateUmsCompletionList(ums_complist_id *id,
			    ums_function *list,
			    int list_count);

int CreateUmsCompletionElement(ums_complist_id id,
		               ums_function func);


int ExecuteUmsThread(ums_compelem_id next);

int UmsThreadYield(void);


int DequeueUmsCompletionListItems(int max_elements,
				  ums_compelem_id *result_array,
				  int *result_length);

int UnregisterCompletionElements(ums_compelem_id *elements,
				 int elem_count);

#endif /* __UMS_SCHED_LINUX_H__ */
