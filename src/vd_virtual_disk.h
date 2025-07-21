
#ifndef VD_VIRTUAL_DISK_H
#define VD_VIRTUAL_DISK_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include <wchar.h>

#ifdef __cplusplus
  // C++11 and later: char16_t is a built-in type
#else
  typedef uint16_t char16_t;
#endif


/// FAT/exFAT File Directory Entry attribute bits (Table 28)
typedef enum  {
    FAT_FILE_ATTR_READ_ONLY  = 0x0001,  ///< read-only file
    FAT_FILE_ATTR_HIDDEN     = 0x0002,  ///< hidden file
    FAT_FILE_ATTR_SYSTEM     = 0x0004,  ///< system file
    FAT_FILE_ATTR_ARCHIVE    = 0x0020,  ///< archive bit
    FAT_FILE_ATTR_MAX        = 0xFFFF,  ///< force 2 byte value
} fat_file_attr_t;

// Function pointer type for LBA region handlers: fetch or generate bufsize number of bytes
// at the given LBA + offset into the provided buffer.
typedef void (*usb_msc_lba_read10_fn_t)(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
typedef void (*vd_file_sector_get_fn_t)(uint32_t offset, void* buf, uint32_t bufsize);

// ---------------------------------------------------------------
// Virtual Disk File Structures
// ---------------------------------------------------------------

// Dynamic file structure: may be changed at runtime
typedef struct __packed {
    const char16_t *   name;            // Pointer to UTF-16LE file name
    uint8_t            name_length;     // Name length, in UTF-16 code units
    fat_file_attr_t    file_attributes; // FAT/exFAT file attributes
    uint16_t           first_cluster;   // First cluster number
    size_t             size_bytes;      // File size in bytes
    time_t             creat_time_sec;  // Creation time in seconds, Unix epoch (since 1.1.1970)
    time_t             mod_time_sec;    // Modification time in seconds
    vd_file_sector_get_fn_t get_content;
} vd_dynamic_file_t;

#define STR_UTF16_EXPAND(x) u ## #x
#define PICOVD_DEFINE_FILE_RUNTIME(struct_name, file_name, file_size_bytes, get_content_cb) \
    vd_dynamic_file_t struct_name = { \
        .name = STR_UTF16_EXPAND(file_name), \
        .name_length = (sizeof(file_name) / sizeof(char16_t)) - 1, \
        .file_attributes = FAT_FILE_ATTR_READ_ONLY, \
        .first_cluster = 0, \
        .size_bytes = file_size_bytes, \
        .creat_time_sec = 0, \
        .mod_time_sec = 0, \
        .get_content = get_content_cb, \
    };

// Static file structure: fixed at compile time

typedef struct vd_static_file_s vd_static_file_t; // Opaque static file type

// Compile-time directory entries for static files
#define PICOVD_DEFINE_FILE_STATIC(struct_name, file_name_str, file_size_bytes) \
    const vd_static_file_t struct_name = { \
        .file_dir_entry = { \
            .entry_type = exfat_entry_type_file_directory, \
            .secondary_count = 2u, \
            .set_checksum = 0u, /* Computed lazily at runtime */ \
            .file_attributes = FAT_FILE_ATTR_READ_ONLY, \
        }, \
        .stream_extension_entry = { \
            .entry_type = exfat_entry_type_stream_extension, \
            .secondary_flags = 0x03, /* XXX */ \
            .name_length = PICOVD_UTF16_STRING_LEN(STR_UTF16_EXPAND(file_name_str)), \
            .name_hash = exfat_dirs_compute_name_hash(STR_UTF16_EXPAND(file_name_str), PICOVD_UTF16_STRING_LEN(STR_UTF16_EXPAND(file_name_str))), \
            .valid_data_length = file_size_bytes, \
            .first_cluster = 0, \
            .data_length = file_size_bytes, \
        }, \
        .file_name_entry = { \
            .entry_type = exfat_entry_type_file_name, \
            .file_name = STR_UTF16_EXPAND(file_name_str), \
        }, \
    };

// ---------------------------------------------------------------
// API to handle files on the virtual disk during runtime
// ---------------------------------------------------------------
// Add a file to the virtual disk during runtime
// The caller must retain the vd_dynamic_file_t struct until the file is removed
int vd_add_file(vd_dynamic_file_t* file);

// Update the size of a dynamic file on the virtual disk during runtime
// Updates the file size and modification time
int vd_update_file(vd_dynamic_file_t* file, size_t size_bytes);

// ---------------------------------------------------------------
// Virtual Disk Read Callback for USB MSC layer
// ---------------------------------------------------------------

extern int32_t vd_virtual_disk_read(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);

// ---------------------------------------------------------------
// Functions to provide RP2350 memory files
// XXX FIXME: Move to rp2350.h
// ---------------------------------------------------------------

extern void vd_file_sector_get_bootrom(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
extern void vd_file_sector_get_sram(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);
extern void vd_file_sector_get_flash(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);

// ---------------------------------------------------------------
// Indicate that the virtual disk contents have changed,
// forcing the host to re-read the disk.
// ---------------------------------------------------------------
extern void vd_virtual_disk_contents_changed(bool hard_reset);

#endif // VD_VIRTUAL_DISK_H
