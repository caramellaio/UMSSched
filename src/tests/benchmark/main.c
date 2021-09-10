#include "../../user/ums_api.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

static int c = 0;
static int _timer(int ums_sched);

static int entry_point(int ums_sched);

int main(void) {
	int err = 0;
	ums_sched_id sched_id;
	ums_sched_id sched_id2;
	ums_complist_id complist_id;

	ums_function funcs[4] = {
		_timer,
		_timer,
		_timer,
		_timer,
	};

	if (CreateUmsCompletionList(&complist_id, funcs, 4)) {
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


static int _timer(int ums_sched)
{
	int times =0;
	double total = 0;

	for (times = 0; times < 1; times++) {
		clock_t now;
		clock_t local = 0;
		clock_t last_time = clock();

		printf("\n\n\nLast time: %ld\n", last_time);
		UmsThreadYield();

		now = clock();
		printf("Now: %ld\n\n\n\n", now);

		local = (now - last_time);

		total += local;
		fprintf(stdout, "LOCAL TIME: %ld\n", local);
		times++;
	}

	total /=  times;

	total /= CLOCKS_PER_SEC;

	fprintf(stdout, "AVG TIME: %lf\n", total);
	return c;
}

static int entry_point(int ums_sched)
{
	int res;
	int res_len;
	int shared[2];

	while (1) {
		if (DequeueUmsCompletionListItems(1, shared, &res_len))
			return -2;

		if (res_len <= 0) {
			return -1;
		}

		if (ExecuteUmsThread(shared[0])) {
			res = -1;
		}

	}
	return res;
}
