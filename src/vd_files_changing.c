#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include <picovd_config.h>
#include "vd_exfat_params.h"
#include "vd_virtual_disk.h"
#include "vd_exfat.h"
#include "vd_exfat_dirs.h"
#include "pico/time.h"

static const char* FORMAT_STRING = "%02d:%02d:%02d: LBA=%u, off=%u, len=%u\n";

void vd_return_changing_file_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize) {
    assert(lba == PICOVD_CHANGING_FILE_START_LBA);

    absolute_time_t now = get_absolute_time();

    uint64_t us = to_us_since_boot(now);
    uint32_t total_s = us / 1000000;
    uint32_t hours = total_s / 3600;
    uint32_t mins  = (total_s / 60) % 60;
    uint32_t secs  = total_s % 60;

    int len = sprintf((char*)buffer, FORMAT_STRING,
                      hours, mins, secs,
                      lba, offset, bufsize);
}

bool files_changing_build_file_partition_entry_set(uint32_t slot_idx __unused, exfat_root_dir_entries_dynamic_file_t *des) {
    // This function builds a dynamic entry set for the changing file.
    memset(des, 0x00, sizeof(*des));

    // (1) Prepare the file directory entry
    des->file_directory.entry_type = exfat_entry_type_file_directory;
    des->file_directory.secondary_count = 2 /* XXX FIXME */;
    des->file_directory.file_attributes = EXFAT_FILE_ATTR_READ_ONLY;

    // XXX FIXME: Not RTC, as it should be
    absolute_time_t now = get_absolute_time();

    uint64_t us = to_us_since_boot(now);
    uint32_t total_s = us / 1000000;
    uint32_t hours = total_s / 3600;
    uint32_t mins  = (total_s / 60) % 60;
    uint32_t secs  = total_s % 60;

    // Set all timestamps to Jan 1, 2025 at current uptime time-of-day
    uint32_t timestamp = exfat_make_timestamp(2025, 1, 1, hours, mins, secs);
    des->file_directory.creat_time       = timestamp;
    des->file_directory.last_mod_time    = timestamp;
    des->file_directory.last_acc_time    = timestamp;

    // No 10 ms increments
    des->file_directory.creat_time_ms    = 0;
    des->file_directory.last_mod_time_ms = 0;
    // Mark UTC offset valid (0 minutes)
    des->file_directory.creat_time_off    = exfat_utc_offset_UTC;
    des->file_directory.last_mod_time_off = exfat_utc_offset_UTC;
    des->file_directory.last_acc_time_off = exfat_utc_offset_UTC;

    // (2) Prepare the stream extension entry
    des->stream_extension.entry_type = exfat_entry_type_stream_extension;
    des->stream_extension.secondary_flags = 0x03;   // always 'valid data length' + 'no FAT'
    des->stream_extension.name_length       = PICOVD_CHANGING_FILE_NAME_LEN;
    des->stream_extension.valid_data_length = PICOVD_CHANGING_FILE_SIZE_BYTES;
    des->stream_extension.data_length       = PICOVD_CHANGING_FILE_SIZE_BYTES;
    des->stream_extension.first_cluster     = PICOVD_CHANGING_FILE_START_CLUSTER;

    // (3) Prepare the file name entries
    const char16_t name[] = PICOVD_CHANGING_FILE_NAME;

    des->file_name[0].entry_type = exfat_entry_type_file_name;
    memcpy(des->file_name[0].file_name, name, PICOVD_CHANGING_FILE_NAME_LEN * sizeof(char16_t));

    // Mark extra entries as unused
    for (size_t i = 1; i < sizeof(des->file_name) / sizeof(des->file_name[0]); ++i) {
        des->file_name[i].entry_type = exfat_entry_type_unused;
    }

    // (4) Compute SetChecksum and store it (bytes 2–3 of primary entry)
    uint16_t checksum = exfat_dirs_compute_setchecksum(
                            (const uint8_t *)des,
                            (size_t)(3 * 32)); // XXX FIXME
    des->file_directory.set_checksum = checksum;

    return true;
}
