
#include <stdint.h>

// ---------------------------------------------------------------
// Functions to provide RP2350 memory
// ---------------------------------------------------------------

extern void vd_return_bootrom_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);
extern void vd_return_sram_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);
extern void vd_return_flash_sector(uint32_t lba, void* buffer, uint32_t offset, uint32_t bufsize);
