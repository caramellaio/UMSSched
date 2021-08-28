#include "ums_api.h"
#include "../shared/ums_request.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int c = 0;
static int index = 0;

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

	getchar();

	fprintf(stderr, "Final value of c: %d\n", c);

	return err;
}

static int incr_c(int ums_sched)
{
	fprintf(stderr, "I am completion element %d\n", ums_sched);
	fprintf(stderr, "incrementing c\n");
	c++;
	sleep(1);
	

	/* It is not sched! */
	UmsThreadYield(ums_sched);

	return c;
}

static int decr_c(int ums_sched)
{
	fprintf(stderr, "I am completion element %d\n", ums_sched);
	fprintf(stderr, "decrementing c\n");
	c--;
	sleep(1);

	UmsThreadYield(ums_sched);

	return c;
}

static int mult_c(int ums_sched)
{
	fprintf(stderr, "I am completion element %d\n", ums_sched);
	fprintf(stderr, "multiplying c\n");
	c*=c;
	sleep(1);

	UmsThreadYield(ums_sched);

	return c;
}

static int entry_point(int ums_sched)
{
	int res;
	fprintf(stderr, "entry_point: \n");
	index = index % 3;
	index++;
	fprintf(stderr, "Index: %d\n", index);
	if (ExecuteUmsThread(ums_sched, index++)) {
		fprintf(stderr, "exec failed!\n");
		res = -1;
	}

	UmsThreadYield(ums_sched);

	fprintf(stderr, "end entry_point: \n");
	index = index % 3;
	return res;
}
