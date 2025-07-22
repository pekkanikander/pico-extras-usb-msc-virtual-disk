
#include <pico/bootrom.h>

#include "picovd_config.h"

#include "vd_virtual_disk.h"
#include "vd_exfat_params.h"
#include "vd_exfat.h"
#include "vd_exfat_dirs.h"

// Compile-time directory entries for SRAM.BIN
#if PICOVD_SRAM_ENABLED
PICOVD_DEFINE_FILE_STATIC(
    exfat_root_dir_sram_file_data,
    PICOVD_SRAM_FILE_NAME,
    PICOVD_SRAM_SIZE_BYTES
);
#endif // PICOVD_SRAM_ENABLED

// Compile-time directory entries for BOOTROM.BIN
#if PICOVD_BOOTROM_ENABLED

PICOVD_DEFINE_FILE_STATIC(
    exfat_root_dir_bootrom_file_data,
    PICOVD_BOOTROM_FILE_NAME,
    PICOVD_BOOTROM_SIZE_BYTES
);
#endif // PICOVD_BOOTROM_ENABLED

// Compile-time directory entries for FLASH.BIN
#if PICOVD_FLASH_ENABLED
PICOVD_DEFINE_FILE_STATIC(
    exfat_root_dir_flash_file_data,
    PICOVD_FLASH_FILE_NAME,
    PICOVD_FLASH_SIZE_BYTES
);
#endif // PICOVD_FLASH_ENABLED
