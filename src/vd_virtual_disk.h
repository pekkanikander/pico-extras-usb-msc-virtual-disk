
#include <stdint.h>

// ---------------------------------------------------------------
// Virtual Disk Read Callback
// ---------------------------------------------------------------

extern int32_t vd_virtual_disk_read(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);

// ---------------------------------------------------------------
// Functions to provide RP2350 memory files
// XXX FIXME: Move to rp2350.h
// ---------------------------------------------------------------

extern void vd_return_bootrom_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);
extern void vd_return_sram_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);
extern void vd_return_flash_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);

// ---------------------------------------------------------------
// Function to provide the changing file contents sector
// ---------------------------------------------------------------
extern void vd_return_changing_file_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);

// ---------------------------------------------------------------
// Indicate that the virtual disk contents have changed,
// forcing the host to re-read the disk.
// ---------------------------------------------------------------
extern void vd_virtual_disk_contents_changed(void);
