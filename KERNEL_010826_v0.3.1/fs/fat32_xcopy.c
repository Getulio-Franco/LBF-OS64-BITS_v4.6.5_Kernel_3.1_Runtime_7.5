/**
============================================================================
FAT32 COPY - V4.0 (CHUNKED, LOCK-SAFE, SELF-VERIFYING)
============================================================================
Regras de projeto (aprendidas com os bugs anteriores):
 1. SEM trava externa: fat32_write_file/append/read ja se auto-travam
    (fat32_lock). Segurar a trava AQUI e chamar elas = deadlock recursivo
    (foi exatamente o hang antigo do copy).
 2. SEM kmalloc do arquivo inteiro: copia em fatias de 1 cluster
    (limitadas a 16KB) -> nao estoura o heap em ELF grandes (100KB+).
 3. Ordem segura: 1a fatia via fat32_write_file (cria/trunca com overwrite
    seguro + free_chain), demais via fat32_append_file (estende a cadeia).
 4. Falha no meio => destino parcial eh removido (sem lixo/orfaos no disco).
 5. Verificacao final: tamanho do destino == tamanho da origem.
 6. Guardas: origem==destino, origem diretorio, origem inexistente.
============================================================================
*/
#include "fs/fat32.h"
#include "fs/fat32_logic.h"
#include "fs/fat32_file.h"
#include "fs/fat32_xcopy.h"
#include "drivers/pd/storage.h"
#include "drivers/video.h"
#include "util/string.h"
#include "mem/heap.h"

extern fat32_bpb_t disk_bpb;
extern uint32_t current_dir_cluster;
extern uint8_t fat32_current_dev_id;

#define XCOPY_MAX_CHUNK 16384u   /* teto do buffer, mesmo em clusters gigantes */

/* codigos de erro proprios (negativos, distintos de FS_*) */
#define XC_ERR_ARGS        -1
#define XC_ERR_MESMO       -2
#define XC_ERR_ORIG_DIR    -3
#define XC_ERR_MEM         -4
#define XC_ERR_READ        -5
#define XC_ERR_WRITE       -6
#define XC_ERR_APPEND      -7
#define XC_ERR_VERIFY      -8

static void xcopy_msg(const char* m) {
    terminal_print("[XCOPY] ");
    terminal_print(m);
    terminal_print("\n");
}

/**
@brief Copia um arquivo (qualquer tamanho) no diretorio atual/subpasta 1 nivel.
@return FS_SUCCESS (0) ou codigo negativo XC_ERR_*.
*/
int fat32_copy_file(const char* source, const char* destination) {
    /* 0) guardas de argumento */
    if (!source || !destination) return XC_ERR_ARGS;
    if (strcmp(source, destination) == 0) {
        xcopy_msg("origem == destino: abortado (evita auto-truncamento).");
        return XC_ERR_MESMO;
    }

    /* 1) metadados da origem (find_entry NAO trava - seguro chamar solto) */
    fat32_directory_entry_t entry;
    if (fat32_find_entry_path(source, &entry) != 0) {
        xcopy_msg("origem nao encontrada.");
        return FS_NOT_FOUND;
    }
    if (entry.attributes & 0x10) {
        xcopy_msg("origem eh diretorio: cp de pastas nao suportado ainda.");
        return XC_ERR_ORIG_DIR;
    }
    uint32_t file_size = entry.file_size;

    /* 2) buffer de fatia: 1 cluster, teto de 16KB */
    uint32_t chunk = (uint32_t)disk_bpb.sectors_per_cluster * 512u;
    if (chunk == 0) chunk = 4096;
    if (chunk > XCOPY_MAX_CHUNK) chunk = XCOPY_MAX_CHUNK;
    uint8_t* buf = (uint8_t*)kmalloc(chunk);
    if (!buf) {
        xcopy_msg("sem memoria para o buffer de fatia.");
        return XC_ERR_MEM;
    }

    /* 3) copia fatia a fatia: write (cria) + append (estende) */
    int rc = 0;
    uint32_t off = 0;
    int first = 1;
    if (file_size == 0) {
        /* arquivo vazio: cria destino vazio direto */
        if (fat32_write_file(destination, buf, 0) != FS_SUCCESS) rc = XC_ERR_WRITE;
    } else {
        while (off < file_size) {
            uint32_t n = file_size - off;
            if (n > chunk) n = chunk;
            /* leitura: trava interna so no memcpy - seguro */
            if (fat32_read_file_at_offset(source, buf, n, off) != 0) { rc = XC_ERR_READ; break; }
            if (first) {
                /* 1a fatia: write_file faz overwrite seguro + free_chain velha */
                if (fat32_write_file(destination, buf, n) != FS_SUCCESS) { rc = XC_ERR_WRITE; break; }
                first = 0;
            } else {
                if (fat32_append_file(destination, buf, n) != FS_SUCCESS) { rc = XC_ERR_APPEND; break; }
            }
            off += n;
        }
    }
    kfree(buf);

    /* 4) falha no meio: remove destino parcial (nao deixa lixo no disco) */
    if (rc != 0) {
        fat32_delete_file(destination);
        xcopy_msg("falha no meio da copia: destino parcial removido.");
        return rc;
    }

    /* 5) verificacao: tamanho do destino deve bater com a origem */
    fat32_directory_entry_t dst_entry;
    if (fat32_find_entry_path(destination, &dst_entry) != 0 || dst_entry.file_size != file_size) {
        xcopy_msg("verificacao de tamanho falhou.");
        return XC_ERR_VERIFY;
    }
    xcopy_msg("copia concluida e verificada.");
    return FS_SUCCESS;
}

/**
@brief Move um arquivo (copia + delete da origem) - base p/ mv entre pastas.
@return FS_SUCCESS ou codigo negativo.
*/
int fat32_move_file(const char* source, const char* destination) {
    int rc = fat32_copy_file(source, destination);
    if (rc != FS_SUCCESS) return rc;
    if (fat32_delete_file(source) != FS_SUCCESS) {
        xcopy_msg("move: copia ok, mas delete da origem falhou.");
        return -9;
    }
    return FS_SUCCESS;
}

/* Helper para buscar entradas suportando subdiretórios (ex: "arquivo/comb.elf") */
int fat32_find_entry_path(const char* path, fat32_directory_entry_t* out_entry) {
    int last_slash = -1;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash == -1) {
        return fat32_find_entry(path, out_entry);
    }

    char dir_name[128];
    if (last_slash >= 127) return -1;
    memcpy(dir_name, path, (size_t)last_slash);
    dir_name[last_slash] = '\0';
    const char* file_name = path + last_slash + 1;

    fat32_directory_entry_t dir_entry;
    if (fat32_find_entry(dir_name, &dir_entry) != 0 || !(dir_entry.attributes & 0x10)) {
        return -1;
    }

    uint32_t dir_c = (((uint32_t)dir_entry.cluster_high << 16) | dir_entry.cluster_low) & 0x0FFFFFFF;
    if (dir_c == 0) dir_c = 2;

    char fat_name[11];
    fat32_to_83_filename(file_name, fat_name);

    __attribute__((aligned(16))) uint8_t sect[512];
    while (dir_c < FAT32_EOC && dir_c >= 2) {
        uint32_t lba = fat32_cluster_to_lba(dir_c);
        for (uint32_t s = 0; s < disk_bpb.sectors_per_cluster; s++) {
            if (storage_read_sectors(fat32_current_dev_id, lba + s, 1, sect) == 0) return -1;
            fat32_directory_entry_t* e = (fat32_directory_entry_t*)sect;
            for (int i = 0; i < 16; i++) {
                if (e[i].filename[0] == 0x00) return -1;
                if (memcmp(e[i].filename, fat_name, 11) == 0) {
                    if (out_entry) *out_entry = e[i];
                    return 0;
                }
            }
        }
        dir_c = fat32_get_next_cluster(dir_c) & 0x0FFFFFFF;
    }
    return -1;
}
