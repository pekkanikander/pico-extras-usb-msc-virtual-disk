
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
#ifndef STR_UTF16_EXPAND
#define STR_UTF16_EXPAND(x) u ## #x
#endif

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

/**
 * @brief Define a dynamic (runtime) virtual file for the PicoVD virtual disk.
 *
 * This macro creates and initializes a vd_dynamic_file_t structure for a file
 * whose contents and size may change at runtime.
 * The file shall be registered with the virtual disk using vd_add_file().
 *
 * Dynamic files are useful for exposing data that changes over time or is generated on demand.
 *
 * @param struct_name   Name of the variable to define (vd_dynamic_file_t)
 * @param file_name     File name (as a string literal, e.g., "DYNAMIC.TXT")
 * @param file_size_bytes Initial file size in bytes (may be updated later)
 * @param get_content_cb Callback function to provide file content (see vd_file_sector_get_fn_t)
 *
 * @note The file name is automatically converted to UTF-16LE as required by exFAT.
 * @note The file is initially read-only. Other attributes can be set after creation if needed.
 * @note The struct must remain valid (not go out of scope) while the file is registered.
 *
 * @see vd_add_file()
 * @see vd_update_file()
 * @see vd_dynamic_file_t
 */
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
typedef struct vd_static_file_s vd_static_file_t; // Opaque, see vd_exfat_dirs.h

/**
 * @brief Define a static (compile-time) virtual file for the PicoVD virtual disk.
 *
 * This macro creates and initializes a vd_static_file_t structure
 * for a file whose contents are fixed at compile time.
 * Static files are suitable for exposing firmware images, documentation,
 * or other data that does not change at runtime.
 *
 * @param struct_name      Name of the variable to define (vd_static_file_t)
 * @param file_name_str    File name (as a string literal, e.g., "README.TXT")
 * @param file_size_bytes  File size in bytes
 *
 * @note The file name is automatically converted to UTF-16LE as required by exFAT.
 * @note The file is initially read-only. Other attributes are currently not supported.
 *
 * @see vd_static_file_t
 * @see PICOVD_DEFINE_FILE_RUNTIME
 *
 * For usage examples, see the README.
 */
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
            .name_hash = exfat_dirs_compute_name_hash(STR_UTF16_EXPAND(file_name_str), \
                            PICOVD_UTF16_STRING_LEN(STR_UTF16_EXPAND(file_name_str))), \
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

/**
 * @brief Register a dynamic (runtime) file with the PicoVD virtual disk.
 *
 * This function adds a file defined with PICOVD_DEFINE_FILE_RUNTIME
 * (or a manually constructed vd_dynamic_file_t struct) to the virtual disk,
 * making it visible to the host as part of the exFAT filesystem.
 *
 * The file's cluster allocation is based on max_size_bytes,
 * which sets the maximum size the file may grow to at runtime.
 * This allows the file's length to change dynamically (up to the reserved space).
 * Reserving space does not cost anything, but it does reserve space in the dynamic area.
 * Hence, as long as you are not short of the dynamic area,
 * you can allocate as much space as you want for each of your files.
 *
 * Whenever the file's size or contents change, the file should be updated using vd_update_file().
 * This informs the host that the file has changed and should be re-read.
 * Depending the host's caching strategy, the file may not be re-read immediately.
 * If you don't call vd_update_file(), the host may not notice the change for a long time,
 * even minutes or sometimes hours.
 *
 * However, notifying the host is a costly operation, causing the host to re-read
 * all of the virtual disk metadata and re-scanning the file system.
 * Hence, you should only call vd_update_file() when you are done with the changes.
 *
 * @param file Pointer to a vd_dynamic_file_t structure describing the file.
 * @param max_size_bytes Maximum file size (in bytes) that may be allocated for this file at runtime.
 *                      The file's cluster chain space is allocated to cover this size.
 *
 * @return 0 on success, negative value on error (e.g., if the requested space cannot be allocated).
 *
 * @note The file is initially read-only. For other attributes, you have to set them manually.
 * @note The number of dynamic files is limited by PICOVD_PARAM_MAX_DYNAMIC_FILES.
 * @note The vd_dynamic_file_t struct must remain valid while the file is registered.
 * @note vd_add_file() does not call vd_virtual_disk_contents_changed() automatically.
 *       You should call it after adding all your files.
 * @see PICOVD_DEFINE_FILE_RUNTIME
 * @see vd_update_file
 * @see vd_dynamic_file_t
 * @see vd_virtual_disk_contents_changed
 */
int vd_add_file(vd_dynamic_file_t* file, size_t max_size_bytes);

/**
 * @brief Update the size and modification time of a dynamic
 *       (runtime) file on the PicoVD virtual disk.
 *
 * This function should be called whenever the contents or
 * size of a registered dynamic file change.
 * It updates the file's size and modification timestamp,
 * and notifies the host that the file has changed.
 *
 * @param file Pointer to the vd_dynamic_file_t structure for the file to update.
 * @param size_bytes New file size in bytes.
 *
 * @return 0 on success, negative value on error (e.g., if more space cannot be allocated).
 *
 * @note Only files registered with vd_add_file() can be updated.
 *       Calling this function with a file that was not registered
 *       with vd_add_file() is likely to cause a crash.
 * @note The file remains read-only. Other attributes are currently not supported.
 *
 * @see vd_add_file
 * @see vd_dynamic_file_t
 */
int vd_update_file(vd_dynamic_file_t* file, size_t size_bytes);

/**
 * @brief Notify the host that the virtual disk contents have changed.
 *
 * This function signals to the USB MSC layer that the contents of the virtual disk
 * have changed, prompting the host operating system to re-read the disk and update
 * its cached view. This is useful after making changes to file data, directory entries,
 * or other on-disk structures that should be visible to the host.
 *
 * @param hard_reset If true, forces a full USB disconnect/reconnect (remount) to ensure
 *                   the host discards all cached metadata and data. If false, only a soft
 *                   notification is sent (host may still cache some data).
 *
 * @note Use this function after modifying files, directories, or metadata that must be
 *       immediately visible to the host. Frequent use may cause the host to remount the disk,
 *       which can interrupt ongoing file operations.
 * @note vd_update_file calls this function automatically with hard_reset=false.
 * @see vd_update_file
 */
extern void vd_virtual_disk_contents_changed(bool hard_reset);



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

#endif // VD_VIRTUAL_DISK_H
