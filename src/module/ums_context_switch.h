/** @file ums_context_switch.h
 *
 * @author Alberto Bombardelli
 * @brief This file contains the definition of context switch struct and procedures
 *
 * This header file defines macros and structures to perform context switch
 * using user mode scheduling. To generate the context it is sufficient
 * to call gen_ums_context, to switch to the new context use put_ums_context,
 * to update the context when returned from user space use get_ums_context.
 *
 * @code
 *	// process 1
 *	spin_lock(&my_struct->lock);
 *	gen_ums_context(current, &my_struct->ctx);
 *	spin_unlock(&my_struct->lock);
 *	// it blocks to avoid messing with user stack
 *	set_current_state(TASK_INTERRUPTIBLE);
 *	schedule();
 *
 *	// process 2
 *	spin_lock(&my_struct->lock);
 *	put_ums_context(current, &my_struct->ctx);
 *	spin_unlock(&my_struct->lock);
 *
 *	// process 2 (coming back from user mode)
 *	spin_lock(&my_struct->lock);
 *	get_ums_context(current, &my_struct->ctx);
 *	spin_unlock(&my_struct->lock);
 *
 * @endcode
 *
 * @warning Only storing the context does not guarantee that the switch will
 *	return to the exact same point. If the process continues its execution
 *	is user mode he might update the stack and corrupt. You must use an 
 *	updated context.
 *
*/
#ifndef __UMS_CONTEXT_SWITCH_H__
#define __UMS_CONTEXT_SWITCH_H__

#include <linux/ptrace.h>
#include <asm/processor.h>
#include <asm/fpu/internal.h>
#include <linux/sched/task_stack.h>

/**
 * @struct ums_context 
 * \brief The context of a thread usable for UMS context switch
 *
 * @var pt_regs: value of the general registers of the task.
 *
 * @var fpu_regs: value of the register for floating point operations.
 *
 * The ums_context struct is a structure in charge of storing information
 * of a task that can be executed by another `host` task through a UMS switch
*/
struct ums_context {
	/**
	 * @pt_regs: Values of the general registers
	 *
	 * Here, the pt_regs are contained. They are used during context switch
	*/
	struct pt_regs pt_regs;

	/**
	 * @fpu_regs: Value of the registers for floating point operations
	 *
	 * This registers contains the state of floating point operations of
	 * when the task was interrupted
	*/
	struct fpu fpu_regs;
};

/**
 * @brief generate from a task a context suitable for UMS switch
 *
 * @param[in] task: the task_struct that will generate the context.
 * @param[out] res: a pointer to the resulting ums_context.
 *
 *
 * @return does not return values
 *
 * Takes the pt_regs from the task and initialize fpu_regs to 0
 *
 * @note This macro assumes that res has been allocated in memory
 *
 * @sa get_ums_context
 * @sa put_ums_context
*/
#define gen_ums_context(task, res)					\
	do {								\
		memcpy(&(res)->pt_regs, task_pt_regs(task),		\
		       sizeof(struct pt_regs));				\
	} while (0)

/**
 * @brief get a task a context suitable for UMS switch
 *
 * @param[in] task: the task_struct that will generate the context.
 * @param[in,out] ctx: the context that is going to be updated.
 *
 * @return does not return values
 *
 * Takes the pt_regs and the fpu_regs from the task
 *
 * @note This macro assumes that ctx has been allocated in memory
 *
 * @sa ums_context
 * @sa gen_ums_context
 * @sa put_ums_context
*/
#define get_ums_context(task, ctx)					\
	do {								\
		memcpy(&(ctx)->pt_regs, task_pt_regs(task),		\
		       sizeof(struct pt_regs));				\
	} while (0)

/**
 * @brief set the context to a task
 *
 * @param[in,out] task: the task_struct that will change the context.
 * @param[in] ctx: the context that is going to update the task.
 *
 * @return does not return values
 *
 * This function copy the pt_regs in the task and set the fpu register 
 * state. When the thread will return with user space it will execute 
 * the context ctx.
 *
 * @note Has side effects on task
 *
 * @warning using this macro with inconsistent context may cause internal errors
 * @sa ums_context
 * @sa gen_ums_context
 * @sa put_ums_context
*/
#define put_ums_context(task, ctx)					\
	do {								\
		memcpy(task_pt_regs(task), &(ctx)->pt_regs,		\
		       sizeof(struct pt_regs));				\
	} while (0)

/**
 * @brief dump pt_regs value of a ums_context
 * @param[in] ctx: the context to print
 *
 * @return does not return value
 *
 * This function dumps the value of the pt_regs, using KERN_DEBUG 
 * printk option.
 *
 * @sa ums_context
 * @sa __dump_pt_regs
*/
#define dump_pt_regs(ctx)						\
	__dump_pt_regs(&ctx.pt_regs)

/**
 * @brief dump the pt_regs
 * @param[in] pt_regs: the registers to dump
 *
 * @return does not return value
 *
 * This function dumps the value of the pt_regs, using KERN_DEBUG 
 * printk option.
 *
 * @sa ums_context
 * @sa dump_pt_regs
*/
#define __dump_pt_regs(pt_regs)						\
	do {								\
		printk(KERN_DEBUG					\
		       "PT_REGS:\n\tip=%ld\n\tcs=%ld\n\tsp=%ld\n\tss=%ld\n",	\
		       (pt_regs)->ip,(pt_regs)->cs,			\
		       (pt_regs)->sp, (pt_regs)->ss);			\
	} while (0)
#endif /* __UMS_CONTEXT_SWITCH_H__ */
