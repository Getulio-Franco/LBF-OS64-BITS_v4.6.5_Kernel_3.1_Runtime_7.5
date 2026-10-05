/* core_create.c - CORE AUTONOMO de criacao (mkdir/touch/write) */
#include "system/liblib.h"
#include "system/string.h"
#include "Runtime_Cmd/sys_cmd/cmd.h"

int ccmd_mkdir(const char* path) {
    if (!path || !path[0]) return CMD_ERR_ARGS;
    file_info_t fi;
    if (sys_fat_stat(path, &fi) == 0) return CMD_ERR_EXISTS;
    if (sys_fat_mkdir(path) != 0) return CMD_ERR_IO;
    if (sys_fat_stat(path, &fi) != 0 || !(fi.attributes & 0x10)) return CMD_ERR_VERIFY;
    return CMD_OK;
}
int ccmd_touch(const char* path) {
    if (!path || !path[0]) return CMD_ERR_ARGS;
    file_info_t fi;
    if (sys_fat_stat(path, &fi) == 0) return CMD_ERR_EXISTS;
    static uint8_t zero[1] = { 0 };
    if (sys_fat_write(path, zero, 0) != 0) return CMD_ERR_IO;
    if (sys_fat_stat(path, &fi) != 0 || fi.size != 0) return CMD_ERR_VERIFY;
    return CMD_OK;
}
int ccmd_write(const char* path, const uint8_t* data, uint32_t len) {
    if (!path || !path[0]) return CMD_ERR_ARGS;
    if (len > 0 && !data) return CMD_ERR_ARGS;
    if (sys_fat_write(path, (void*)(uintptr_t)data, len) != 0) return CMD_ERR_IO;
    file_info_t fi;
    if (sys_fat_stat(path, &fi) != 0 || fi.size != len) return CMD_ERR_VERIFY;
    return CMD_OK;
}
