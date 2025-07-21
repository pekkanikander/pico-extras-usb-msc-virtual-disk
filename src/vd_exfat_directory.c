/**
 * @file src/vd_exfat_directory.c
 * @brief USB MSC “virtual disk” interface stubs for PicoVD.
 */

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <tusb.h>
#include "tusb_config.h"     // for CFG_TUD_MSC_EP_BUFSIZE

#include "picovd_config.h"

#include "vd_virtual_disk.h"
#include "vd_exfat_params.h"
#include "vd_exfat.h"
#include "vd_exfat_dirs.h"
#include "vd_files_rp2350.h"


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
// Dynamic files management
// ---------------------------------------------------------------------------

#ifndef PICOVD_PARAM_MAX_DYNAMIC_FILES
#define PICOVD_PARAM_MAX_DYNAMIC_FILES 12
#endif

typedef struct {
    const vd_dynamic_file_t* file;
    uint16_t         name_hash;
} dynamic_file_entry_t;

static dynamic_file_entry_t dynamic_files[PICOVD_PARAM_MAX_DYNAMIC_FILES];
static size_t dynamic_file_count = 0;

// Add a dynamic file, returns index or -1 if full
int vd_exfat_dir_add_file(const vd_dynamic_file_t* file) {
    if (dynamic_file_count >= PICOVD_PARAM_MAX_DYNAMIC_FILES) return -1;
    dynamic_files[dynamic_file_count].file      = file;
    dynamic_files[dynamic_file_count].name_hash = exfat_dirs_compute_name_hash(file->name, file->name_length);
    vd_exfat_dir_update_file(file, false);
    return (int)dynamic_file_count++;
}

int vd_exfat_dir_update_file(vd_dynamic_file_t* file, bool update_disk) {
    absolute_time_t now = get_absolute_time();
    uint64_t us = to_us_since_boot(now);
    uint32_t secs = us / 1000000;

    file->mod_time_sec = secs;
    if (update_disk) {
        vd_virtual_disk_contents_changed(false);
    }
    return 0;
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

// Change build_file_entry_set to take a vd_file_t pointer directly
static bool build_file_entry_set(const vd_dynamic_file_t *file, exfat_root_dir_entries_dynamic_file_t *des) {
    assert(file != NULL);
    memset(des, 0x00, sizeof(*des));

    // (1) Prepare the file directory entry
    des->file_directory.entry_type = exfat_entry_type_file_directory;
    des->file_directory.secondary_count = 2; // 1 stream + 1 name entry, to be adjusted if needed)
    des->file_directory.file_attributes = file->file_attributes;

    // Set timestamps (convert from time_t to exFAT format)
    // For now, use fixed date/time if not set
    const time_t t = file->creat_time_sec;
    struct tm tm;
    gmtime_r(&t, &tm);
    const uint32_t creat_timestamp = exfat_make_timestamp(
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    des->file_directory.creat_time    = creat_timestamp;
    // Use the file's modification time for last_mod_time, not creation time
    const time_t mod_t = file->mod_time_sec;
    struct tm mod_tm;
    gmtime_r(&mod_t, &mod_tm);
    const uint32_t mod_timestamp = exfat_make_timestamp(
        mod_tm.tm_year + 1900, mod_tm.tm_mon + 1, mod_tm.tm_mday,
        mod_tm.tm_hour, mod_tm.tm_min, mod_tm.tm_sec);
    des->file_directory.last_mod_time = mod_timestamp;
    des->file_directory.last_acc_time = mod_timestamp;
    des->file_directory.creat_time_off    = exfat_utc_offset_UTC;
    des->file_directory.last_mod_time_off = exfat_utc_offset_UTC;
    des->file_directory.last_acc_time_off = exfat_utc_offset_UTC;

    // (2) Prepare the stream extension entry
    des->stream_extension.entry_type = exfat_entry_type_stream_extension;
    des->stream_extension.secondary_flags = 0x03;   // always 'valid data length' + 'no FAT' XXX FIXME
    des->stream_extension.name_length = file->name_length;
    des->stream_extension.valid_data_length = file->size_bytes;
    des->stream_extension.data_length = file->size_bytes;
    des->stream_extension.first_cluster = file->first_cluster;
    des->stream_extension.name_hash = exfat_dirs_compute_name_hash(file->name, file->name_length);

    // (3) Prepare the file name entries (only one for now)
    des->file_name[0].entry_type = exfat_entry_type_file_name;
    memcpy(des->file_name[0].file_name, file->name, file->name_length * sizeof(char16_t));
    // Mark extra entries as unused
    for (size_t i = 1; i < sizeof(des->file_name) / sizeof(des->file_name[0]); ++i) {
        des->file_name[i].entry_type = exfat_entry_type_unused;
    }
    return true;
}

static int32_t  current_slot_idx = -1;  ///< partition index currently in slot_buf

// ---------------------------------------------------------------------------
// Generate a slice of a *dynamic* root-directory sector
// ---------------------------------------------------------------------------
void exfat_generate_root_dir_dynamic_sector(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize) {

    assert(lba > EXFAT_ROOT_DIR_START_LBA &&
           lba < EXFAT_ROOT_DIR_START_LBA + EXFAT_ROOT_DIR_LENGTH_SECTORS);
    assert(bufsize <= EXFAT_BYTES_PER_SECTOR);
    assert(offset   < EXFAT_BYTES_PER_SECTOR);

    // slot 0 starts at (EXFAT_ROOT_DIR_START_LBA + 1)
    uint32_t slot_idx = lba - EXFAT_ROOT_DIR_START_LBA - 1u;

    bool ok = false;
    if (slot_idx < dynamic_file_count) {
        ok = build_file_entry_set(dynamic_files[slot_idx].file, &directory_entry_set_buffer);
    } else {
        ok = false;
    }
    if (ok) {
        const uint16_t checksum = exfat_dirs_compute_setchecksum(
            (const uint8_t *)&directory_entry_set_buffer,
            (size_t)((1 + directory_entry_set_buffer.file_directory.secondary_count) * 32));
        directory_entry_set_buffer.file_directory.set_checksum = checksum;
        current_slot_idx = slot_idx;
    } else {
        current_slot_idx = -1;
    }

    // Copy the requested slice. XXX HACK: We assume maybe too much,
    // but we do static_assert that the entry set is a multiple of CFG_TUD_MSC_EP_BUFSIZE bytes.
    if (current_slot_idx >= 0 && offset < sizeof(directory_entry_set_buffer)) {
        memcpy(buf, ((uint8_t *)&directory_entry_set_buffer) + offset, bufsize);
    } else {
        memset(buf, exfat_entry_type_unused, bufsize); // Fill with unused entries
    }
}
