/**
 * @file src/vd_virtual_disk.c
 * @brief USB MSC “virtual disk” interface stubs for PicoVD.
 */

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <tusb.h>
#include <picovd_config.h>

#include "vd_virtual_disk.h"
#include "vd_exfat_params.h"
#include "vd_exfat.h"
#include "vd_exfat_dirs.h"

#include <pico/unique_id.h>

/**
 * --------------------------------------------------------------------------
 * Dynamic File Allocation Region handling
 *
 * We reserve a region of the virtual disk for dynamic file allocation.
 * This region is used to store the contents of dynamic files, which are
 * created and deleted at runtime.
 * --------------------------------------------------------------------------
 */

// Simple sequential allocator for dynamic clusters
static uint32_t dynamic_cluster_map_next_cluster = PICOVD_DYNAMIC_AREA_START_CLUSTER;
static size_t   dynamic_cluster_map_count = 0;

// Dynamic cluster map entry: maps a cluster range to a handler
typedef struct {
    uint32_t first_cluster;
    size_t   file_size_bytes;
    void   (*handler)(uint32_t file_offset, void* buf, uint32_t bufsize);
} dynamic_cluster_map_entry_t;

#ifndef PICOVD_PARAM_MAX_DYNAMIC_FILES
#define PICOVD_PARAM_MAX_DYNAMIC_FILES 12
#endif

static dynamic_cluster_map_entry_t dynamic_cluster_map[PICOVD_PARAM_MAX_DYNAMIC_FILES];

// Allocates clusters for a dynamic file and registers its handler
static uint32_t vd_dynamic_cluster_alloc(size_t region_size_bytes, void (*handler)(uint32_t file_offset, void* buf, uint32_t bufsize)) {
    const size_t cluster_size_bytes = EXFAT_BYTES_PER_SECTOR * EXFAT_SECTORS_PER_CLUSTER;
    size_t clusters_needed = (region_size_bytes + cluster_size_bytes - 1) / cluster_size_bytes;

    // Check if there is enough space in the dynamic area
    if (dynamic_cluster_map_next_cluster + clusters_needed >= PICOVD_DYNAMIC_AREA_END_CLUSTER) {
        return 0; // Out of space
    }
    if (dynamic_cluster_map_count >= PICOVD_PARAM_MAX_DYNAMIC_FILES) {
        return 0; // Out of mapping entries
    }
    uint32_t allocated_cluster = dynamic_cluster_map_next_cluster;
    dynamic_cluster_map_next_cluster += clusters_needed;

    dynamic_cluster_map[dynamic_cluster_map_count++] = (dynamic_cluster_map_entry_t){
        .first_cluster = allocated_cluster,
        .file_size_bytes = region_size_bytes,
        .handler = handler,
    };
    return allocated_cluster;
}


/**
 * --------------------------------------------------------------------------
 * LBA Region Table and Handlers
 *
 * We divide the virtual disk into regions, each starting at a given LBA
 * and served by a dedicated handler function.  At runtime, the MSC read
 * callback will consult this table to dispatch each LBA to the correct
 * generator.
 * --------------------------------------------------------------------------
 */

// Table entry: start_lba marks the first sector of a region,
// handler is invoked for any LBA in that region.
typedef struct {
    usb_msc_lba_read10_fn_t handler;
    uint32_t        next_lba; // Next LBA after this region
} lba_region_t;

static void gen_boot_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
static void gen_extb_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
static void gen_zero_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
static void gen_cksm_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
static void gen_fat0_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
static void gen_ones_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
static void gen_upcs_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
static void gen_dirs_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);

// Forward declaration for dynamic area handler
static void vd_dynamic_area_handler(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);

// Region table: each entry defines a region of the virtual disk
static const lba_region_t lba_regions[] = {
    // §2 Volume Structure
    { gen_boot_sector, 1 },  // LBA 0, §3.1 Boot Sector
    { gen_extb_sector, 9 },  // §3.2 Extended Boot Sectors
    { gen_zero_sector, 11 }, // §3.3 Main and Backup OEM Parameters
    { gen_cksm_sector, 12 }, // §3.4 Main Boot Checksum Sub-region
    { gen_boot_sector, 13 }, // §3.1 Backup Boot Sector
    { gen_extb_sector, 21 }, // §3.2 Extended Boot Sectors (backup)
    { gen_zero_sector, 23 }, // §3.3 Main and Backup OEM Parameters (backup)
    { gen_cksm_sector, 24 }, // §3.4 Backup Boot Checksum Sub-region
#if EXFAT_FAT_REGION_START_LBA > 24
    // Space between Backup Boot Checksum Sub-region and FAT region, if any
    // This is not used in our exFAT, but we reserve it for future use.
    // It is zero-filled.
   { gen_zero_sector, EXFAT_FAT_REGION_START_LBA },
#endif

    // §4   FAT region, first sector
    { gen_fat0_sector, EXFAT_FAT_REGION_START_LBA + 1 },
    // §4 Rest of FAT region and unused sectors
    { gen_zero_sector, EXFAT_CLUSTER_HEAP_START_LBA, },
#if EXFAT_ALLOCATION_BITMAP_START_LBA > EXFAT_CLUSTER_HEAP_START_LBA
    // Space between FAT and Allocation Bitmap regions, if any
    { gen_zero_sector,  EXFAT_ALLOCATION_BITMAP_START_LBA, },
#endif

    // §7.1 Allocation Bitmap region (not used in our exFAT)
    { gen_ones_sector, EXFAT_ALLOCATION_BITMAP_START_LBA + EXFAT_ALLOCATION_BITMAP_LENGTH_SECTORS, },
    // §7.2 Up-case Table first sector
    { gen_upcs_sector, EXFAT_UPCASE_TABLE_START_LBA + EXFAT_UPCASE_TABLE_LENGTH_SECTORS, },
    // §7.2 Zero sectors before the root directory
    { gen_zero_sector, EXFAT_ROOT_DIR_START_LBA, },
    // §7.4 Root Directory sectors, from vd_exfat_directory.c
    { exfat_generate_root_dir_fixed_sector,   EXFAT_ROOT_DIR_START_LBA + 1, },
    { exfat_generate_root_dir_dynamic_sector, EXFAT_ROOT_DIR_START_LBA + EXFAT_ROOT_DIR_LENGTH_SECTORS },

    // Add dynamic area handler for the dynamic cluster region
    { vd_dynamic_area_handler, PICOVD_DYNAMIC_AREA_END_LBA },

#if PICOVD_BOOTROM_ENABLED
    // BOOTROM.BIN file, from vd_rp2350.c
    { gen_zero_sector, PICOVD_BOOTROM_START_LBA, },
    { vd_file_sector_get_bootrom, PICOVD_BOOTROM_START_LBA + PICOVD_BOOTROM_SIZE_BYTES / EXFAT_BYTES_PER_SECTOR, },
#endif

#if PICOVD_FLASH_ENABLED
    // FLASH.BIN file, from vd_rp2350.c
    { gen_zero_sector, PICOVD_FLASH_START_LBA, },
    { vd_file_sector_get_flash, PICOVD_FLASH_START_LBA + PICOVD_FLASH_SIZE_BYTES / EXFAT_BYTES_PER_SECTOR, },
#endif

#if PICOVD_SRAM_ENABLED
    // SRAM.BIN file, from vd_rp2350.c
    { gen_zero_sector, PICOVD_SRAM_START_LBA, },
    { vd_file_sector_get_sram, PICOVD_SRAM_START_LBA + PICOVD_SRAM_SIZE_BYTES / EXFAT_BYTES_PER_SECTOR, },
#endif

};

// Helper functions
static inline uint32_t get_volume_serial_number(void) {
    static bool inited = false;
    static uint32_t volume_serial_number = 0;

    if (!inited) {
        pico_unique_board_id_t board_id;
        pico_get_unique_board_id(&board_id);
        // Use the first 4 bytes of the 8-byte board ID as the 32-bit serial
        volume_serial_number =
                        ((uint32_t)board_id.id[0]      ) |
                        ((uint32_t)board_id.id[1] <<  8) |
                        ((uint32_t)board_id.id[2] << 16) |
                        ((uint32_t)board_id.id[3] << 24);
        inited = true;
    }
    return volume_serial_number;
}

// Sector generators
static void gen_zero_sector(uint32_t lba __unused, uint32_t offset __unused, void* buf, uint32_t bufsize) {
    memset(buf, 0, bufsize);
}

static void gen_ones_sector(uint32_t lba __unused, uint32_t offset __unused, void* buf, uint32_t bufsize) {
    memset(buf, 0xff, bufsize);
}
static void gen_extb_sector_signature(uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    // Generate an extended boot sector with the signature bytes 0x55 and 0xAA
    // at the end of the sector, if they fall within the requested offset and size.
    assert(offset < MSC_BLOCK_SIZE);
    assert(offset + bufsize <= MSC_BLOCK_SIZE);

    const uint32_t pos55 = MSC_BLOCK_SIZE - 2;  // 510
    const uint32_t posAA = MSC_BLOCK_SIZE - 1;  // 511
    // For each signature byte, see if it falls inside [offset, offset+bufsize).
    // The compiler will optimize this a lot.
    if (offset + bufsize > pos55 && offset < pos55) {
        buffer[pos55 - offset] = 0x55;
    }
    if (offset + bufsize > posAA && offset < posAA) {
        buffer[posAA - offset] = 0xAA;
    }
}

static void gen_extb_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize) {
    gen_zero_sector(lba, offset, buf, bufsize);
    gen_extb_sector_signature(offset, buf, bufsize);
}

static void gen_boot_sector(uint32_t lba __unused, uint32_t offset, void* buf, uint32_t bufsize) {
    uint8_t *base = (uint8_t *)buf;
    uint8_t *out = (uint8_t *)buf;
    uint32_t pos = offset;
    uint32_t remaining = bufsize;

    // 1) Copy from the static header bytes (first exfat_boot_sector_data_length bytes)
    if (pos < exfat_boot_sector_data_length) {
        uint32_t n = exfat_boot_sector_data_length - pos;
        if (n > remaining)
            n = remaining;
        memcpy(out, exfat_boot_sector_data + pos, n);
        out += n;
        pos += n;
        remaining -= n;
    }

    // 2) Insert VolumeSerialNumber bytes at offsets 100-103 if they fall in this slice
    const uint32_t serial_pos = 100; // byte offset for serial start
    if (offset <= serial_pos && offset + bufsize > serial_pos) {
        uint32_t serial = get_volume_serial_number();
        for (uint32_t i = 0; i < sizeof(uint32_t); i++) {
            uint32_t abs_pos = serial_pos + i;
            if (abs_pos >= offset && abs_pos < offset + bufsize) {
                base[abs_pos - offset] = (serial >> (8 * i)) & 0xFF;
            }
        }
    }

    // 3) Zero-fill any remaining bytes
    if (remaining > 0) {
        memset(out, 0, remaining);
    }

    // 4) Fill the sector signature bytes at the end of the buffer
    gen_extb_sector_signature(offset, buf, bufsize);
}

/**
 * Compute the VBR checksum at runtime over sectors 0–10.
 * Skips offsets 106, 107 and 112 in sector 0.
 */
static uint32_t compute_vbr_checksum_runtime_simple(void) {
    uint32_t sum = 0;
    uint8_t sector[MSC_BLOCK_SIZE];
    // Iterate each sector
    for (uint32_t lba = 0; lba < 11; ++lba) {
        // Generate sector data into 'sector' buffer via the MSC callback
        tud_msc_read10_cb(0, lba, 0, sector, MSC_BLOCK_SIZE);

        // Walk bytes
        for (uint32_t off = 0; off < MSC_BLOCK_SIZE; ++off) {
            if (lba == 0 && (off == 106 || off == 107 || off == 112)) {
                continue;
            }
            // Rotate right by one: ROR32(sum)
            sum = (sum >> 1) | (sum << 31);
            sum = (sum + sector[off]) & 0xFFFFFFFFu;
        }
    }
    return sum;
}

// Compute the VBR checksum at runtime using an optimized algorithm.
// This is a more efficient version that avoids the need to read sectors
// and instead computes the checksum directly from the volume serial number.
// It uses a compile-time prefix checksum and a suffix checksum, and rotates
// the checksum value based on the serial number bytes.
// This is based on the C++ implementation in vd_exfat.cpp.

// XXX FIXME: this function is not yet used in the code, as it does not
// pass the tests yet. It is a work in progress to replace the simple version
// of the checksum computation with a more efficient one that does not require
// reading the sectors at runtime. The goal is to optimize the VBR checksum
// computation to avoid unnecessary reads and improve performance.
static uint32_t compute_vbr_checksum_runtime_optimised(void) {
    // Phase 1: start from compile-time prefix checksum
    uint32_t sum = EXFAT_VBR_CHECKSUM_PREFIX;

    uint32_t serial = get_volume_serial_number();

    // Phase 2: rotate and add each byte of the serial number
    // Let the compiler optimize this loop
    for (int i = 0; i < 4; i++) {
        // Rotate right 32-bit by one bit, then add the next byte of the serial
        // This is equivalent to: sum = ror32(sum) + ((serial >> (8 * i)) & 0xFF);
        // but avoids the need for a separate ror32 function.
        sum = (sum >> 1) | (sum << 31);
        sum = (sum + ((serial >> (8 * i)) & 0xFF)) & 0xFFFFFFFFu;
    }

    // Phase 3: apply suffix rotation and suffix checksum
    // To understand this, you need to understand the math
    // Otherwise this will feel like magic.
    sum = (sum >> EXFAT_VBR_SUFFIX_ROT) | (sum << (32 - EXFAT_VBR_SUFFIX_ROT));
    sum = (sum + EXFAT_VBR_CHECKSUM_SUFFIX) & 0xFFFFFFFFu;
}

static uint32_t compute_vbr_checksum_runtime(void) {
    // This is the main entry point for computing the VBR checksum at runtime.
    // XXX FIXME: switch over to the optimized version of the checksum computation,
    // once we have verified that it works correctly.
    return compute_vbr_checksum_runtime_simple();
}


static void gen_cksm_sector(uint32_t lba __unused, uint32_t offset, void* buffer, uint32_t bufsize) {
    // For the math, see the C++ source file vd_exfat.cpp

    // Sanity-check slice bounds for checksum sector
    assert(offset < MSC_BLOCK_SIZE);
    assert(bufsize <= MSC_BLOCK_SIZE - offset);

    static bool checksum_cached = false;
    static uint32_t checksum_value = 0;

    if (!checksum_cached) {
        // Cache the result for future calls
        checksum_cached = true;
        checksum_value = compute_vbr_checksum_runtime();
    }

    // Fill requested slice of sector 11 with the 32-bit checksum pattern
    uint8_t  *base8  = ((uint8_t *)buffer);
    // Fallback: byte-wise pattern respecting absolute byte positions
    for (uint32_t i = 0; i < bufsize; ++i) {
        uint32_t abs_pos = offset + i;
        uint32_t byte_index = abs_pos & 3;
        base8[i] = (checksum_value >> (8 * byte_index)) & 0xFF;
    }
}

static void gen_fat0_sector(uint32_t lba __unused,
                            uint32_t offset,
                            void*    buffer,
                            uint32_t bufsize)
{
    // Zero-fill the buffer
    memset(buffer, 0, bufsize);
    const uint8_t* data = (const uint8_t*)exfat_fat0_sector_data;

    // Copy the precomputed FAT0 beginning entries
    if (offset < exfat_fat0_sector_data_len) {
        size_t copy_len = exfat_fat0_sector_data_len - offset;
        if (copy_len > bufsize) copy_len = bufsize;
        memcpy(buffer, data + offset, copy_len);
    }
}

static void gen_upcs_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize)
{
    // Ensure buffer and offsets are 16-bit aligned
    assert(((uintptr_t)buf & 1) == 0);
    assert((offset & 1) == 0);
    assert((bufsize & 1) == 0);
    assert(lba >= EXFAT_UPCASE_TABLE_START_LBA);

    // Compute base index for this sector
    const size_t   words_per_sector = EXFAT_BYTES_PER_SECTOR / sizeof(uint16_t);
    const uint32_t sector_index     = lba - EXFAT_UPCASE_TABLE_START_LBA;
    const uint32_t base_index       = sector_index * words_per_sector;
    const uint32_t start_word       = offset / sizeof(uint16_t);
    const uint32_t word_count       = bufsize / sizeof(uint16_t);

    assert(base_index + start_word + word_count <= EXFAT_UPCASE_TABLE_LENGTH_SECTORS * words_per_sector);

    // Prepare word-based pointers and counts
    uint16_t* out        = (uint16_t*)buf;
    const uint16_t* table = exfat_upcase_table;

    for (uint32_t i = 0; i < word_count; ++i) {
        uint32_t idx = base_index + start_word + i;
        uint16_t value;
        if (idx < exfat_upcase_table_len / sizeof(exfat_upcase_table[0])) {
            value = exfat_upcase_table[idx];
        } else {
            // Identity mapping or zero if compressed table
            value = (EXFAT_UPCASE_TABLE_COMPRESSED? 0: (uint16_t)idx);
        }
        out[i] = value;
    }
}

// Handler for the dynamic area: looks up the cluster map and calls the file handler
static void vd_dynamic_area_handler(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize) {
    // Compute cluster number from LBA
    uint32_t cluster = ((lba - EXFAT_CLUSTER_HEAP_START_LBA) / EXFAT_SECTORS_PER_CLUSTER) + EXFAT_CLUSTER_HEAP_START_CLUSTER;
    uint32_t cluster_offset = (lba % EXFAT_SECTORS_PER_CLUSTER) * EXFAT_BYTES_PER_SECTOR + offset;
    for (size_t i = 0; i < dynamic_cluster_map_count; ++i) {
        const dynamic_cluster_map_entry_t* entry = &dynamic_cluster_map[i];
        size_t clusters_allocated = (entry->file_size_bytes + (EXFAT_BYTES_PER_SECTOR * EXFAT_SECTORS_PER_CLUSTER) - 1) / (EXFAT_BYTES_PER_SECTOR * EXFAT_SECTORS_PER_CLUSTER);
        if (cluster >= entry->first_cluster && cluster < entry->first_cluster + clusters_allocated) {
            // Compute file-relative offset
            uint32_t file_offset = (cluster - entry->first_cluster) * (EXFAT_BYTES_PER_SECTOR * EXFAT_SECTORS_PER_CLUSTER) + cluster_offset;
            // Clamp to file size
            if (file_offset < entry->file_size_bytes) {
                size_t to_copy = bufsize;
                if (file_offset + to_copy > entry->file_size_bytes) {
                    to_copy = entry->file_size_bytes - file_offset;
                }
                entry->handler(file_offset, buf, to_copy);
                if (to_copy < bufsize) {
                    memset((uint8_t*)buf + to_copy, 0, bufsize - to_copy);
                }
                return;
            } else {
                // Out of file bounds: zero-fill
                memset(buf, 0, bufsize);
                return;
            }
        }
    }
    // Not found: zero-fill
    memset(buf, 0, bufsize);
}

// Read10 callback: serve LBA regions defined in the lba_regions table
// Called from the TinyUSB MSC stack when a READ10 command is issued.
int32_t vd_virtual_disk_read(uint32_t lba,
                             uint32_t offset,
                             void*    buffer,
                             uint32_t bufsize)
{
    // Check LBA against the region table
    for (size_t i = 0; i < sizeof(lba_regions) / sizeof(lba_region_t); i++) {
        if (lba < lba_regions[i].next_lba) {
            lba_regions[i].handler(lba, offset, buffer, bufsize);
            return bufsize; // Return full sector size
        }
    }
    // Fallback for other LBAs: zero-filled
    memset(buffer, 0, bufsize);
    return bufsize;
}

int vd_add_file(vd_dynamic_file_t* file) {
    // If the file has no first cluster defined, allocate cluster chain
    if (file->first_cluster == 0) {
        file->first_cluster = vd_dynamic_cluster_alloc(file->size_bytes, file->get_content);
        if (file->first_cluster == 0) {
            return -1;
        }
    }
    vd_exfat_dir_add_file(file);
    return 0;
}

int vd_update_file(vd_dynamic_file_t *file, size_t size_bytes) {
    if (size_bytes > file->size_bytes) {
        const int32_t cluster_size_bytes = EXFAT_BYTES_PER_SECTOR * EXFAT_SECTORS_PER_CLUSTER;
        const int32_t clusters_needed    = (      size_bytes + cluster_size_bytes - 1) / cluster_size_bytes;
        const int32_t clusters_allocated = (file->size_bytes + cluster_size_bytes - 1) / cluster_size_bytes;
        const int32_t clusters_to_add    = clusters_needed - clusters_allocated;
        if (clusters_to_add > 0) {
            if (file->first_cluster + clusters_allocated == dynamic_cluster_map_next_cluster &&
                dynamic_cluster_map_next_cluster + clusters_to_add <= PICOVD_DYNAMIC_AREA_END_CLUSTER) {
                // We are at the end of the dynamic cluster map and we can allocate new clusters.
                dynamic_cluster_map_next_cluster += clusters_to_add;
            } else {
                return -1;
            }
        }
    }
    file->size_bytes = size_bytes;
    vd_exfat_dir_update_file(file, true);
    return 0;
}
