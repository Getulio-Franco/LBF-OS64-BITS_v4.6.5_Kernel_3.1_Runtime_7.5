/* core_copy.c - CORE AUTONOMO de copia (fatias de 4KB, sem Heap Full) */
#include "system/liblib.h"
#include "system/string.h"
#include "Runtime_Cmd/sys_cmd/cmd.h"

#define CCOPY_CHUNK 4096u

int ccmd_copy(const char* src, const char* dst) {
    if (!src || !dst || !src[0] || !dst[0]) return CMD_ERR_ARGS;
    if (strcmp(src, dst) == 0) return CMD_ERR_ARGS;
    file_info_t fs, fd;
    if (sys_fat_stat(src, &fs) != 0) return CMD_ERR_NOTFOUND;
    if (fs.attributes & 0x10) return CMD_ERR_ARGS;   /* pasta: futuro core_move */

    uint8_t* buf = (uint8_t*)malloc(CCOPY_CHUNK);
    if (!buf) return CMD_ERR_MEM;

    int rc = CMD_OK;
    uint32_t off = 0;
    int first = 1;
    if (fs.size == 0) {
        if (sys_fat_write(dst, buf, 0) != 0) rc = CMD_ERR_IO;
    } else {
        while (off < fs.size) {
            uint32_t n = fs.size - off;
            if (n > CCOPY_CHUNK) n = CCOPY_CHUNK;
            if (sys_fat_read_at(src, buf, n, off) != 0) { rc = CMD_ERR_IO; break; }
            if (first) {
                if (sys_fat_write(dst, buf, n) != 0) { rc = CMD_ERR_IO; break; }
                first = 0;
            } else {
                if (sys_fat_append(dst, (const char*)buf, n) != 0) { rc = CMD_ERR_IO; break; }
            }
            off += n;
        }
    }
    if (rc != CMD_OK) { sys_fat_rm(dst); free(buf); return rc; }   /* sem cadaver */
    if (sys_fat_stat(dst, &fd) != 0 || fd.size != fs.size) {       /* verificacao */
        sys_fat_rm(dst); free(buf); return CMD_ERR_VERIFY;
    }
    free(buf);
    return CMD_OK;
}
