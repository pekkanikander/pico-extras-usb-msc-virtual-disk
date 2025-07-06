#ifndef VD_EXFAT_PARAMS_H
#define VD_EXFAT_PARAMS_H

#include <tusb_config.h>
#include <assert.h>

#ifdef __cplusplus
  // C++11 and later: char16_t is a built-in type
#else
  typedef uint16_t char16_t;
#endif


// -----------------------------------------------------------------------------
// Virtual disk parameters
// -----------------------------------------------------------------------------

// In theory, you should be able to change the parameters below
// to create a larger or smaller virtual disk, with different sizes for the
// allocation bitmap, up-case table, and root directory. However, the
// current implementation has been tested only with this specific configuration,
// so changing these parameters may lead to unexpected behavior or errors.

// 1 GiB virtual disk size
#define VIRTUAL_DISK_SIZE              (0x40000000)

#define EXFAT_UPCASE_TABLE_COMPRESSED  (1)

#define EXFAT_ALLOCATION_BITMAP_START_CLUSTER    2U
#define EXFAT_ROOT_DIR_LENGTH_CLUSTERS           3U // 1 + 8 sectors 

// -----------------------------------------------------------------------------
// USB MSC interface parameters
// -----------------------------------------------------------------------------
#define MSC_BLOCK_SIZE                  CFG_TUD_MSC_BUFSIZE   // (512)

// Total blocks served by the Pico (256 K clusters × 8 sectors per cluster)
#define MSC_TOTAL_BLOCKS               (VIRTUAL_DISK_SIZE / MSC_BLOCK_SIZE)

// -----------------------------------------------------------------------------
// exFAT filesystem constants (compile-time parameters)
// -----------------------------------------------------------------------------

#define EXFAT_BYTES_PER_SECTOR_SHIFT    9U   // 2^9 = 512
#define EXFAT_BYTES_PER_SECTOR          (1U << EXFAT_BYTES_PER_SECTOR_SHIFT)
#define EXFAT_SECTORS_PER_CLUSTER_SHIFT 3U   // 2^3 = 8
#define EXFAT_SECTORS_PER_CLUSTER       (1U << EXFAT_SECTORS_PER_CLUSTER_SHIFT)

#define EXFAT_FILE_SYSTEM_VERSION_MAJOR 1U
#define EXFAT_FILE_SYSTEM_VERSION_MINOR 0U
#define EXFAT_FILE_SYSTEM_VERSION       \
  ((EXFAT_FILE_SYSTEM_VERSION_MAJOR << 8) | \
    EXFAT_FILE_SYSTEM_VERSION_MINOR)

#define EXFAT_VOLUME_LENGTH             (MSC_TOTAL_BLOCKS)

// -----------------------------------------------------------------------------
// Region start LBAs within the virtual disk
// -----------------------------------------------------------------------------

// LBA of the first FAT sector (FATOffset in the boot sector)
#define EXFAT_FAT_REGION_START_LBA        (0x18)
#define EXFAT_FAT_REGION_LENGTH           (0x800)

// LBA of the first data-cluster (ClusterHeapOffset in the boot sector)
// Note the gap, see docs/ExFAT-design.md Section Cluster Mapping
#define EXFAT_CLUSTER_HEAP_START_CLUSTER  (2) // Defined by MicroSoft
#define EXFAT_CLUSTER_HEAP_START_LBA      (0x8010U)
#define EXFAT_CLUSTER_COUNT               \
  (((MSC_TOTAL_BLOCKS - EXFAT_CLUSTER_HEAP_START_LBA) + (EXFAT_SECTORS_PER_CLUSTER - 1)) \
    / EXFAT_SECTORS_PER_CLUSTER)

// Allocation-bitmap is always at cluster 2, so its LBA is the same as
// EXFAT_CLUSTER_HEAP_START_LBA
#define EXFAT_ALLOCATION_BITMAP_START_LBA EXFAT_CLUSTER_HEAP_START_LBA
#define EXFAT_ALLOCATION_BITMAP_LENGTH_CLUSTERS                      \
    (((((EXFAT_CLUSTER_COUNT / 8)                                    \
       + (EXFAT_BYTES_PER_SECTOR - 1)) / EXFAT_BYTES_PER_SECTOR)     \
      + (EXFAT_SECTORS_PER_CLUSTER - 1)) / EXFAT_SECTORS_PER_CLUSTER)
#define EXFAT_ALLOCATION_BITMAP_LENGTH_SECTORS   \
    (EXFAT_ALLOCATION_BITMAP_LENGTH_CLUSTERS * EXFAT_SECTORS_PER_CLUSTER)

_Static_assert(EXFAT_ALLOCATION_BITMAP_LENGTH_CLUSTERS == 8,
               "Allocation Bitmap is tested only with 8 clusters.");


// Up-case Table region starts at after Allocation Bitmap, so its LBA is:
#define EXFAT_UPCASE_TABLE_START_LBA       \
    (EXFAT_ALLOCATION_BITMAP_START_LBA + EXFAT_ALLOCATION_BITMAP_LENGTH_SECTORS)
#define EXFAT_UPCASE_TABLE_LENGTH_CLUSTERS (\
    (EXFAT_UPCASE_TABLE_COMPRESSED ? 1U : 32U)) // 1 clusters for compressed, 32 for uncompressed
#define EXFAT_UPCASE_TABLE_LENGTH_SECTORS  \
    (EXFAT_UPCASE_TABLE_LENGTH_CLUSTERS * EXFAT_SECTORS_PER_CLUSTER)
#define EXFAT_UPCASE_TABLE_START_CLUSTER   \
    (EXFAT_ALLOCATION_BITMAP_START_CLUSTER \
     + EXFAT_ALLOCATION_BITMAP_LENGTH_CLUSTERS)

// -----------------------------------------------------------------------------
// Root-directory parameters
// -----------------------------------------------------------------------------

// Root directory after the Allocation Bitmap and Up-case Table regions
#define EXFAT_ROOT_DIR_START_LBA               \
  (EXFAT_ALLOCATION_BITMAP_START_LBA           \
    + EXFAT_ALLOCATION_BITMAP_LENGTH_SECTORS   \
    + EXFAT_UPCASE_TABLE_LENGTH_SECTORS)
#define EXFAT_ROOT_DIR_START_CLUSTER           \
    (EXFAT_ALLOCATION_BITMAP_START_CLUSTER     \
     + EXFAT_ALLOCATION_BITMAP_LENGTH_CLUSTERS \
     + EXFAT_UPCASE_TABLE_LENGTH_CLUSTERS)
#define EXFAT_ROOT_DIR_LENGTH_SECTORS (        \
    EXFAT_ROOT_DIR_LENGTH_CLUSTERS * EXFAT_SECTORS_PER_CLUSTER)

_Static_assert(EXFAT_ROOT_DIR_START_LBA
               == (EXFAT_ROOT_DIR_START_CLUSTER - 2) * EXFAT_SECTORS_PER_CLUSTER
                   + EXFAT_CLUSTER_HEAP_START_LBA,
               "Root directory start LBA must match the start cluster number");

// ----------------------------------------------------------------------------
// Volume label and GUID
// ----------------------------------------------------------------------------
#define EXFAT_VOLUME_GUID_LENGTH      16U // Length of the volume GUID in bytes
#define EXFAT_VOLUME_GUID_NULL        {0x00, 0x00, 0x00, 0x00, \
                                        0x00, 0x00, 0x00, 0x00, \
                                        0x00, 0x00, 0x00, 0x00, \
                                        0x00, 0x00, 0x00, 0x00}
#define EXFAT_VOLUME_GUID_NULL_STR    "00000000-0000-0000-0000-000000000000"

#define EXFAT_VOLUME_LABEL_MAX_LENGTH 11U // Maximum length of the volume label in UTF-16
#define EXFAT_VOLUME_LABEL_LENGTH \
  ((sizeof(PICOVD_VOLUME_LABEL_UTF16) / sizeof(char16_t)) - 1) // Length in UTF-16 characters

_Static_assert(EXFAT_VOLUME_LABEL_LENGTH <= EXFAT_VOLUME_LABEL_MAX_LENGTH,
               "Volume label must fit in the maximum length");

// ---------------------------------------------------------------------
// Compile-time assertions for configuration consistency
// ---------------------------------------------------------------------

_Static_assert(CFG_TUD_MSC_BUFSIZE == (1U << EXFAT_BYTES_PER_SECTOR_SHIFT),
               "MSC block size must match with exFAT bytes per sector");
_Static_assert((MSC_TOTAL_BLOCKS * MSC_BLOCK_SIZE) == VIRTUAL_DISK_SIZE,
               "Total blocks must match the virtual disk size");

#endif // VD_EXFAT_PARAMS_H
