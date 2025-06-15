/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

#define CFG_TUD_ENABLED         (1)

// Legacy RHPORT configuration
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT        (0)
#endif
// end legacy RHPORT

//------------------------
// DEVICE CONFIGURATION //
//------------------------

// Enable 1 CDC and 1 MSC class
#define CFG_TUD_CDC             (1)
#define CFG_TUD_VENDOR          (1)
#define CFG_TUD_MSC             (1)

// Set CDC FIFO buffer sizes
#define CFG_TUD_CDC_BUFSIZE     (64)
#define CFG_TUD_CDC_RX_BUFSIZE  CFG_TUD_CDC_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE  CFG_TUD_CDC_BUFSIZE
#define CFG_TUD_CDC_EP_BUFSIZE  CFG_TUD_CDC_BUFSIZE

// Enable support for vendor-class interfaces (e.g. the “Reset” interface)
// and pick a buffer size for vendor control transfers (if needed)
#ifndef CFG_TUD_VENDOR_RX_BUFSIZE
#define CFG_TUD_VENDOR_RX_BUFSIZE  64
#endif

#define CFG_TUD_MSC_BUFSIZE     (512)
// MSC USB endpoint max-packet size (full-speed bulk = 64 bytes)
#ifndef CFG_TUD_MSC_EP_BUFSIZE
#define CFG_TUD_MSC_EP_BUFSIZE  (64)
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  (64)
#endif

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
