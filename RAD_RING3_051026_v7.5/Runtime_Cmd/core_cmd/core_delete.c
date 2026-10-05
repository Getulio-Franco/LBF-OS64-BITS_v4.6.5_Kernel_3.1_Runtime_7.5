/* core_delete.c - CORE AUTONOMO de remocao */
#include "system/liblib.h"
#include "system/string.h"
#include "Runtime_Cmd/sys_cmd/cmd.h"

int ccmd_delete(const char* path) {
    if (!path || !path[0]) return CMD_ERR_ARGS;
    file_info_t fi;
    if (sys_fat_stat(path, &fi) != 0) return CMD_ERR_NOTFOUND;
    if (fi.attributes & 0x10) return CMD_ERR_ARGS;   /* dir: futuro core_rmdir (31) */
    if (sys_fat_rm(path) != 0) return CMD_ERR_IO;
    if (sys_fat_stat(path, &fi) == 0) return CMD_ERR_VERIFY;  /* ainda existe? */
    return CMD_OK;
}
