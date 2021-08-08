#include "ums_api.h"
#include <stddef.h>
#include <pthread.h>
#include <stdio.h>


static void* do_gen_ums_sched(void *args);

int EnterUmsSchedulingMode(struct ums_entry_point entry_point,
                           struct ums_completion_list comp_list)
{
  pthread_t ums_pthread;

  int err = 0;

  err = pthread_create(&ums_pthread, NULL, &do_gen_ums_sched, NULL);

  if (err) {
    fprintf(stderr, "Error: cannot create User Mode Scheduler thread!\n");
  }

  return err;
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

static void* do_gen_ums_sched(void *args)
{
  printf("Executing function: %s\n", __func__);

  return 0;
}
