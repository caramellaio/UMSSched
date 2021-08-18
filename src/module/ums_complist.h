#ifndef __UMS_COMPLIST_H__
#define __UMS_COMPLIST_H__
typedef int ums_complist_id;
typedef int ums_compelem_id;

#define UMS_COMPLIST_HASH_BITS 8
#define UMS_COMPELEM_HASH_BITS 8
int ums_complist_add(ums_complist_id *result);

int ums_complist_remove(ums_complist_id id);

int ums_complist_map(ums_complist_id list_id,
		     ums_compelem_id elem_id);

int ums_complist_unmap(ums_complist_id list_id,
		       ums_compelem_id elem_id);

int ums_compelem_add(ums_compelem_id* result);

int ums_compelem_remove(ums_compelem_id id);

int ums_complist_init(void);
#endif /* __UMS_COMPLIST_H__ */
