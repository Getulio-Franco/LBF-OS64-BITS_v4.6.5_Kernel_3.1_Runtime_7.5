#ifndef CORE_CREATE_H
#define CORE_CREATE_H
#include <stdint.h>
int ccmd_mkdir(const char* path);                          /* cria pasta        */
int ccmd_touch(const char* path);                          /* cria arquivo vazio */
int ccmd_write(const char* path, const uint8_t* data, uint32_t len); /* cria/sobrescreve */
#endif
