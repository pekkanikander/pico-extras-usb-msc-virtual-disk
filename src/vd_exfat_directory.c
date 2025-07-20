/**
 * @file src/vd_exfat_directory.c
 * @brief USB MSC “virtual disk” interface stubs for PicoVD.
 */

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "pico/bootrom.h"    // get_partition_table_info()

#include <tusb.h>
#include "tusb_config.h"     // for CFG_TUD_MSC_EP_BUFSIZE

#include "picovd_config.h"

#include "vd_virtual_disk.h"
#include "vd_exfat_params.h"
#include "vd_exfat.h"
#include "vd_exfat_dirs.h"


// ---------------------------------------------------------------------------
// Directory set SetChecksum computation
/// See Microsoft spec §6.3.3 “SetChecksum Field” (Figure 2)
// ---------------------------------------------------------------------------
uint16_t exfat_dirs_compute_setchecksum(const uint8_t *entries, size_t len) {
    uint16_t sum = 0;
    for (int i = 0; i < len; i++) {
        if (i == 2 || i == 3)
           continue;
        sum = ((sum & 0x0001)? 0x8000 : 0) + (sum >> 1) + (uint16_t)(entries[i]);
    }
    return sum;
}

// ---------------------------------------------------------------------------
// Directory entry sets for the root directory.
//
// The SetChecksums computed lazily the first time an entry set
//
// To minimise runtime SRAM use, we don't store the root directory as a single
// runtime buffer but as a series of entry sets, pointed by this table.
// This allows us to compute the assemble the directory sector contents
// relatively quicly at runtime.  An alternative would be to use a separate
// linker segment for the entry sets, thereby making them continuous.
// However, that would not be easy.  Firstly, computing the SetChecksums at
// compile time turned out to be quite complicated, requiring C++20.
// Secondly, for we have both compile-time generated and run-time generated
// entry sets.  The current code allows them to be both handled by this
// same code.
//
// As long as the number of entry sets is relatively low, in the order
// of a few tens at most, having this whole table in SRAM takes at most
// a few hundred bytes.
// ---------------------------------------------------------------------------
static struct {
    const uint8_t * entries;  ///< Pointer to the entry set buffer, in code
    // XXX RECONSIDER: We could store an uint8_t of how many entries in a set, reducing the memory consumption a bit
    size_t entries_length;   ///< Length of the entry set buffer, in bytes
    bool checksum_computed;
    uint16_t checksum;        ///< Runtime lazily compiled SetChecksum
} exfat_root_dir_entries[] = {
    { (uint8_t*)&exfat_root_dir_first_entries_data, sizeof(exfat_root_dir_first_entries_data) },
#if PICOVD_SRAM_ENABLED
    { (uint8_t*)&exfat_root_dir_sram_file_data, sizeof(exfat_root_dir_sram_file_data) },
#endif
#if PICOVD_BOOTROM_ENABLED
    { (uint8_t*)&exfat_root_dir_bootrom_file_data, sizeof(exfat_root_dir_bootrom_file_data) },
#endif
#if PICOVD_FLASH_ENABLED
    { (uint8_t*)&exfat_root_dir_flash_file_data, sizeof(exfat_root_dir_flash_file_data) },
#endif
};

static_assert(sizeof(exfat_root_dir_first_entries_data)
            + sizeof(exfat_root_dir_sram_file_data)
            + sizeof(exfat_root_dir_bootrom_file_data)
            + sizeof(exfat_root_dir_flash_file_data)
            <= EXFAT_BYTES_PER_SECTOR,
              "Compile time root directory entries must fit into a single sector");

// ---------------------------------------------------------------------------
// Generate a slice of a root directory sector, as requested by the MSC layer.
// ---------------------------------------------------------------------------
void exfat_generate_root_dir_fixed_sector(uint32_t __unused lba, uint32_t offset, void* buffer, uint32_t bufsize) {

    assert(lba == EXFAT_ROOT_DIR_START_LBA);

    uint8_t *buf = (uint8_t *)buffer; // Current place to copy
    size_t   len = bufsize;           // Remaining bytes to copy
    size_t   idx = 0;                 // Current index within the sector

    for (size_t i = 0; i < sizeof(exfat_root_dir_entries) / sizeof(exfat_root_dir_entries[0]); i++) {
        const uint8_t * entries_data = exfat_root_dir_entries[i].entries;
        const size_t    entries_len  = exfat_root_dir_entries[i].entries_length;

        // Step 1: Entries preparation, if not already prepared
        if (!exfat_root_dir_entries[i].checksum_computed) {
            // Compute checksum for the current entry
            exfat_root_dir_entries[i].checksum
              = exfat_dirs_compute_setchecksum(entries_data, entries_len);
            exfat_root_dir_entries[i].checksum_computed = true;
        }
        const uint16_t checksum = exfat_root_dir_entries[i].checksum;

        // Step 2: If the requested slice is after the current entries, skip to next ones
        if (offset >= idx + entries_len) {
            idx += entries_len;
            continue;
        }

        // Phase 3: Copy data from the current entries to the buffer
        // If some part of the entries falls before the requested slice,
        // start copying only at within the entries as needed.
        // We have copied idx bytes so far. Hence, the offset within the entries
        // is offset - idx, or the number of bytes to skip in the beginning
        // of this entry set. If this is negative, we start copying from the beginning.
        const size_t   entries_offset = (offset > idx? offset - idx: 0);
        assert(entries_offset < entries_len);
        const uint8_t *copy_from      = entries_data + entries_offset;
        size_t         copy_len       = entries_len  - entries_offset;

        if (copy_len > len)
            copy_len = len;

        memcpy(buf, copy_from, copy_len);

        // Phase 3: Place the SetChecksum, if needed and if it is part of the copied data,
        // at indices 2 and 3 as measured from the beginning of the entries
        if (entries_data[0] == exfat_entry_type_file_directory ||
            entries_data[0] == exfat_entry_type_volume_guid) {
            if (entries_offset <= 2 && copy_len > 2 - entries_offset) {
                buf[2 - entries_offset] =  checksum       & 0xFF;
            }
            if (entries_offset <= 3 && copy_len > 3 - entries_offset) {
                buf[3 - entries_offset] = (checksum >> 8) & 0xFF;
            }
        }

        // Phase 4: Advance for the next entry (if any)
        buf += copy_len;
        len -= copy_len;
        idx += entries_offset + copy_len;

        assert(buf >= ((uint8_t *)buffer) && buf <= ((uint8_t *)buffer) + bufsize);
        assert(len <= bufsize);
        assert(idx <= EXFAT_BYTES_PER_SECTOR);

        // If the buffer is full, stop
        if (len == 0)
            break;
    }

    // Mark the rest of the buffer as unused
    memset(buf, exfat_entry_type_unused, len);
}

// ---------------------------------------------------------------------------
// Generate a slice of a root directory sector, as requested by the MSC layer.
// This function is used for dynamically generated entries, mainly partitions.
// ---------------------------------------------------------------------------

// Buffer for a dynamically generated entry set. Cleared on each call.
static exfat_root_dir_entries_dynamic_file_t directory_entry_set_buffer;

#ifndef __INTELLISENSE__
_Static_assert(sizeof(exfat_root_dir_entries_dynamic_file_t) % CFG_TUD_MSC_EP_BUFSIZE == 0,
              "Dynamic entry-set must be a multiple of MSC EP buffer size");
#endif

// ---------------------------------------------------------------------------
// Helper: assemble a 512‑byte root‑dir slot for partition `part_idx`
// ---------------------------------------------------------------------------
static bool build_rp2350_partition_entry_set(uint32_t part_idx, exfat_root_dir_entries_dynamic_file_t *des) {
    // 1. Query BootROM for LOCATION/FLAGS and NAME of this single partition.
    enum {
        PT_LOCATION_AND_FLAGS = 0x0010,
        PT_NAME               = 0x0080,
        PT_SINGLE_PARTITION   = 0x8000,
    };

    static uint32_t pt_buf[34]; // plenty for location/flags + long name (max 32 words)
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

    uint32_t *p   = pt_buf + 1;          // skip supported‑flags word
    uint32_t loc  = *p++;                // permissions_and_location
    uint32_t flg  = *p++;                // permissions_and_flags

    // Extract start address and length (see §5.9.4.2 of the datasheet)
    uint32_t flash_page  =  (loc & 0x0001FFFFu);   // 4‑kB units, matching cluster size
    uint32_t flash_size  = ((loc & 0x03ffe000u) >> 17) * 4096u;

    // NAME field
    uint8_t  name_len   = (*(uint8_t *)p) & 0x7F;
    const uint8_t *name_bytes = ((const uint8_t *)p) + 1;     // ASCII/UTF‑8
    uint8_t n_fname = (uint8_t)((name_len + 14) / 15);        // ceil(len/15) XXX FIXME

    memset(des, 0, sizeof(*des));

    des->file_directory.entry_type  = exfat_entry_type_file_directory;
    des->file_directory.file_attributes = EXFAT_FILE_ATTR_READ_ONLY;
    des->file_directory.secondary_count = 1 /* XXX FIXME */ + n_fname;

    des->stream_extension.entry_type    = exfat_entry_type_stream_extension;
    des->stream_extension.secondary_flags = 0x03;   // always 'valid data length' + 'no FAT'
    des->stream_extension.name_length       = name_len;
    des->stream_extension.valid_data_length = (uint64_t)flash_size;
    des->stream_extension.data_length       = (uint64_t)flash_size;
    // First cluster calculation: clusters start at 2
    des->stream_extension.first_cluster = flash_size? flash_page + PICOVD_FLASH_START_CLUSTER : 0;

    // Expand the name to UTF‑16LE
    assert(name_len <= 127);
    static char16_t file_name[127/*XXX FIXME*/];
    for (size_t i = 0; i < name_len; i++) {
        file_name[i] = name_bytes[i]; // UTF-8 to UTF-16LE
    }

    des->stream_extension.name_hash
        = exfat_dirs_compute_name_hash(file_name, name_len);

    // (3) File‑Name secondary entries (UTF‑16LE, padded with 0x0000)
    for (uint8_t i = 0; i < n_fname; i++) {
        exfat_file_name_dir_entry_t *fn = &des->file_name[i];
        fn->entry_type = exfat_entry_type_file_name;
        uint8_t copy = (uint8_t)((name_len > 15) ? 15 : name_len);
        for (uint8_t j = 0; j < copy; ++j) {
            fn->file_name[j] = name_bytes[i * 15 + j];
        }
        name_len -= copy;
    }

    // Mark extra entries as unused
    for (uint8_t i = n_fname; i < sizeof(des->file_name)/sizeof(des->file_name[0]); i++) {
        des->file_name[i].entry_type = exfat_entry_type_unused;
    }

    return true;
}

typedef bool (*build_partition_entry_set_t)(uint32_t slot_idx, exfat_root_dir_entries_dynamic_file_t  *des);

static build_partition_entry_set_t build_partition_entry_set_table[] = {
#if PICOVD_BOOTROM_PARTITIONS_ENABLED
    build_rp2350_partition_entry_set, // Slot 0
    build_rp2350_partition_entry_set, // Slot 1
    build_rp2350_partition_entry_set, // Slot 2
    build_rp2350_partition_entry_set, // Slot 3
    build_rp2350_partition_entry_set, // Slot 4
    build_rp2350_partition_entry_set, // Slot 5
    build_rp2350_partition_entry_set, // Slot 6
    build_rp2350_partition_entry_set, // Slot 7
#endif
#if PICOVD_CHANGING_FILE_ENABLED
    files_changing_build_file_partition_entry_set, // Slot agnostic
#endif
    // Add more slots here if needed, e.g. for other partitions
};

static int32_t  current_slot_idx = -1;  ///< partition index currently in slot_buf

// ---------------------------------------------------------------------------
// Generate a slice of a *dynamic* root-directory sector
// ---------------------------------------------------------------------------
void exfat_generate_root_dir_dynamic_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize) {

    assert(lba > EXFAT_ROOT_DIR_START_LBA &&
           lba < EXFAT_ROOT_DIR_START_LBA + EXFAT_ROOT_DIR_LENGTH_SECTORS);
    assert(bufsize <= EXFAT_BYTES_PER_SECTOR);
    assert(offset  < EXFAT_BYTES_PER_SECTOR);

    // slot 0 starts at (EXFAT_ROOT_DIR_START_LBA + 1)
    uint32_t slot_idx = lba - EXFAT_ROOT_DIR_START_LBA - 1u;

    // (1) build / fetch cached slot
    if (slot_idx < sizeof(build_partition_entry_set_table)/sizeof(build_partition_entry_set_table[0])) {
        if (slot_idx != current_slot_idx) {
            bool ok = build_partition_entry_set_table[slot_idx](slot_idx, &directory_entry_set_buffer);
            if (ok) {
                //  Compute SetChecksum and store it (bytes 2–3 of primary entry)
                // XXX FUTURE: Considern caching these and recomputing only when needed.
                const uint16_t checksum = exfat_dirs_compute_setchecksum(
                    (const uint8_t *)&directory_entry_set_buffer,
                    (size_t)((1 + directory_entry_set_buffer.file_directory.secondary_count) * 32));
                directory_entry_set_buffer.file_directory.set_checksum = checksum;
                current_slot_idx = slot_idx;
            } else {
                current_slot_idx = -1;
            }
        }
    } else {
        current_slot_idx = -1; // No valid partition entry
    }

    // (2) copy the requested slice. XXX HACK: We assume maybe too much,
    // but we do static_assert that the entry set is a multiple of CFG_TUD_MSC_EP_BUFSIZE bytes.
    if (current_slot_idx >= 0 && offset < sizeof(directory_entry_set_buffer)) {
        memcpy(buffer, ((uint8_t *)&directory_entry_set_buffer) + offset, bufsize);
    } else {
        memset(buffer, exfat_entry_type_unused, bufsize); // Fill with unused entries
    }
}
