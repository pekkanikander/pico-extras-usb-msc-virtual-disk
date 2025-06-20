#include <stdio.h>

#include <bsp/board.h>
#include <tusb.h>

#include <pico/stdlib.h>
#include <pico/usb_reset_interface.h>

#include <pico/bootrom.h>
#include <boot/picobin.h>

int main()
{
    // Initialize XIP and flash, necessary when running as a no_flash binary
    rom_connect_internal_flash(); // Ensure the flash is connected
    rom_flash_exit_xip();         // ensure we're starting from SPI-command mode
    rom_flash_enter_cmd_xip();    // send 0xEB + dummy cycles
    rom_flash_flush_cache();

    // Initialize TinyUSB stack
    board_init();
    tusb_init();

    // TinyUSB board init callback after init
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }

    // let pico sdk use the first cdc interface for std io
    stdio_init_all();

    // main run loop
    while (true) {
        // TinyUSB device task, must be called regurlarly
        tud_task();
#if 0
        if (tud_cdc_n_connected(0)) {
            // print on CDC 0 some debug message
            printf("Connected to CDC 0\n");
            sleep_ms(5000); // wait for 5 seconds
        }
#endif
    }
}
