#ifndef __UMS_CONTEXT_SWITCH_H__
#define __UMS_CONTEXT_SWITCH_H__

#include <linux/ptrace.h>
#include <asm/processor.h>
#include <asm/fpu/internal.h>
#include <linux/sched/task_stack.h>

struct ums_context {
	struct pt_regs pt_regs;
	struct fpu fpu_regs;
};

#define gen_ums_context(task, res)					\
	do {								\
		memcpy(&(res)->pt_regs, task_pt_regs(task),		\
		       sizeof(struct pt_regs));				\
	} while (0)

#define get_ums_context(task, ctx)					\
	do {								\
		memcpy(&(ctx)->pt_regs, task_pt_regs(task),		\
		       sizeof(struct pt_regs));				\
	} while (0)

#define put_ums_context(task, ctx)					\
	do {								\
		memcpy(task_pt_regs(task), &(ctx)->pt_regs,		\
		       sizeof(struct pt_regs));				\
	} while (0)

#define dump_pt_regs(regs)						\
	__dump_pt_regs(&regs.pt_regs)

#define __dump_pt_regs(pt_regs)						\
	do {								\
		printk(KERN_DEBUG					\
		       "PT_REGS:\n\tip=%ld\n\tcs=%ld\n\tsp=%ld\n\tss=%ld\n",	\
		       (pt_regs)->ip,(pt_regs)->cs,			\
		       (pt_regs)->sp, (pt_regs)->ss);			\
	} while (0)
#endif /* __UMS_CONTEXT_SWITCH_H__ */
