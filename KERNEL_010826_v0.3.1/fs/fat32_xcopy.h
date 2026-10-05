/**
============================================================================
FAT32 COPY - HEADER V4.0
============================================================================
Copia/move arquivos com buffer fatiado (sem estourar heap), sem trava
externa (as funcoes de file ja se auto-travam) e com verificacao final.
============================================================================
*/
#ifndef FS_FAT32_COPY_H
#define FS_FAT32_COPY_H

#include <stdint.h>
#include "fs/fat32_file.h"   /* FS_SUCCESS, FS_NOT_FOUND, ... */

/* Codigos de erro proprios do xcopy */
#define XC_ERR_ARGS        -1   /* argumentos nulos                */
#define XC_ERR_MESMO       -2   /* origem == destino               */
#define XC_ERR_ORIG_DIR    -3   /* origem eh diretorio             */
#define XC_ERR_MEM         -4   /* kmalloc do buffer falhou        */
#define XC_ERR_READ        -5   /* falha de leitura na origem      */
#define XC_ERR_WRITE       -6   /* falha de escrita no destino     */
#define XC_ERR_APPEND      -7   /* falha de append no destino      */
#define XC_ERR_VERIFY      -8   /* tamanho final nao confere       */

/**
@brief Copia um arquivo (qualquer tamanho) usando fatias de 1 cluster.
@param source      Nome/caminho relativo ("README.TXT" ou "pasta/arq.bin").
@param destination Nome/caminho do novo arquivo (sobrescreve se existir).
@return FS_SUCCESS (0) ou codigo negativo XC_ERR_*.
*/
int fat32_copy_file(const char* source, const char* destination);

/**
@brief Move um arquivo (copy + delete da origem).
@return FS_SUCCESS (0) ou codigo negativo.
*/
int fat32_move_file(const char* source, const char* destination);
int fat32_find_entry_path(const char* path, fat32_directory_entry_t* out_entry);

#endif /* FS_FAT32_COPY_H */
