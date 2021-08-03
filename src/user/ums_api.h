#ifndef __UMS_LINUX_H__
#define __UMS_LINUX_H__

#include "../shared/ums_structs.h"

int EnterUmsSchedulingMode(struct ums_entry_point entry_point,
                           struct ums_completion_list comp_list);

void ExecuteUmsThread(struct ums_scheduler* scheduler,
                      struct ums_worker* new_worker);

void UmSThreadYield(struct ums_scheduler* scheduler,
                    struct ums_worker* act_worker);

struct ums_worker* DequeueUmsCompletionListItems(struct ums_scheduler* scheduler);
#endif /* __UMS_SCHED_LINUX_H__ */
