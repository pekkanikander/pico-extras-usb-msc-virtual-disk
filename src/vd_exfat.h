#pragma once
#include <stdint.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------
// Boot sector image exported for MSC interface.
// Defined in the C++ source as a constexpr instance.
// ---------------------------------------------------------------
extern const uint8_t exfat_boot_sector_data[];
extern const size_t exfat_boot_sector_data_length;

/**
 * Compile-time VBR checksum for the exFAT boot sector.
 * This is the checksum for sectors 0-10, as per Microsoft spec §3.1.2,
 * ignoring VolumeSerialNumber, which is added at runtime.
 * For the math involved, see the C++ source file.
 */
// Net rotate amount for suffix (modulo 32)
extern const int      EXFAT_VBR_SUFFIX_ROT;
extern const uint32_t EXFAT_VBR_CHECKSUM_PREFIX;
extern const uint32_t EXFAT_VBR_CHECKSUM_SUFFIX;

// ---------------------------------------------------------------
// First FAT sector partial data (initial cluster chains)
// ---------------------------------------------------------------
extern const uint32_t * const exfat_fat0_sector_data; ///< First FAT sector data
extern const size_t   exfat_fat0_sector_data_len; // (128 bytes)

// ---------------------------------------------------------------
// Minimal up-case table
// ---------------------------------------------------------------
// Up-case table for exFAT, used for case-insensitive file name matching.
extern const uint16_t exfat_upcase_table[]; ///< Up-case table data
extern const size_t   exfat_upcase_table_len; /// Length of the up-case table in bytes (30 entries × 2 bytes each = 60 bytes)
extern const uint32_t exfat_upcase_table_checksum; ///< Checksum of the up-case table

// ---------------------------------------------------------------
// Functions to generate the root directory sectors
// ---------------------------------------------------------------
// This function generates the root directory sector data for exFAT.
extern  void exfat_generate_root_dir_fixed_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
extern  void exfat_generate_root_dir_dynamic_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);

// ---------------------------------------------------------------
// Macro to compute an LBA from a cluster number
// ---------------------------------------------------------------
#define EXFAT_CLUSTER_TO_LBA(cluster) \
    (EXFAT_CLUSTER_HEAP_START_LBA + \
    ((cluster) - EXFAT_CLUSTER_HEAP_START_CLUSTER) * EXFAT_SECTORS_PER_CLUSTER)

#ifdef __cplusplus
}
#endif
