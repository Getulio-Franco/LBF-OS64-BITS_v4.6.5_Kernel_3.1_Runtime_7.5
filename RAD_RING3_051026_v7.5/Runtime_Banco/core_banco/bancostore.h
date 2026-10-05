/* bancostore.h */
#ifndef BANCOSTORE_H
#define BANCOSTORE_H
#include <stdint.h>
int bstore_exists(const char* f);
int bstore_size(const char* f, uint32_t* out);
int bstore_read_whole(const char* f, uint8_t* buf, uint32_t max, uint32_t* out_len);
int bstore_write_whole(const char* f, const uint8_t* buf, uint32_t len);
int bstore_read_at(const char* f, uint8_t* buf, uint32_t len, uint32_t off);
int bstore_write_at(const char* f, const uint8_t* buf, uint32_t len, uint32_t off);
int bstore_delete(const char* f);
#endif
