/**
============================================================================
FAT32 LOGIC - V3.3 (SYNC & ROOT-CLUSTER GUARD)
============================================================================
Mudanças em relação à V3.2 (sincronizadas com a revisão do Runtime_Cmd /
beckup / recover — o bug de sobrescrita do diretório raiz):
 1) fat32_sanitize_fat(): repara FAT[0]/FAT[1] e, se a entrada do cluster
    da RAIZ estiver zerada ("raiz solta"), fecha a cadeia (EOC).
    Chamar 1x no fat32_mount, após calcular fat_start_sector.
 2) fat32_find_free_cluster(): NUNCA retorna 0, 1 nem o cluster da raiz
    (guarda dupla: cluster < 2 e cluster == root_c) + limite pelo tam. da FAT.
 3) fat32_get_next_cluster(): guarda cluster < 2 e fora do limite da FAT.
 4) fat32_set_cluster_entry(): checa limites antes de escrever.
 5) find_entry / find_entry_ext(): guarda de auto-loop (next == cluster).
============================================================================
*/
#include "fs/fat32.h"
#include "fs/fat32_logic.h"
#include "drivers/hw/ahci_hal.h"
#include "util/string.h"

extern fat32_bpb_t disk_bpb;
extern uint32_t fat_start_sector;
extern uint32_t data_start_sector;
extern uint32_t current_dir_cluster;
extern uint8_t fat32_current_dev_id;

extern int storage_read_sectors(uint8_t dev_id, uint32_t lba, uint32_t count, void* buffer);
extern int storage_write_sectors(uint8_t dev_id, uint32_t lba, uint32_t count, const void* buffer);

/* Limite superior de cluster que cabe na FAT (entradas = fat_size * 128) */
static uint32_t fat_max_cluster(void) {
    uint32_t fat_size = (disk_bpb.fat_size_16 != 0) ? disk_bpb.fat_size_16 : disk_bpb.fat_size_32;
    return fat_size * 128u;
}

uint32_t fat32_cluster_to_lba(uint32_t cluster) {
    if (cluster < 2) cluster = disk_bpb.root_cluster;
    return data_start_sector + ((cluster - 2) * disk_bpb.sectors_per_cluster);
}

/**
 * @brief NOVO: saneia o 1º setor da FAT (idempotente).
 *        Repara SOMENTE entradas zeradas (mínima intervenção):
 *        FAT[0] = media descriptor | FAT[1] = reservado | FAT[raiz] = EOC.
 * @return 1 se reparou algo, 0 se já estava íntegro.
 */
int fat32_sanitize_fat(void) {
    __attribute__((aligned(16))) uint8_t fat_buffer[512];
    if (storage_read_sectors(fat32_current_dev_id, fat_start_sector, 1, fat_buffer) != 1)
        return 0;
    uint32_t* t = (uint32_t*)fat_buffer;
    uint32_t root_c = (disk_bpb.root_cluster != 0) ? disk_bpb.root_cluster : 2;
    int mudou = 0;
    if ((t[0] & 0x0FFFFFFF) == 0x00000000) { t[0] = 0x0FFFFFF8; mudou = 1; }
    if ((t[1] & 0x0FFFFFFF) == 0x00000000) { t[1] = 0x0FFFFFFF; mudou = 1; }
    /* o cluster da raiz NUNCA pode ficar "livre": se zerou, fecha a cadeia */
    if (root_c < 128 && (t[root_c] & 0x0FFFFFFF) == 0x00000000) { t[root_c] = 0x0FFFFFF8; mudou = 1; }
    if (mudou)
        storage_write_sectors(fat32_current_dev_id, fat_start_sector, 1, fat_buffer);
    return mudou;
}

uint32_t fat32_get_next_cluster(uint32_t cluster) {
    if (cluster < 2 || cluster >= fat_max_cluster()) return 0x0FFFFFFF;
    __attribute__((aligned(16))) uint8_t fat_buffer[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_sector + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;
    if (storage_read_sectors(fat32_current_dev_id, fat_sector, 1, fat_buffer) == 0) {
        return 0x0FFFFFFF;
    }
    uint32_t next_cluster = *(uint32_t*)&fat_buffer[ent_offset];
    return next_cluster & 0x0FFFFFFF;
}

void fat32_set_cluster_entry(uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster >= fat_max_cluster()) return;   /* NOVO: limites */
    __attribute__((aligned(16))) uint8_t sector_buffer[512];
    uint32_t fat_sector = fat_start_sector + (cluster / 128);
    uint32_t fat_index = cluster % 128;
    if (storage_read_sectors(fat32_current_dev_id, fat_sector, 1, sector_buffer) == 1) {
        ((uint32_t*)sector_buffer)[fat_index] = (value & 0x0FFFFFFF);
        storage_write_sectors(fat32_current_dev_id, fat_sector, 1, sector_buffer);
    }
}

void fat32_to_83_filename(const char* input, char* output) {
    int i = 0, j = 0;
    for (int k = 0; k < 11; k++) output[k] = ' ';
    if (!input) return;
    if (input[0] == '.') {
        output[0] = '.';
        if (input[1] == '.') output[1] = '.';
        return;
    }
    while (input[i] != '.' && input[i] != '\0' && j < 8) {
        char c = input[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        output[j++] = c;
    }
    if (input[i] == '.') {
        i++; j = 8;
        while (input[i] != '\0' && j < 11) {
            char c = input[i++];
            if (c >= 'a' && c <= 'z') c -= 32;
            output[j++] = c;
        }
    }
}

void fat32_format_name_for_display(char* dest, unsigned char* src) {
    int p = 0;
    for (int i = 0; i < 8; i++) {
        if (src[i] != ' ') dest[p++] = src[i];
    }
    if (src[8] != ' ') {
        dest[p++] = '.';
        for (int i = 8; i < 11; i++) {
            if (src[i] != ' ') dest[p++] = src[i];
        }
    }
    dest[p] = '\0';
}

int fat32_find_entry(const char* name, fat32_directory_entry_t* out_entry) {
    __attribute__((aligned(16))) uint8_t sector_buffer[512];
    char fat_name[11];
    uint32_t cluster = current_dir_cluster;
    fat32_to_83_filename(name, fat_name);
    while (cluster < FAT32_EOC && cluster >= 2) {
        uint32_t lba = fat32_cluster_to_lba(cluster);
        for (uint32_t s = 0; s < disk_bpb.sectors_per_cluster; s++) {
            if (storage_read_sectors(fat32_current_dev_id, lba + s, 1, sector_buffer) == 0)
                return -1;
            fat32_directory_entry_t* entries = (fat32_directory_entry_t*)sector_buffer;
            for (int i = 0; i < 16; i++) {
                if (entries[i].filename[0] == 0x00) return -1;
                if (entries[i].filename[0] == 0xE5) continue;
                if (memcmp(entries[i].filename, fat_name, 11) == 0) {
                    memcpy(out_entry, &entries[i], sizeof(fat32_directory_entry_t));
                    return 0;
                }
            }
        }
        uint32_t next = fat32_get_next_cluster(cluster);
        if (next == cluster) break;          /* NOVO: guarda de auto-loop */
        cluster = next;
    }
    return -1;
}

int fat32_find_entry_ext(const char* name, fat32_directory_entry_t* out_entry, uint32_t* out_lba, uint32_t* out_offset) {
    __attribute__((aligned(16))) uint8_t sector_buffer[512];
    char fat_name[11];
    uint32_t cluster = current_dir_cluster;
    fat32_to_83_filename(name, fat_name);
    while (cluster < FAT32_EOC && cluster >= 2) {
        uint32_t lba = fat32_cluster_to_lba(cluster);
        for (uint32_t s = 0; s < disk_bpb.sectors_per_cluster; s++) {
            if (storage_read_sectors(fat32_current_dev_id, lba + s, 1, sector_buffer) == 0)
                return -1;
            fat32_directory_entry_t* entries = (fat32_directory_entry_t*)sector_buffer;
            for (int i = 0; i < 16; i++) {
                if (entries[i].filename[0] == 0x00) return -1;
                if (entries[i].filename[0] == 0xE5) continue;
                if (memcmp(entries[i].filename, fat_name, 11) == 0) {
                    memcpy(out_entry, &entries[i], sizeof(fat32_directory_entry_t));
                    if (out_lba)    *out_lba = lba + s;
                    if (out_offset) *out_offset = i * sizeof(fat32_directory_entry_t);
                    return 0;
                }
            }
        }
        uint32_t next = fat32_get_next_cluster(cluster);
        if (next == cluster) break;          /* NOVO: guarda de auto-loop */
        cluster = next;
    }
    return -1;
}

uint32_t fat32_find_free_cluster(void) {
    __attribute__((aligned(16))) uint8_t sector_buffer[512];
    uint32_t fat_size = (disk_bpb.fat_size_16 != 0) ? disk_bpb.fat_size_16 : disk_bpb.fat_size_32;
    uint32_t root_c = (disk_bpb.root_cluster != 0) ? disk_bpb.root_cluster : 2;
    for (uint32_t s = 0; s < fat_size; s++) {
        if (storage_read_sectors(fat32_current_dev_id, fat_start_sector + s, 1, sector_buffer) == 0)
            return 0;
        uint32_t* table = (uint32_t*)sector_buffer;
        for (int i = 0; i < 128; i++) {
            uint32_t cluster = (s * 128) + (uint32_t)i;
            if (cluster < 2) continue;        /* 0 e 1: reservados */
            if (cluster == root_c) continue;  /* RAIZ: jamais alocar (era o bug) */
            if ((table[i] & 0x0FFFFFFF) == 0x00000000) {
                return cluster;
            }
        }
    }
    return 0;
}
