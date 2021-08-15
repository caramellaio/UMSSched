/*
 */

#include <linux/kernel.h>
#include <asm/switch_to.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <asm/uaccess.h> /* for put_user */

#include "ums_device.h"
#include "ums_scheduler.h"
/* TODO: Move ums requests in another file */
#include "../shared/ums_request.h"

#define MODULE_NAME_LOG "umsdev: "

MODULE_LICENSE("GPL");

MODULE_AUTHOR("Alberto Bombardelli");

/*
 * Prototypes - this would normally go in a .h file
 */

static long device_ioctl(struct file *file, unsigned int request, unsigned long data);

#define SUCCESS 0
#define FAILURE -1
#define DEVICE_NAME "usermodscheddev"
#define BUF_LEN 80

/*
 * Global variables are declared as static, so are global within the file.
 */
static struct file_operations fops = {
	.unlocked_ioctl = device_ioctl};

static struct miscdevice mdev = {
	.minor = 0,
	.name = DEVICE_NAME,
	.mode = S_IALLUGO,
	.fops = &fops};

/*
 * This function is called when the module is loaded
 */
int init_module(void)
{
	int ret;
	printk(KERN_DEBUG MODULE_NAME_LOG "init\n");

	ret = misc_register(&mdev);

	if (ret < 0) {
		printk(KERN_ALERT MODULE_NAME_LOG "Registering char device failed\n");
		return ret;
	}

	printk(KERN_DEBUG MODULE_NAME_LOG "Device registered successfully\n");

	return SUCCESS;
}

/*
 * This function is called when the module is unloaded
 */
void cleanup_module(void)
{
	/*
	* Unregister the device
	*/
	misc_deregister(&mdev);

	printk(KERN_DEBUG MODULE_NAME_LOG "exit\n");
}

static long device_ioctl(struct file *file, unsigned int request, unsigned long data)
{
	printk(KERN_DEBUG MODULE_NAME_LOG
	       "device_ioctl: pid->%d, path=%s, request=%u\n", current->pid,
	       file->f_path.dentry->d_iname, request);

	switch (request) {
	case UMS_REQUEST_ENTER_UMS_SCHEDULING:
	{
		int err = 0;
		int result = 0;
		/* in case of a single parameter, we can get it directly from data */
		int comp_list = (int)data;

		printk(KERN_DEBUG MODULE_NAME_LOG "Calling ums_sched_add...\n");

		err = ums_sched_add(comp_list, &result);

		/* TODO: Use better errors */
		if (err)
			return FAILURE;
			

		printk(KERN_INFO MODULE_NAME_LOG "ums scheduler entry %d created.\n", result);
		/* TODO: I will use `copy_to_user` in order to return the id */
		if (copy_to_user(&data, &result, sizeof(int)))
			return FAILURE;

	}
	break;

	/* Required parameters:
	 * ums_sched_id
	 * task_struct (from current)
	 * cpu to set
	*/
	case UMS_REQUEST_REGISTER_SCHEDULER_THREAD:
	{
		/* register a scheduler thread for CPU i */
		/* TODO: this comment is on how to set the correct CPU
		struct cpumask mask;
		cpumask_clear(&mask);
		cpumask_set_cpu(num_cpu, &mask);
		sched_setaffinity(pid, &mask)
		*/
	}
	break;

	case UMS_REQUEST_NEW_COMPLETION_LIST:
	{

	}
	break;

	case UMS_REQUEST_REGISTER_COMPLETION_ELEM:
	{

	}
	break;
	default: return FAILURE;
	}

	return SUCCESS;
}

