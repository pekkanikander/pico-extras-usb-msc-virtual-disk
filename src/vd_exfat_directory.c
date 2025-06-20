/**
 * @file src/vd_exfat_directory.c
 * @brief USB MSC “virtual disk” interface stubs for PicoVD.
 */

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <tusb.h>

#include "picovd_config.h"

#include "vd_exfat_params.h"
#include "vd_exfat.h"
#include "vd_exfat_dirs.h"

// ---------------------------------------------------------------------------
// Directory set SetChecksum computation
/// See Microsoft spec §6.3.3 “SetChecksum Field” (Figure 2)
// ---------------------------------------------------------------------------
static uint16_t exfat_dirs_compute_setchecksum(const uint8_t *entries, size_t len) {
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
    // XXX: FIXME: Add space for runtime-generated entries
};

// ---------------------------------------------------------------------------
// Generate a slice of a root directory sector, as requested by the MSC layer.
// ---------------------------------------------------------------------------
void exfat_generate_root_dir_sector(uint32_t __unused lba, void* buffer, uint32_t offset, uint32_t bufsize) {

    assert(lba == EXFAT_ROOT_DIR_START_LBA); // XXX FIXME: Remove me, we need to support more sectors than just one

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

    // Zero the rest of the requested buffer, if any
    memset(buf, 0, len);
}
