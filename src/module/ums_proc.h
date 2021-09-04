#ifndef __UMS_PROC_H__
#define __UMS_PROC_H__
#include <linux/proc_fs.h>
struct proc_dir_entry *ums_proc_dir;

#define UMS_PROC_DIR_NAME "ums"

#define ums_proc_init()							\
	(NULL == (ums_proc_dir = proc_mkdir(UMS_PROC_DIR_NAME, NULL)))

#define ums_proc_deinit()						\
	do {								\
		proc_remove(ums_proc_dir);				\
		ums_proc_dir = NULL;					\
	} while (0)

#define ums_proc_root() (ums_proc_dir)

#endif /* __UMS_PROC_H__ */
