/* bancostore.c — ÚNICO core que inclui liblib.h */
#include "bancostore.h"
#include "../../system/liblib.h"
#include <string.h>
int bstore_exists(const char* f){ file_info_t st; return (sys_fat_stat(f,&st)==0)?1:0; }
int bstore_size(const char* f, uint32_t* out){ file_info_t st; if(sys_fat_stat(f,&st)!=0) return -1; if(out)*out=st.size; return 0; }
int bstore_read_whole(const char* f, uint8_t* buf, uint32_t max, uint32_t* out_len){
    int n = sys_fat_read(f, buf, max); if(n<0) return -1; if(out_len)*out_len=(uint32_t)n; return 0; }
int bstore_write_whole(const char* f, const uint8_t* buf, uint32_t len){
    return (sys_fat_write(f,(void*)buf,len)==0)?0:-1; }
int bstore_read_at(const char* f, uint8_t* buf, uint32_t len, uint32_t off){
    return (sys_fat_read_at(f,buf,len,off)==0)?0:-1; }
int bstore_write_at(const char* f, const uint8_t* buf, uint32_t len, uint32_t off){
    return (sys_fat_write_at(f,buf,len,off)==0)?0:-1; }
int bstore_delete(const char* f){ return (sys_fat_rm(f)==0)?0:-1; }
