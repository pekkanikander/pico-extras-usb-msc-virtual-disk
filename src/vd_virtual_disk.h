
#include <stdint.h>

// ---------------------------------------------------------------
// Functions to provide RP2350 memory
// ---------------------------------------------------------------

extern void vd_return_bootrom_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);
extern void vd_return_sram_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);
extern void vd_return_flash_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);

// ---------------------------------------------------------------
// Indicate that the virtual disk contents have changed,
// forcing the host to re-read the disk.
// ---------------------------------------------------------------
extern void vd_virtual_disk_contents_changed(void);
