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

	ums_function funcs[1] = {
		_timer,
	};

	if (CreateUmsCompletionList(&complist_id, funcs, 1)) {
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

	fprintf(stderr, "I am completion element %d\n", ums_sched);
	fprintf(stderr, "multiplying c\n");
	for (times = 0; times < 50; times++) {
		clock_t now;
		clock_t last_time = clock();
		double local = 0;
		UmsThreadYield();
		now = clock();

		local = (double)(now - last_time) / CLOCKS_PER_SEC;

		total += local;
		fprintf(stdout, "LOCAL TIME: %lf\n", local);
		times++;
	}

	total = total / times;

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
