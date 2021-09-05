#include "ums_proc.h"

struct proc_dir_entry *ums_proc_dir;

int ums_proc_init(void)
{
	ums_proc_dir = proc_mkdir(UMS_PROC_DIR_NAME, NULL);

	return ! ums_proc_dir;
}

void ums_proc_deinit(void)
{
	printk(KERN_DEBUG "Calling proc deinit");
	proc_remove(ums_proc_dir);
	ums_proc_dir = NULL;
}
