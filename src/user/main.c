#include "ums_api.h"
#include "../shared/ums_request.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int c = 0;

static int incr_c(int ums_sched);

static int decr_c(int ums_sched);

static int mult_c(int ums_sched);

static int entry_point(int ums_sched);

int main(void) {
	int err = 0;
	ums_sched_id sched_id;
	ums_complist_id complist_id;

	ums_function funcs[3] = {
		incr_c,
		decr_c,
		mult_c
	};

	CreateUmsCompletionList(&complist_id, funcs, 3);
	
	EnterUmsSchedulingMode(entry_point, complist_id, &sched_id);

	return err;
}

static int incr_c(int ums_sched)
{
	for(;;)
		c++;

	return c;
}

static int decr_c(int ums_sched)
{
	for(;;)
		c--;

	return c;
}

static int mult_c(int ums_sched)
{
	for(;;)
		c*=c;

	return c;
}

static int entry_point(int ums_sched)
{
	return 0;
}
