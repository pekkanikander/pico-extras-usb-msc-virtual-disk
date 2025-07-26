#include <stdio.h>

#include <bsp/board.h>
#include <tusb.h>

#include <pico/stdlib.h>
#include <pico/usb_reset_interface.h>

#include <pico/bootrom.h>
#include <boot/picobin.h>

#include <vd_virtual_disk.h>
#include <vd_files_stdout.h>

int main()
{
    // Initialize XIP and flash, necessary when running as a no_flash binary
    rom_connect_internal_flash(); // Ensure the flash is connected
    rom_flash_exit_xip();         // ensure we're starting from SPI-command mode
    rom_flash_enter_cmd_xip();    // send 0xEB + dummy cycles
    rom_flash_flush_cache();

    // Load the partition table from the BootROM,
    // also necessary when running as a no_flash binary
    static uint8_t work_area[4 * 1024]; // XXX FIXME
    int rc = rom_load_partition_table(work_area, sizeof(work_area), false);

    // Initialize TinyUSB stack
    board_init();
    tusb_init();

    // TinyUSB board init callback after init
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }

    // let pico sdk use the first cdc interface for std io
    stdio_init_all();

    // Add STDOUT.TXT files to the virtual disk
    vd_files_stdout_init();

    // Print the PicoVD version, with at least 128 bytes, to get it exposed
    // through the exFAT file system.
    printf("PicoVD:" PICO_PROGRAM_VERSION_STRING " " PICO_PROGRAM_NAME "\n");
    printf("Padding padding padding padding padding padding padding padding\n");
    printf("Padding padding padding padding padding padding padding padding\n");
    printf("Padding padding padding padding padding padding padding padding\n");
    printf("Padding padding padding padding padding padding padding padding\n");
    fflush(stdout);

    // main run loop
    while (true) {
        // TinyUSB device task, must be called regurlarly
        tud_task();
    }
}
