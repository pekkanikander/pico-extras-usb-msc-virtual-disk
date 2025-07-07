
// Configuration file for PicoVD
// This file contains compile-time configuration options for the PicoVD project.
#ifndef PICOVD_CONFIG_H
#define PICOVD_CONFIG_H
// ---------------------------------------------------------------------------
// PicoVD Configuration
// ---------------------------------------------------------------------------
// Enable or disable features by defining the corresponding macros.

#define PICOVD_VOLUME_LABEL_UTF16       u"PicoVD"

// Add support for SRAM file
// This will enable the generation of a file named "SRAM.BIN" in the exFAT filesystem.
#define PICOVD_SRAM_ENABLED             (1)
#define PICOVD_SRAM_FILE_NAME           u"SRAM.BIN"
#define PICOVD_SRAM_FILE_NAME_LEN       8u
#define PICOVD_SRAM_SIZE_BYTES          (0x42000) // 264 KiB
#define PICOVD_SRAM_START_CLUSTER       (0x1F000) // See ExFAT-design.md
#define PICOVD_SRAM_START_LBA           EXFAT_CLUSTER_TO_LBA(PICOVD_SRAM_START_CLUSTER)

// Add support for ROM file
// This will enable the generation of a file named "BOOTROM.BIN" in the exFAT filesystem.
// The file will be generated from the contents of the Boot ROM segment on the RP2530.
#define PICOVD_BOOTROM_ENABLED          (1)
#define PICOVD_BOOTROM_FILE_NAME        u"BOOTROM.BIN"
#define PICOVD_BOOTROM_FILE_NAME_LEN    11u
#define PICOVD_BOOTROM_SIZE_BYTES       (0x8000) // 32 KiB
#define PICOVD_BOOTROM_START_CLUSTER    (0xE000) // Within the free cluster range
#define PICOVD_BOOTROM_START_LBA        EXFAT_CLUSTER_TO_LBA(PICOVD_BOOTROM_START_CLUSTER)

// Add support for FLASH file
// This will enable the generation of a file named "FLASH.BIN" in the exFAT filesystem.
// The file will be generated from the contents of the Flash segment on the RP2530
#define PICOVD_FLASH_ENABLED            (0)
#define PICOVD_FLASH_FILE_NAME          u"FLASH.BIN"
#define PICOVD_FLASH_FILE_NAME_LEN      9u
#define PICOVD_FLASH_SIZE_BYTES         (0x200000) // 2 Mb
#define PICOVD_FLASH_START_CLUSTER      (0xF000) // See ExFAT-design.md
#define PICOVD_FLASH_START_LBA          EXFAT_CLUSTER_TO_LBA(PICOVD_FLASH_START_CLUSTER)

// Add support for the RP2350 BootROM flash partitions
#define PICOVD_BOOTROM_PARTITIONS_ENABLED (1)
#define PICOVD_BOOTROM_PARTITIONS_FILE_BASE u"PARTx.BIN"
#define PICOVD_BOOTROM_PARTITIONS_FILE_NAME_LEN 8u

#endif
