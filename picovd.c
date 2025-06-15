#include <stdio.h>

#include <bsp/board.h>
#include <tusb.h>

#include <pico/stdlib.h>
#include <pico/usb_reset_interface.h>

int main()
{
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
        // TinyUSB device task | must be called regurlarly
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
