#ifndef LIB_CMD_H
#define LIB_CMD_H
#include <stdint.h>
#include "Runtime_Cmd/sys_cmd/cmd.h"  

/* ---- IDs de operacao (faixas reservadas por core) ---- */
#define CMD_OP_CHDIR    1                      /* navegacao (syscall direta)   */
/* core_create: 10-19 */
#define CMD_OP_MKDIR   10
#define CMD_OP_TOUCH   11
#define CMD_OP_WRITE   12
/* core_copy: 20-29 */
#define CMD_OP_COPY    20
/* core_delete: 30-39 */
#define CMD_OP_DELETE  30
/* reservas futuras: core_read 40-49 | core_rename 50-59 | core_search 60-69 */

extern uint64_t cmd_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                            uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
static inline int sys_cmd_chdir(const char* p)
{ return (int)cmd_syscall(CMD_OP_CHDIR, (uint64_t)p, 0, 0, 0, 0, 0, 0); }
static inline int sys_cmd_mkdir(const char* p)
{ return (int)cmd_syscall(CMD_OP_MKDIR, (uint64_t)p, 0, 0, 0, 0, 0, 0); }
static inline int sys_cmd_touch(const char* p)
{ return (int)cmd_syscall(CMD_OP_TOUCH, (uint64_t)p, 0, 0, 0, 0, 0, 0); }
static inline int sys_cmd_write(const char* p, const void* d, uint32_t n)
{ return (int)cmd_syscall(CMD_OP_WRITE, (uint64_t)p, (uint64_t)d, (uint64_t)n, 0, 0, 0, 0); }
static inline int sys_cmd_copy(const char* s, const char* d)
{ return (int)cmd_syscall(CMD_OP_COPY, (uint64_t)s, (uint64_t)d, 0, 0, 0, 0, 0); }
static inline int sys_cmd_delete(const char* p)
{ return (int)cmd_syscall(CMD_OP_DELETE, (uint64_t)p, 0, 0, 0, 0, 0, 0); }
#endif /* LIB_CMD_H */
