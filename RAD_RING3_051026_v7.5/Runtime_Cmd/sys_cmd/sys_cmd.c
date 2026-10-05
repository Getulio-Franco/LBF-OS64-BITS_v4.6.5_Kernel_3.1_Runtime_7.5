/* sys_cmd.c - une os cores autônomos sob um unico ponto de entrada */
#include "system/liblib.h"
#include "Runtime_Cmd/sys_cmd/cmd.h"
#include "../core_cmd/core_create.h"
#include "../core_cmd/core_copy.h"
#include "../core_cmd/core_delete.h"

uint64_t cmd_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    (void)a4; (void)a5; (void)a6; (void)a7;
    switch (num) {
    case CMD_OP_CHDIR:  return (uint64_t)(uint32_t)sys_fat_chdir((const char*)a1);
    case CMD_OP_MKDIR:  return (uint64_t)(uint32_t)ccmd_mkdir((const char*)a1);
    case CMD_OP_TOUCH:  return (uint64_t)(uint32_t)ccmd_touch((const char*)a1);
    case CMD_OP_WRITE:  return (uint64_t)(uint32_t)ccmd_write((const char*)a1, (const uint8_t*)a2, (uint32_t)a3);
    case CMD_OP_COPY:   return (uint64_t)(uint32_t)ccmd_copy((const char*)a1, (const char*)a2);
    case CMD_OP_DELETE: return (uint64_t)(uint32_t)ccmd_delete((const char*)a1);
    default:            return (uint64_t)(uint32_t)CMD_ERR_ARGS;
    }
}
