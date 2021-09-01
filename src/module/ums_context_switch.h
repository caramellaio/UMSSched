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
		memset(&(res)->fpu_regs, 0, sizeof(struct fpu));	\
	} while (0)

#define put_ums_context(task, ctx)					\
	do {								\
		memcpy(task_pt_regs(task), &(ctx)->pt_regs,		\
		       sizeof(struct pt_regs));				\
		copy_fxregs_to_kernel(&(ctx)->fpu_regs);		\
	} while (0)

#endif /* __UMS_CONTEXT_SWITCH_H__ */
