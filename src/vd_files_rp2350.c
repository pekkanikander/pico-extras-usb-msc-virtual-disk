
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <tusb.h>
#include <picovd_config.h>
#include "vd_virtual_disk.h"
#include "vd_exfat.h"
#include "vd_exfat_params.h"



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
