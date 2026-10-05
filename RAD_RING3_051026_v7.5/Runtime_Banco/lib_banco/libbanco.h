#ifndef LIBBANCO_H
#define LIBBANCO_H
#include <stdint.h>
#include "../sys_banco/banco.h"
static inline int sys_banco_create_db(const char* b)
    { return (int)banco_syscall(SYS_BANCO_CREATE_DB,(uint64_t)b,0,0,0,0,0,0); }
static inline int sys_banco_create_table(const char* b, const char* t, const char* spec)
    { return (int)banco_syscall(SYS_BANCO_CREATE_TABLE,(uint64_t)b,(uint64_t)t,(uint64_t)spec,0,0,0,0); }
static inline int sys_banco_insert(const char* b, const char* t, const char* valores)
    { return (int)banco_syscall(SYS_BANCO_INSERT,(uint64_t)b,(uint64_t)t,(uint64_t)valores,0,0,0,0); }
static inline int sys_banco_select(const char* b, const char* t, const char* campo, const char* valor, char* out, int cap)
    { return (int)banco_syscall(SYS_BANCO_SELECT,(uint64_t)b,(uint64_t)t,(uint64_t)campo,(uint64_t)valor,(uint64_t)out,(uint64_t)(uint32_t)cap,0); }
static inline int sys_banco_update(const char* b, const char* t, const char* pk, const char* valores)
    { return (int)banco_syscall(SYS_BANCO_UPDATE,(uint64_t)b,(uint64_t)t,(uint64_t)pk,(uint64_t)valores,0,0,0); }
static inline int sys_banco_delete(const char* b, const char* t, const char* pk)
    { return (int)banco_syscall(SYS_BANCO_DELETE,(uint64_t)b,(uint64_t)t,(uint64_t)pk,0,0,0,0); }
static inline int sys_banco_list_tables(const char* b, char* out, int cap)
    { return (int)banco_syscall(SYS_BANCO_LIST_TABLES,(uint64_t)b,(uint64_t)out,(uint64_t)(uint32_t)cap,0,0,0,0); }
static inline int sys_banco_list_fields(const char* b, const char* t, char* out, int cap)
    { return (int)banco_syscall(SYS_BANCO_LIST_FIELDS,(uint64_t)b,(uint64_t)t,(uint64_t)out,(uint64_t)(uint32_t)cap,0,0,0); }
static inline int sys_banco_count(const char* b, const char* t)
    { return (int)banco_syscall(SYS_BANCO_COUNT,(uint64_t)b,(uint64_t)t,0,0,0,0,0); }
static inline int sys_banco_drop_table(const char* b, const char* t)
    { return (int)banco_syscall(SYS_BANCO_DROP_TABLE,(uint64_t)b,(uint64_t)t,0,0,0,0,0); }
static inline int sys_banco_alter_rename(const char* b,const char* t,const char* o,const char* n)
    { return (int)banco_syscall(SYS_BANCO_ALTER_RENAME,(uint64_t)b,(uint64_t)t,(uint64_t)o,(uint64_t)n,0,0,0); }
static inline int sys_banco_alter_resize(const char* b,const char* t,const char* nm,const char* sp)
    { return (int)banco_syscall(SYS_BANCO_ALTER_RESIZE,(uint64_t)b,(uint64_t)t,(uint64_t)nm,(uint64_t)sp,0,0,0); }
static inline int sys_banco_alter_add(const char* b,const char* t,const char* sp)
    { return (int)banco_syscall(SYS_BANCO_ALTER_ADD,(uint64_t)b,(uint64_t)t,(uint64_t)sp,0,0,0,0); }
static inline int sys_banco_alter_drop(const char* b,const char* t,const char* nm)
    { return (int)banco_syscall(SYS_BANCO_ALTER_DROP,(uint64_t)b,(uint64_t)t,(uint64_t)nm,0,0,0,0); }
#endif /* LIBBANCO_H */
