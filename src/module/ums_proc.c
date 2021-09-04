#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#define BUFSIZE 100

// MODULE_DESCRIPTION("proc example");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alberto Bombardelli <albertobombardelli123@gmail.com>");

#define MODULE_NAME_LOG "ums_proc: "

#define UMS_FOLDER_NAME "ums"
static ssize_t umsproc_read(struct file *file, char __user *ubuf, size_t count, loff_t *offset);
static ssize_t umsproc_write(struct file *file, const char __user *ubuf, size_t count, loff_t *offset);

static struct proc_dir_entry *ums_proc_dir;

static struct proc_ops pops =
{
        .proc_read = umsproc_read,
};

static int ums_proc_init(void)
{
        printk(KERN_DEBUG MODULE_NAME_LOG "init");

	ums_proc_dir = proc_mkdir(UMS_FOLDER_NAME, NULL);

        printk(KERN_DEBUG MODULE_NAME_LOG "created %s proc folder", 
	       UMS_FOLDER_NAME);
        return 0;
}

static void ums_proc_exit(void)
{
        printk(KERN_DEBUG MODULE_NAME_LOG "exit");

        proc_remove(ums_proc_dir);
}

/*
 * Public methods
*/

/*
 * Methods
 */

static ssize_t umsproc_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
        char buf[BUFSIZE];
        int len = 0;

        printk(KERN_DEBUG MODULE_NAME_LOG "read: pid->%d, length=%ld, offset=%llu,\n", current->pid, count, *ppos);

        if (*ppos > 0 || count < BUFSIZE)
                return 0;
        len += sprintf(buf, "this is a test proc file, pid=%d\n", current->pid);

        if (copy_to_user(ubuf, buf, len))
                return -EFAULT;
        *ppos = len;

        return len;
}

module_init(ums_proc_init);
module_exit(ums_proc_exit);
