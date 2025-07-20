
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

// ---------------------------------------------------------------
// Virtual Disk File Structure
// ---------------------------------------------------------------
typedef struct __packed {
    const char16_t *   name;            // Pointer to UTF-16LE file name
    uint8_t            name_length;     // Name length, in UTF-16 code units
    fat_file_attr_t    file_attributes; // FAT/exFAT file attributes
    uint16_t           first_cluster;   // First cluster number
    size_t             size_bytes;      // File size in bytes
    time_t             creat_time_sec;  // Creation time in seconds, Unix epoch (since 1.1.1970)
    time_t             mod_time_sec;    // Modification time in seconds
    usb_msc_lba_read10_fn_t get_content;
} vd_file_t;

// ---------------------------------------------------------------
// API to add a file to the virtual disk during runtime
// ---------------------------------------------------------------
int vd_add_file(const vd_file_t* file);

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
// Function to provide the changing file contents sector
// ---------------------------------------------------------------
extern void vd_file_sector_get_changing_file(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize);

// ---------------------------------------------------------------
// Indicate that the virtual disk contents have changed,
// forcing the host to re-read the disk.
// ---------------------------------------------------------------
extern void vd_virtual_disk_contents_changed(bool hard_reset);

#endif // VD_VIRTUAL_DISK_H
