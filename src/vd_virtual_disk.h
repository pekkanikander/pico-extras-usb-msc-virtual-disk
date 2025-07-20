
#include <stdint.h>

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
