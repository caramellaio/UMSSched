#ifndef __UMS_COMPLIST_H__
#define __UMS_COMPLIST_H__
typedef int ums_complist_id;

#define UMS_COMPLIST_HASH_BITS 8
#define UMS_COMPELEM_HASH_BITS 8
int ums_complist_add(ums_complist_id *result);
#endif /* __UMS_COMPLIST_H__ */
