#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <pico/time.h>

#include "picovd_config.h"

#include "vd_virtual_disk.h"
#include "vd_exfat_params.h"
#include "vd_exfat.h"
#include "vd_exfat_dirs.h"

static void changing_file_content_cb(uint32_t offset, void* buffer, uint32_t bufsize) {

    absolute_time_t now = get_absolute_time();

    uint64_t us = to_us_since_boot(now);
    uint32_t total_s = us / 1000000;
    uint32_t hours = total_s / 3600;
    uint32_t mins  = (total_s / 60) % 60;
    uint32_t secs  = total_s % 60;

    static const char* FORMAT_STRING = "%02d:%02d:%02d: off=%u, len=%u\n";
    int len = snprintf((char*)buffer, bufsize, FORMAT_STRING,
                      hours, mins, secs,
                      offset, bufsize);
}

static vd_dynamic_file_t changing_file = {
    .name            = PICOVD_CHANGING_FILE_NAME,
    .name_length     = sizeof(PICOVD_CHANGING_FILE_NAME) / sizeof(char16_t) - 1, // Exclude NUL
    .file_attributes = FAT_FILE_ATTR_READ_ONLY,
    .first_cluster   = 0, // Allocated at runtime
    .size_bytes      = PICOVD_CHANGING_FILE_SIZE_BYTES,
    .creat_time_sec  = 0,
    .mod_time_sec    = 0,
    .get_content     = changing_file_content_cb,
};

// Initialization function to register the file at runtime
void vd_files_changing_init(void) {
#if PICOVD_CHANGING_FILE_ENABLED
    vd_add_file(&changing_file);
#endif
}
