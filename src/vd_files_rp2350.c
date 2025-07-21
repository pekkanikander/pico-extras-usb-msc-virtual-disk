
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <pico/bootrom.h>   // get_partition_table_info()
#include <pico/aon_timer.h> // aon_timer_get_datetime()

#include <tusb.h>

#include <picovd_config.h>
#include "vd_virtual_disk.h"
#include "vd_exfat.h"
#include "vd_exfat_params.h"
#include "vd_exfat_dirs.h"

#ifndef PICOVD_BOOTROM_PARTITIONS_NAMES_STORAGE_SIZE
#define PICOVD_BOOTROM_PARTITIONS_NAMES_STORAGE_SIZE 256
#endif

// Single contiguous buffer for all partition names (UTF-16)
static char16_t partition_name_buf[PICOVD_BOOTROM_PARTITIONS_NAMES_STORAGE_SIZE / sizeof(char16_t)];
static size_t partition_name_buf_used = 0;

#ifndef PICOVD_BOOTROM_PARTITIONS_MAX_FILES
#define PICOVD_BOOTROM_PARTITIONS_MAX_FILES 8
#endif
typedef struct {
    vd_file_t file;
    char16_t *name;
} partition_file_entry_t;

static partition_file_entry_t partition_file_entries[PICOVD_BOOTROM_PARTITIONS_MAX_FILES];

// Helper: Fill a vd_file_t from a BootROM flash partition entry
bool fill_vd_file_from_rp2350_partition(uint32_t part_idx, partition_file_entry_t *entry) {
    enum {
        PT_LOCATION_AND_FLAGS = 0x0010,
        PT_NAME               = 0x0080,
        PT_SINGLE_PARTITION   = 0x8000,
    };

    uint32_t pt_buf[34]; // plenty for location/flags + long name (max 32 words)
    uint32_t flags = PT_SINGLE_PARTITION |
                     PT_LOCATION_AND_FLAGS |
                     PT_NAME |
                     (part_idx << 24);   // partition# in top 8 bits per spec

    int words
    = rom_get_partition_table_info(pt_buf,
        (uint32_t)(sizeof(pt_buf)/sizeof(pt_buf[0])),
        flags);
    if (words < 3 /* XXX FIXME */) {
        return false; // BootROM error (invalid idx, hash mismatch, ...)
    }

    uint32_t *p   = pt_buf + 1;          // skip supported-flags word
    uint32_t loc  = *p++;                // permissions_and_location
    uint32_t flg  = *p++;                // permissions_and_flags

    // Extract start address and length (see §5.9.4.2 of the datasheet)
    uint32_t flash_page  =  (loc & 0x0001FFFFu);   // 4-kB units, matching cluster size
    uint32_t flash_size  = ((loc & 0x03ffe000u) >> 17) * 4096u;

    // NAME field
    uint8_t  name_len   = (*(uint8_t *)p) & 0x7F;
    const uint8_t *name_bytes = ((const uint8_t *)p) + 1;     // ASCII/UTF-8

    uint8_t base_name[PICOVD_BOOTROM_PARTITIONS_FILE_NAME_LEN];

    // Use the compile-time name if the partition name is empty. // XXX TODO: Not tested yet.
    if (name_len == 0) {
        name_len = PICOVD_BOOTROM_PARTITIONS_FILE_NAME_LEN;
        memcpy(base_name, PICOVD_BOOTROM_PARTITIONS_FILE_NAME_BASE, PICOVD_BOOTROM_PARTITIONS_FILE_NAME_LEN);
        base_name[PICOVD_BOOTROM_PARTITIONS_FILE_NAME_N_IDX] = '0' + part_idx;
        name_bytes = (const uint8_t *)base_name;
    }

    struct timespec ts;
    aon_timer_get_time(&ts);

    // Linear allocation for UTF-16 name
    if (partition_name_buf_used + name_len > (sizeof(partition_name_buf) / sizeof(char16_t))) {
        // XXX TODO: Dynamically allocate a buffer for the name from the heap
        printf("BootROM partition name buffer exhausted, skipping partition %u\n", part_idx);
        return false;
    }
    char16_t *name_ptr = &partition_name_buf[partition_name_buf_used];
    for (size_t i = 0; i < name_len; i++) {
        name_ptr[i] = name_bytes[i]; // UTF-8 to UTF-16LE (ASCII only)
    }
    partition_name_buf_used += name_len;

    entry->name = name_ptr;
    entry->file = (vd_file_t){
        .name            = name_ptr,
        .name_length     = name_len,
        .file_attributes = FAT_FILE_ATTR_READ_ONLY,
        .first_cluster   = flash_size ? flash_page + PICOVD_FLASH_START_CLUSTER : 0,
        .size_bytes      = flash_size,
        .creat_time_sec  = ts.tv_sec,
        .mod_time_sec    = ts.tv_sec,
        .get_content     = NULL, // No content callback for partitions
    };

    return true;
}

void vd_files_rp2350_init_bootrom_partitions(void) {
#if PICOVD_BOOTROM_PARTITIONS_ENABLED
    size_t name_len = 0;
    #ifndef PICOVD_BOOTROM_PARTITIONS_MAX_FILES
    for (uint32_t i = 0; i < PICOVD_BOOTROM_PARTITIONS_MAX_FILES; ++i) {
        #endif
        char16_t *name_ptr = NULL;
        if (fill_vd_file_from_rp2350_partition(i, &partition_file_entries[i])) {
            vd_exfat_dir_add_file(&partition_file_entries[i].file);
        }
    }
#endif
}

void vd_file_sector_get_bootrom(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    assert(lba >= PICOVD_BOOTROM_START_LBA);
    assert(lba  < PICOVD_BOOTROM_START_LBA + PICOVD_BOOTROM_SIZE_BYTES / EXFAT_BYTES_PER_SECTOR);

    const uint32_t address = ((lba - PICOVD_BOOTROM_START_LBA) << EXFAT_BYTES_PER_SECTOR_SHIFT); // Bootrom is mapped at address 0x0
    memcpy(buffer, (const void*)address, bufsize);
}

void vd_file_sector_get_sram(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    assert(lba >= PICOVD_SRAM_START_LBA);
    assert(lba  < PICOVD_SRAM_START_LBA + PICOVD_SRAM_SIZE_BYTES / EXFAT_BYTES_PER_SECTOR);

    const uint32_t address = ((lba - PICOVD_SRAM_START_LBA) << EXFAT_BYTES_PER_SECTOR_SHIFT) + SRAM0_BASE;
    memcpy(buffer, (const void*)address, bufsize);
}

void vd_file_sector_get_flash(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    assert(lba >= PICOVD_FLASH_START_LBA);
    assert(lba  < PICOVD_FLASH_START_LBA + PICOVD_FLASH_SIZE_BYTES / EXFAT_BYTES_PER_SECTOR);

    uint32_t flash_address;

    if ((PICOVD_FLASH_START_LBA << EXFAT_BYTES_PER_SECTOR_SHIFT) == XIP_BASE) {
        // Optimized version for the RP2350, with a directly mapped flash pages
        flash_address = lba << EXFAT_BYTES_PER_SECTOR_SHIFT;
    } else {
        // Generic version, with a flash address offset
        flash_address = ((lba - PICOVD_FLASH_START_LBA) << EXFAT_BYTES_PER_SECTOR_SHIFT) + XIP_BASE;
    }

    memcpy(buffer, (const void*)flash_address, bufsize);
}
