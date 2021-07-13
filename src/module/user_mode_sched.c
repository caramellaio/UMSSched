/*
 */

#include <linux/kernel.h>
#include <asm/switch_to.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <asm/uaccess.h> /* for put_user */

#include "user_mode_sched.h"

#define MODULE_NAME_LOG "umsdev: "

/*
 * Prototypes - this would normally go in a .h file
 */

static long device_ioctl(struct file *file, unsigned int request, unsigned long data);

#define SUCCESS 0
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

    if (ret < 0)
    {
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
    printk(KERN_DEBUG MODULE_NAME_LOG "device_ioctl: pid->%d, path=%s, request=%u\n", current->pid, file->f_path.dentry->d_iname, request);

    return SUCCESS;
}

MODULE_LICENSE("GPL");

MODULE_AUTHOR("Alberto Bombardelli");
