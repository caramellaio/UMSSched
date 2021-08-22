#ifndef __UMS_COMPLIST_H__
#define __UMS_COMPLIST_H__
typedef int ums_complist_id;
typedef int ums_compelem_id;

#define UMS_COMPLIST_HASH_BITS 8
#define UMS_COMPELEM_HASH_BITS 8
int ums_complist_add(ums_complist_id *result);

int ums_complist_remove(ums_complist_id id);

int ums_complist_exists(ums_complist_id comp_id);

int ums_complist_reserve(ums_complist_id comp_id,
			 int to_reserve,
			 ums_compelem_id *ret_array,
			 int *size);

int ums_compelem_add(ums_compelem_id* result,
		     ums_complist_id list_id);

int ums_compelem_remove(ums_compelem_id id);

int ums_compelem_store_reg(ums_compelem_id compelem_id);

int ums_compelem_exec(ums_compelem_id compelem_id);

int ums_complist_init(void);
#endif /* __UMS_COMPLIST_H__ */
