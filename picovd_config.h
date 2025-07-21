
// Configuration file for PicoVD
// This file contains compile-time configuration options for the PicoVD project.
#ifndef PICOVD_CONFIG_H
#define PICOVD_CONFIG_H
// ---------------------------------------------------------------------------
// PicoVD Configuration
// ---------------------------------------------------------------------------
// Enable or disable features by defining the corresponding macros.

#define PICOVD_VOLUME_LABEL_UTF16       u"PicoVD"

// Maximum number of dynamic files to support
#define PICOVD_PARAM_MAX_DYNAMIC_FILES  (12)

#define PICOVD_UTF16_STRING_LEN(str) (sizeof(str)/sizeof(char16_t)-1) // Exclude NUL
#define PICOVD_UTF8_STRING_LEN(str)  (sizeof(str)/sizeof(char)    -1) // Exclude NUL

// Add support for SRAM file
// This will enable the generation of a file named "SRAM.BIN" in the exFAT filesystem.
#define PICOVD_SRAM_ENABLED             (1)
#define PICOVD_SRAM_FILE_NAME           u"SRAM.BIN"
#define PICOVD_SRAM_FILE_NAME_LEN       PICOVD_UTF16_STRING_LEN(PICOVD_SRAM_FILE_NAME)
#define PICOVD_SRAM_SIZE_BYTES          (0x42000) // 264 KiB
#define PICOVD_SRAM_START_CLUSTER       (0x1F000) // See ExFAT-design.md
#define PICOVD_SRAM_START_LBA           EXFAT_CLUSTER_TO_LBA(PICOVD_SRAM_START_CLUSTER)

// Add support for ROM file
// This will enable the generation of a file named "BOOTROM.BIN" in the exFAT filesystem.
// The file will be generated from the contents of the Boot ROM segment on the RP2530.
#define PICOVD_BOOTROM_ENABLED          (1)
#define PICOVD_BOOTROM_FILE_NAME        u"BOOTROM.BIN"
#define PICOVD_BOOTROM_FILE_NAME_LEN    PICOVD_UTF16_STRING_LEN(PICOVD_BOOTROM_FILE_NAME)
#define PICOVD_BOOTROM_SIZE_BYTES       (0x8000) // 32 KiB
#define PICOVD_BOOTROM_START_CLUSTER    (0xE000) // Within the free cluster range
#define PICOVD_BOOTROM_START_LBA        EXFAT_CLUSTER_TO_LBA(PICOVD_BOOTROM_START_CLUSTER)

// Add support for FLASH file
// This will enable the generation of a file named "FLASH.BIN" in the exFAT filesystem.
// The file will be generated from the contents of the Flash segment on the RP2530
#define PICOVD_FLASH_ENABLED            (1)
#define PICOVD_FLASH_FILE_NAME          u"FLASH.BIN"
#define PICOVD_FLASH_FILE_NAME_LEN      PICOVD_UTF16_STRING_LEN(PICOVD_FLASH_FILE_NAME)
#define PICOVD_FLASH_SIZE_BYTES         (0x200000) // 2 Mb
#define PICOVD_FLASH_START_CLUSTER      (0xF000) // See ExFAT-design.md
#define PICOVD_FLASH_START_LBA          EXFAT_CLUSTER_TO_LBA(PICOVD_FLASH_START_CLUSTER)

// Add support for the RP2350 BootROM flash partitions
#define PICOVD_BOOTROM_PARTITIONS_ENABLED            (1)
#define PICOVD_BOOTROM_PARTITIONS_MAX_FILES          (8)
#define PICOVD_BOOTROM_PARTITIONS_NAMES_STORAGE_SIZE (256)
// The 'x' in the string will be replaced with the partition index (0-7).
// PICOVD_BOOTROM_PARTITIONS_FILE_NAME_N_IDX must match the position of 'x' in the string.
#define PICOVD_BOOTROM_PARTITIONS_FILE_NAME_BASE     "PARTx.BIN" // UTF-8
#define PICOVD_BOOTROM_PARTITIONS_FILE_NAME_N_IDX    5u // Index of placeholder 'x' in the name
#define PICOVD_BOOTROM_PARTITIONS_FILE_NAME_LEN      PICOVD_UTF8_STRING_LEN(PICOVD_BOOTROM_PARTITIONS_FILE_NAME_BASE)
_Static_assert(PICOVD_BOOTROM_PARTITIONS_FILE_NAME_N_IDX < PICOVD_BOOTROM_PARTITIONS_FILE_NAME_LEN, 
    "PICOVD_BOOTROM_PARTITIONS_FILE_NAME_N_IDX must be within the name");

// Add support for a constantly changing file, to test the host's ability to re-read the disk contents
// This will enable the generation of a file named "CHANGING.TXt" in the exFAT filesystem.
#define PICOVD_CHANGING_FILE_ENABLED    (1)
#define PICOVD_CHANGING_FILE_NAME       u"CHANGING.TXT"
#define PICOVD_CHANGING_FILE_NAME_LEN   PICOVD_UTF16_STRING_LEN(PICOVD_CHANGING_FILE_NAME)
#define PICOVD_CHANGING_FILE_SIZE_BYTES 512 // XXX FIXME

// Dynamic file cluster allocation region
#define PICOVD_DYNAMIC_AREA_START_CLUSTER   (EXFAT_ROOT_DIR_START_CLUSTER + EXFAT_ROOT_DIR_LENGTH_CLUSTERS)
#define PICOVD_DYNAMIC_AREA_END_CLUSTER     (PICOVD_BOOTROM_START_CLUSTER) // 264 KiB
#define PICOVD_DYNAMIC_AREA_START_LBA       EXFAT_CLUSTER_TO_LBA(PICOVD_DYNAMIC_AREA_START_CLUSTER)
#define PICOVD_DYNAMIC_AREA_END_LBA         EXFAT_CLUSTER_TO_LBA(PICOVD_DYNAMIC_AREA_END_CLUSTER)

#endif
