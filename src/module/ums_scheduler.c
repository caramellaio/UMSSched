#include "ums_scheduler.h"

int ums_sched_new(comp_list_id comp_list_id, ums_sched_id* identifier)
{
#if 0
	if (! ums_comp_list_get(comp_list_id))
		return ERROR_MISSING_COMPLIST;

	*identifier = atomic_inc_return(ums_sched_global_counter);

#endif
	return 0;
}
