
#include <pico/bootrom.h>

#include "picovd_config.h"

#include "vd_virtual_disk.h"
#include "vd_exfat_params.h"
#include "vd_exfat.h"
#include "vd_exfat_dirs.h"

// Rotate right 32-bit by one bit
static constexpr uint32_t ror32(uint32_t x) {
    return (x >> 1) | (x << 31);
}


/* 
 * Minimal exFAT Upcase table
 * See Microsoft spec §7.2 "Up-case Table Directory Entry" (Table 24)
 */

#if EXFAT_UPCASE_TABLE_COMPRESSED
// compressed version
// WORD array, total length = 2 (run) + 26 (maps) + 2 (run) = 30 entries
extern "C" constexpr uint16_t exfat_upcase_table[] = {
    // 1) Run of identity: 97 codepoints (0...96)
    0xFFFF, 'a',

    // 2) Explicit mappings for 'a'...'z' -> 'A'...'Z'
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z',

    // 3) Run of identity: the rest of the codepoints, until 0xFFFF
    0xFFFF, (0xFFFF - 'z'), // 0xFFFF - 122 = 0xFF85
};

#else

// uncompressed version
// 128 entries, 2 bytes each
extern "C" constexpr uint16_t exfat_upcase_table[] = {
     0,  1,  2,  3,  4,  5,  6,  7,
     8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63,
    64,  'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
    'X', 'Y', 'Z', 91, 92, 93, 94, 95,
    96,  'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
    'X', 'Y', 'Z', 123, 124, 125, 126, 127,
};

#endif

static constexpr size_t entry_count = sizeof(exfat_upcase_table) / sizeof(exfat_upcase_table[0]);

/// Compile time computation of the checksum for the directory entry,
/// See Microsoft spec §7.2.2 "TableChecksum Field", Figure 3.

// Compute the TableChecksum (32-bit) over the pre-computed up-case table bytes
static constexpr uint32_t compute_upcase_checksum(void) {
    // Compute 32-bit TableChecksum: one ROR32 + add per byte over the entire up-case region
    uint32_t sum = 0;

#if EXFAT_UPCASE_TABLE_COMPRESSED
    // For a compressed up-case table, only the compressed entries count.
    constexpr size_t total_words = entry_count;
#else
    // Total number of 16-bit words in the on-disk up-case region
    constexpr size_t total_words =
        EXFAT_UPCASE_TABLE_LENGTH_CLUSTERS
      * EXFAT_SECTORS_PER_CLUSTER
      * EXFAT_BYTES_PER_SECTOR
      / sizeof(uint16_t);
#endif

    // Total bytes = words × 2
    constexpr size_t total_bytes = total_words * 2;

    for (size_t byte_idx = 0; byte_idx < total_bytes; byte_idx++) {
        size_t word_idx = byte_idx / 2;
        uint16_t word = (word_idx < entry_count)
          ? word = exfat_upcase_table[word_idx]
          : word = static_cast<uint16_t>(word_idx);
        // Select the low or high byte
        uint8_t b = (byte_idx & 1)
          ? static_cast<uint8_t>((word >> 8) & 0xFF)
          : static_cast<uint8_t>(word & 0xFF);

        // spec: ROR32 then add the byte
        sum = ror32(sum);
        sum = (sum + b) & 0xFFFFFFFFu;
    }

    return sum;
}
extern "C" constexpr size_t   exfat_upcase_table_len = sizeof(exfat_upcase_table);
#ifdef __INTELLISENSE__
extern "C" constexpr uint32_t exfat_upcase_table_checksum = 0;
#else
extern "C" constexpr uint32_t exfat_upcase_table_checksum = compute_upcase_checksum();
#endif

// ---------------------------------------------------------------------------
// exFAT boot sector
// ---------------------------------------------------------------------------

uint32_t exfat_get_volume_serial_number(void) {
    // We want BOOT_RANDOM (0x0010) only
    constexpr uint32_t FLAGS = 0x0010; // XXX TBD, replace with the proper #define from the Pico headers
    // Buffer: [supported_flags, rand0, rand1, rand2, rand3]
    uint32_t buf[1 + 4] = {};
    rom_get_sys_info(buf, sizeof(buf)/sizeof(buf[0]), FLAGS);
    // Ignore errors. On an error, just use what happens to be in the buf[].
    // Combine the four 32-bit words into one 32-bit serial.
    return buf[1] ^ buf[2] ^ buf[3] ^ buf[4];
}

#define U16_LE(x) (uint8_t)((x) & 0xFF), (uint8_t)(((x) >> 8) & 0xFF)
#define U32_LE(x) U16_LE((x) & 0xFFFF), U16_LE(((x) >> 16) & 0xFFFF)
#define U64_LE(x) U32_LE((uint32_t)((x) & 0xFFFFFFFF)), U32_LE((uint32_t)(((uint64_t)(x) >> 32) & 0xFFFFFFFF))

extern "C"  constexpr uint8_t exfat_boot_sector_data[120] = {
    0xEB, 0x76, 0x90,                     // JumpBoot: JMP instruction (0xEB 0x76) + NOP
     'E','X','F','A','T',' ',' ',' ',     // FileSystemName: "EXFAT   "
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,      // must_zero[11-26] (16 bytes)
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,      // must_zero[27-42] (16 bytes)
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,      // must_zero[43-58] (16 bytes)
    0,0,0,0,0,                            // must_zero[59-63] (5 bytes)
    U64_LE(0),                            // PartitionOffset
    U64_LE(EXFAT_VOLUME_LENGTH),          // VolumeLength
    U32_LE(EXFAT_FAT_REGION_START_LBA),   // FATOffset
    U32_LE(EXFAT_FAT_REGION_LENGTH),      // FATLength
    U32_LE(EXFAT_CLUSTER_HEAP_START_LBA), // ClusterHeapOffset
    U32_LE(EXFAT_CLUSTER_COUNT),          // ClusterCount
    U32_LE(EXFAT_ROOT_DIR_START_CLUSTER), // RootDirectoryCluster
    U32_LE(0),                            // VolumeSerialNumber, filled at runtime
    U16_LE(EXFAT_FILE_SYSTEM_VERSION),    // FileSystemRevision (1.00)
    U16_LE(0),                            // VolumeFlags
    EXFAT_BYTES_PER_SECTOR_SHIFT,         // BytesPerSectorShift (log2 of 512)
    EXFAT_SECTORS_PER_CLUSTER_SHIFT,      // SectorsPerClusterShift (log2 of 8)
    1,                                    // NumberOfFats
    0,                                    // DriveSelect, not in use
    0xFF,                                 // PercentInUse, not in use
};

constexpr size_t exfat_boot_sector_data_length = sizeof(exfat_boot_sector_data);

_Static_assert(sizeof(exfat_boot_sector_data) == 120, "Boot sector header must be 120 bytes");

// -----------------------------------------------------------------------------
// Compile-time VBR checksum for sector 11 (Main/Backup Boot Checksum Sub-region)
// -----------------------------------------------------------------------------

// Return the byte at (lba, offset) matching the on-disk generator behavior
static constexpr uint8_t sector_byte(uint32_t lba, uint32_t off) {
    // Sector 0: boot sector, bytes from exfat_boot_sector_data, rest zero
    if (lba == 0) {
        // Zero VolumeFlags (bytes 106-107) and PercentInUse (byte 112) per spec
        if (off == 106 || off == 107 || off == 112) {
            return 0;
        }
        if (off < exfat_boot_sector_data_length) {
            return exfat_boot_sector_data[off];
        }
        return 0;
    }
    // Sectors 1-8: extended boot sectors: zeros except bytes 510=0x55, 511=0xAA
    if (lba >= 1 && lba <= 8) {
        if (off == 510) return 0x55;
        if (off == 511) return 0xAA;
        return 0;
    }
    // Sectors 9-10: all zeros
    if (lba <= 10) {
        return 0;
    }
    // Beyond sub-region: not included in VBR checksum
    return 0;
}

/**
 * Compute a VBR checksum over a range of sectors.
 *
 * @param start_lba      First LBA in the range.
 * @param start_offset   Byte offset within the first sector to begin (0-511).
 * @param lba_count      Number of consecutive sectors to include.
 * @param next_offset    Byte offset within the last sector to end (exclusive, 1-512).
 */
static constexpr uint32_t compute_vbr_checksum(uint32_t start_lba,
                                               uint32_t start_offset,
                                               uint32_t lba_count,
                                               uint32_t next_offset) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < lba_count; ++i) {
        uint32_t lba = start_lba + i;
        uint32_t off_begin = (i == 0 ? start_offset : 0);
        uint32_t off_end   = (i == lba_count - 1 ? next_offset : 512);
        for (uint32_t off = off_begin; off < off_end; ++off) {
            // Skip VolumeFlags (106-107) and PercentInUse (112) in the boot sector
            if (lba == 0 && (off == 106 || off == 107 || off == 112)) {
                continue;
            }
            sum = ror32(sum);
            sum = (sum + sector_byte(lba, off)) & 0xFFFFFFFFu;
        }
    }
    return sum;
}

/* -------------------------------------------------------------------------
 * Constants for runtime VBR checksum computation
 *
 * In the realm of 32-bit affine transforms:
 * sum_i+1 = ROR32(sum_i) + b_i
 * Unrolling that over any contiguous slice of bytes, gives
 * sum_final = ROR^n(sum_start) + C
 * where
 *	n = number of bytes in the slice (mod 32), and
 *	C = sum_0-1^n ( ROR^(n-1-i)(b_i)  (mod 2^32), a constant to be precomputed.
 *
 * This implementation, as it currently stands, does not work.
 * Hence, it is not used in the code.
 * Instead, we use a simple runtime version of the VBR checksum computation.
 * That uses the same algorithm as the compile-time version, but
 * iterates over the sectors and bytes, rather than using a precomputed
 * constant.
 * ------------------------------------------------------------------------- */

// Total bytes covered by sectors 0-10 (11 sectors x 512 bytes)
static constexpr int EXFAT_VBR_TOTAL_BYTES = 11 * 512; // 5632

// Byte-offset in sector 0 immediately after the VolumeSerialNumber field
static constexpr int EXFAT_VBR_SUFFIX_START_OFFSET = 104;

// Number of bytes in the suffix region (from offset 104 to end of sector 10)
static constexpr int EXFAT_VBR_SUFFIX_LEN = EXFAT_VBR_TOTAL_BYTES - EXFAT_VBR_SUFFIX_START_OFFSET; // 5632 - 104 = 5528

// Net rotate amount for suffix (modulo 32)
extern "C" constexpr int EXFAT_VBR_SUFFIX_ROT = EXFAT_VBR_SUFFIX_LEN % 32; // 5528 mod 32 = 24

// Materialize compile-time checksums around the VolumeSerialNumber field
extern "C" constexpr uint32_t EXFAT_VBR_CHECKSUM_PREFIX
  = compute_vbr_checksum(0, 0, 1, 100);

// Compile-time checksum of the suffix region
// (sectors 0 bytes 104-511, then sectors 1-10 full)
extern "C" constexpr uint32_t EXFAT_VBR_CHECKSUM_SUFFIX
   = compute_vbr_checksum(0, EXFAT_VBR_SUFFIX_START_OFFSET, 11, 512
);

// -----------------------------------------------------------------------------
// Compile-time first FAT sector beginning with initial cluster chains
// -----------------------------------------------------------------------------

#include <array>
#include <algorithm>

constexpr size_t exfat_fat0_entries =
    2 /* reserved */ +
    EXFAT_ALLOCATION_BITMAP_LENGTH_CLUSTERS +
    EXFAT_UPCASE_TABLE_LENGTH_CLUSTERS +
    EXFAT_ROOT_DIR_LENGTH_CLUSTERS;

// Max cluster index we'll need to fill
constexpr uint32_t exfat_fat0_required_size =
    std::max({
        EXFAT_ALLOCATION_BITMAP_START_CLUSTER + EXFAT_ALLOCATION_BITMAP_LENGTH_CLUSTERS,
        EXFAT_UPCASE_TABLE_START_CLUSTER      + EXFAT_UPCASE_TABLE_LENGTH_CLUSTERS,
        EXFAT_ROOT_DIR_START_CLUSTER          + EXFAT_ROOT_DIR_LENGTH_CLUSTERS
    });

// The FAT sector will contain entries for clusters 0 to N
constexpr std::array<uint32_t, exfat_fat0_required_size> generate_fat0_chains() {
    std::array<uint32_t, exfat_fat0_required_size> fat{};
    fat[0] = 0xFFFFFFF8u; // cluster 0
    fat[1] = 0xFFFFFFFFu; // cluster 1

    auto continuous_chain = [&](uint32_t start, uint32_t length) {
        for (uint32_t i = 0; i < length; ++i) {
            uint32_t cluster = start + i;
            if (i == length - 1) {
                fat[cluster] = 0xFFFFFFFFu; // end of chain
            } else {
                fat[cluster] = cluster + 1;
            }
        }
    };

    continuous_chain(EXFAT_ALLOCATION_BITMAP_START_CLUSTER, EXFAT_ALLOCATION_BITMAP_LENGTH_CLUSTERS);
    continuous_chain(EXFAT_UPCASE_TABLE_START_CLUSTER,      EXFAT_UPCASE_TABLE_LENGTH_CLUSTERS);
    continuous_chain(EXFAT_ROOT_DIR_START_CLUSTER,          EXFAT_ROOT_DIR_LENGTH_CLUSTERS);

    return fat;
}

static constexpr auto exfat_fat0_sector = generate_fat0_chains();
extern "C" const uint32_t * const exfat_fat0_sector_data = exfat_fat0_sector.data();
extern "C" const size_t           exfat_fat0_sector_data_len = exfat_fat0_sector.size() * sizeof(uint32_t);

_Static_assert(exfat_fat0_sector_data_len <= 512,
    "First FAT sector fixed data must less than or equal to 512 bytes");

// ---------------------------------------------------------------------------
// Pre-constructed first directory-entry structs for the root directory
//
// These entries should be in vd_exfat_dirs.c, but they need exfat_upcase_table_checksum
// and exfat_upcase_table_len to be constexpr, which are defined here.
// ---------------------------------------------------------------------------
static constexpr exfat_volume_label_dir_entry_t volume_label_entry = {
    .entry_type   = exfat_entry_type_volume_label,
    .char_count   = EXFAT_VOLUME_LABEL_LENGTH,
    .volume_label = PICOVD_VOLUME_LABEL_UTF16,
    // reserved[8] are zero-initialized
};

static constexpr exfat_allocation_bitmap_dir_entry_t allocation_bitmap_entry = {
    .entry_type    = exfat_entry_type_allocation_bitmap,
    .bitmap_flags  = 0,
    .first_cluster = EXFAT_ALLOCATION_BITMAP_START_CLUSTER,
    .data_length   = static_cast<uint64_t>(EXFAT_ALLOCATION_BITMAP_LENGTH_SECTORS) * EXFAT_BYTES_PER_SECTOR,
};

static constexpr exfat_upcase_table_dir_entry_t upcase_table_entry = {
    .entry_type     = exfat_entry_type_upcase_table,
    .table_checksum = exfat_upcase_table_checksum,  // now uint32_t
    .first_cluster  = EXFAT_UPCASE_TABLE_START_CLUSTER,
#if EXFAT_UPCASE_TABLE_COMPRESSED
    .data_length    = exfat_upcase_table_len, // 30 entries × 2 bytes each = 60 bytes
#else
    .data_length    = EXFAT_UPCASE_TABLE_LENGTH_CLUSTERS * EXFAT_BYTES_PER_SECTOR * EXFAT_SECTORS_PER_CLUSTER,
#endif
};

extern "C" constexpr exfat_root_dir_entries_first_t exfat_root_dir_first_entries_data = {
  volume_label_entry,
  allocation_bitmap_entry,
  upcase_table_entry,
};
