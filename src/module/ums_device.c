/*
 */

#include <linux/kernel.h>
#include <asm/switch_to.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/uaccess.h> /* for put_user */

#include "ums_device.h"
#include "ums_scheduler.h"
#include "ums_complist.h"
/* TODO: Move ums requests in another file */
#include "../shared/ums_request.h"


MODULE_LICENSE("GPL");

MODULE_AUTHOR("Alberto Bombardelli");

static long device_ioctl(struct file *file, unsigned int request, unsigned long data);

#define SUCCESS 0
#define FAILURE -1
#define DEVICE_NAME "usermodscheddev"
#define MODULE_NAME_LOG "umsdev: "
#define DEQUEUE_ELEM_MAX 512

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
		printk(KERN_ALERT MODULE_NAME_LOG "Registering ums device failed\n");
		return ret;
	}

	ret = ums_sched_init();

	if (ret) {
		printk(KERN_ALERT MODULE_NAME_LOG
		       "Initialization of sub-module ums_sched failed!\n");
		return ret;
	}

	ret = ums_complist_init();

	if (ret) {
		printk(KERN_ALERT MODULE_NAME_LOG
		       "Initialization of sub-module ums_complist failed!\n");
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
		ums_complist_id *in_buf, complist;

		in_buf = kmalloc(sizeof(ums_complist_id), GFP_KERNEL);

		if (copy_from_user(in_buf, (void*)data, sizeof(in_buf)))
			return FAILURE;

		complist = *in_buf;

		kfree(in_buf);

		err = ums_sched_add(complist, &result);

		/* TODO: Use better errors */
		if (err)
			return FAILURE;
			

		if (copy_to_user((void*)data, &result, sizeof(int))) {
			printk(KERN_ERR MODULE_NAME_LOG "ums_sched id copy_to_user failed!");
			return FAILURE;
		}

		printk(KERN_INFO MODULE_NAME_LOG "ums_sched id copied to user\n");
	}
	break;

	case UMS_REQUEST_EXIT_UMS_SCHEDULING:
	{
		int err = 0;
		ums_sched_id id = (ums_sched_id)data;

		err = ums_sched_remove(id);

		if (err)
			return FAILURE;
	}
	break;

	case UMS_REQUEST_WAIT_UMS_SCHEDULER:
	{
		int err = 0;
		ums_complist_id id = (ums_complist_id)data;

		err = ums_sched_wait(id);

		if (err)
			return FAILURE;

		printk(KERN_INFO MODULE_NAME_LOG "scheduler %d ended.\n", id);
	}
	break;
	/* Required parameters:
	 * ums_sched_id
	 * task_struct (from current)
	 * cpu to set
	*/
	case UMS_REQUEST_REGISTER_SCHEDULER_THREAD:
	{
		int err;
		ums_sched_id* in_buf;

		in_buf = kmalloc(sizeof(ums_sched_id), GFP_KERNEL);

		if (copy_from_user(in_buf, (void*)data, sizeof(ums_sched_id))) {
			kfree(in_buf);
			/* TODO: temporary */
			printk("Failed copy_from_user\n");
			return FAILURE;
		}

		printk(KERN_INFO MODULE_NAME_LOG "calling ums_sched_register_sched_thread.\n");
		err = ums_sched_register_sched_thread(*in_buf);

		if (err) {
			kfree(in_buf);
			return FAILURE;
		}
		
		printk(KERN_INFO MODULE_NAME_LOG 
		       "ums_sched %d, created new thread for cpu %d\n",
		       *in_buf, get_cpu());

		kfree(in_buf);
	}
	break;

	case UMS_REQUEST_EXEC:
	{
		int in_buf[2];
		int err = 0;

		if (copy_from_user(in_buf, (void*)data, sizeof(int)*2))
			return FAILURE;

		printk(KERN_DEBUG MODULE_NAME_LOG "Calling exec\n");
		


		err = ums_sched_exec(in_buf[0], in_buf[1]);

		if (err)
			return FAILURE;
	}
	break;

	case UMS_REQUEST_YIELD:
	{
		printk(KERN_DEBUG MODULE_NAME_LOG "Calling yield\n");

		if (ums_sched_yield()) {
			printk(KERN_ERR MODULE_NAME_LOG "yield failed!\n");
			return FAILURE;
		}
	}
	break;

	case UMS_REQUEST_NEW_COMPLETION_LIST:
	{
		int err = 0;
		int result = 0;

		printk(KERN_DEBUG MODULE_NAME_LOG "Calling ums_complist_add...\n");

		err = ums_complist_add(&result);

		/* TODO: Use better errors */
		if (err) {
			printk(KERN_ERR MODULE_NAME_LOG "Error %d in ums_complist_add\n", err);
			return FAILURE;
		}

		printk(KERN_INFO MODULE_NAME_LOG "ums completion list entry %d created.\n", result);

		if (copy_to_user((void*)data, &result, sizeof(int))) {
			printk(KERN_ERR MODULE_NAME_LOG "copy_to_user failed!\n");
			return FAILURE;
		}
		printk(KERN_INFO MODULE_NAME_LOG "ums_completion_list id copied to user\n");

	}
	break;

	case UMS_REQUEST_REMOVE_COMPLETION_LIST:
	{
		int err = 0;

		printk(KERN_DEBUG MODULE_NAME_LOG "Calling ums_complist_remove...\n");

		err = ums_complist_remove((int)data);

		if (err)
			return FAILURE;

		printk(KERN_INFO MODULE_NAME_LOG "ums completion list entry %d removed.\n", 
		       (int)data);
	}
	break;

	case UMS_REQUEST_REGISTER_COMPLETION_ELEM:
	{
		int err = 0;
		int result = 0;
		int *in_buf;


		in_buf = kmalloc(sizeof(int), GFP_KERNEL);

		if (copy_from_user(in_buf, (void*)data, sizeof(int)))
			return FAILURE;


		printk(KERN_DEBUG MODULE_NAME_LOG "Calling ums_compelem_add...\n");
		err = ums_compelem_add(&result, *in_buf);

		kfree(in_buf);
		if (err) {
			printk(KERN_ERR MODULE_NAME_LOG "ums_compelem_add failed!\n");
			return FAILURE;
		}

		printk(KERN_INFO MODULE_NAME_LOG "ums completion elem entry %d created.\n", result);

		if (copy_to_user((void*)data, &result, sizeof(int)))
			return FAILURE;
	}
	break;

	case UMS_REQUEST_REMOVE_COMPLETION_ELEM:
	{
		int err = 0;

		printk(KERN_DEBUG MODULE_NAME_LOG "Calling ums_compelem_remove\n...");
		err = ums_compelem_remove((int)data);

		if (err)
			return FAILURE;

		printk(KERN_INFO MODULE_NAME_LOG "ums completion elem entry %d removed.\n",
		       (int)data);
	}
	break;

	case UMS_REQUEST_DEQUEUE_COMPLETION_LIST:
	{
		int err = 0;
		int num_elems, ret_size;
		ums_complist_id comp_id, *ret_array;

		if (copy_from_user(&num_elems, (void*)data, sizeof(int)))
			goto ums_dequeue_fail;

		if (num_elems < 0 || num_elems > DEQUEUE_ELEM_MAX)
			goto ums_dequeue_fail;
		
		ret_array = kmalloc(sizeof(int) * (num_elems + 1), GFP_KERNEL);

		ums_sched_complist_by_current(&comp_id);

		err = ums_complist_reserve(comp_id, num_elems,
					   ret_array, &ret_size);

		*(ret_array + ret_size) = *ret_array;
		*ret_array = ret_size;

		if (err)
			goto ums_dequeue_fail;

		if (copy_to_user((void*) data, ret_array,
			         sizeof(int)*(ret_size+1))) {
			goto ums_dequeue_fail;
		}

		/* break means success in this case */
		goto ums_dequeue_end;

ums_dequeue_fail:
		err = FAILURE;

ums_dequeue_end:
		if (ret_array)
			kfree(ret_array);

		if (err)
			return FAILURE;
	}
	break;

	default: return FAILURE;
	}

	return SUCCESS;
}
