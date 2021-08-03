#include "ums_api.h"
#include <stddef.h>


int EnterUmsSchedulingMode(struct ums_entry_point entry_point,
                           struct ums_completion_list comp_list)
{
  return 0;
}

void ExecuteUmsThread(struct ums_scheduler* scheduler,
                      struct ums_worker* new_worker)
{
  return;
}

void UmSThreadYield(struct ums_scheduler* scheduler,
                    struct ums_worker* act_worker)
{
  return;
}


struct ums_worker* DequeueUmsCompletionListItems(struct ums_scheduler* scheduler)
{
  return NULL;
}
