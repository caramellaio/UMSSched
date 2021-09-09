#include "ums_api.h"
#include "../module/ums_device.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SLEEP_TIME 10
static int c = 0;
static int index = 0;

static int incr_c(int ums_sched);

static int decr_c(int ums_sched);

static int mult_c(int ums_sched);

static int entry_point(int ums_sched);

int main(void) {
	int err = 0;
	ums_sched_id sched_id;
	ums_sched_id sched_id2;
	ums_complist_id complist_id;

	ums_function funcs[16] = {
		mult_c,
		mult_c,
		incr_c,
		decr_c,
		mult_c,
		mult_c,
		incr_c,
		decr_c,
		mult_c,
		mult_c,
		incr_c,
		decr_c,
		mult_c,
		mult_c,
		incr_c,
		decr_c
	};

	if (CreateUmsCompletionList(&complist_id, funcs, 16)) {
		fprintf(stderr, "Fail creating complist\n");
		return -1;
	}

	fprintf(stderr, "Completion list: %d\n", complist_id);

	EnterUmsSchedulingMode(entry_point, complist_id, &sched_id);
	EnterUmsSchedulingMode(entry_point, complist_id, &sched_id2);

	if (WaitUmsChildren())
		fprintf(stderr, "Oh no, res: %d", err ? err : (++err));
	return err;
}

static int incr_c(int ums_sched)
{
	fprintf(stderr, "I am completion element %d\n", ums_sched);
	fprintf(stderr, "incrementing c\n");
	c++;
	sleep(SLEEP_TIME);
	

	return c;
}

static int decr_c(int ums_sched)
{
	fprintf(stderr, "I am completion element %d\n", ums_sched);
	fprintf(stderr, "decrementing c\n");
	c--;
	sleep(SLEEP_TIME);

	return c;
}

static int mult_c(int ums_sched)
{
	fprintf(stderr, "I am completion element %d\n", ums_sched);
	fprintf(stderr, "multiplying c\n");
	c*=c;
	sleep(SLEEP_TIME);

	return c;
}

static int entry_point(int ums_sched)
{
	int res;
	int res_len;
	int shared[2];

	while (1) {
		fprintf(stderr, "entry_point: \n");

		DequeueUmsCompletionListItems(1, shared, &res_len);

		if (res_len <= 0) {
			fprintf(stderr, "dequeue failed!!!\n");
			return -1;
		}

		fprintf(stderr, "compelem to exec: %d\n", shared[0]);

		if (ExecuteUmsThread(shared[0])) {
			fprintf(stderr, "exec failed!\n");
			res = -1;
		}

		fprintf(stderr, "end entry_point: \n");
	}

	fprintf(stderr, "THE IMPOSSIBLE HAPPENED!\n");
	return res;
}
