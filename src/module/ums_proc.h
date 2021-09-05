#ifndef __UMS_PROC_H__
#define __UMS_PROC_H__

#include <linux/proc_fs.h>

extern struct proc_dir_entry *ums_proc_dir;

int ums_proc_init(void);

void ums_proc_deinit(void);

#define UMS_PROC_DIR_NAME "ums"

#define NAME_BUFF 128
#define UMS_FILE_MODE 0444

#define ums_proc_root() (ums_proc_dir)

#define ums_proc_geniddir(id, parent, res)				\
	do {								\
		char __proc_iddir_name[NAME_BUFF];			\
		sprintf(__proc_iddir_name, "%d", id);			\
		*(res) = proc_mkdir(__proc_iddir_name, parent);		\
	} while (0)

#define ums_proc_genidfile(id, parent, proc_ops, data, res)		\
	do {								\
		char __proc_idfile_name[NAME_BUFF];			\
		sprintf(__proc_idfile_name, "%d", id);			\
		*(res) = proc_create_data(__proc_idfile_name,		\
					UMS_FILE_MODE, parent,		\
					proc_ops, data);		\
	} while (0)

#define ums_proc_delete(proc) (proc_remove(proc))

#endif /* __UMS_PROC_H__ */
