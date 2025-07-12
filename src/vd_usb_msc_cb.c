/**
 * @file src/vd_usb_msc.c
 * @brief TinyUSB USB MSC callbacks
 */

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <tusb.h>
#include <class/msc/msc.h>

#include <picovd_config.h>
#include "vd_exfat_params.h"
#include "vd_virtual_disk.h"

// Additional Sense Code and Qualifier for Write Protected (per SPC-4 §6.7)

#ifndef SCSI_ASC_WRITE_PROTECTED
#define SCSI_ASC_WRITE_PROTECTED  0x27
#endif
#ifndef SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED
#define SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED 0x28
#endif
#ifndef SCSI_ASCQ_WRITE_PROTECTED
#define SCSI_ASCQ_WRITE_PROTECTED 0x00
#endif

#ifndef SCSI_CMD_FORMAT_UNIT
#define SCSI_CMD_FORMAT_UNIT      0x04
#endif
#ifndef SCSI_CMD_BLANK
#define SCSI_CMD_BLANK            0x19
#endif
#ifndef SCSI_CMD_UNMAP
#define SCSI_CMD_UNMAP            0x42
#endif
#ifndef SCSI_CMD_MODE_SELECT_10
#define SCSI_CMD_MODE_SELECT_10   0x55
#endif
#ifndef SCSI_CMD_WRITE12
#define SCSI_CMD_WRITE12          0xAA
#endif
#ifndef SCSI_CMD_WRITE16
#define SCSI_CMD_WRITE16          0x8A
#endif

#ifndef SCSI_CMD_MODE_SENSE_10
#define SCSI_CMD_MODE_SENSE_10    0x5A

// Mode Parameter Header (10) for MODE SENSE (10) – SPC-4 §5.5.4
typedef struct TU_ATTR_PACKED {
  uint16_t data_len;        // Mode Data Length (big-endian)
  uint8_t  medium_type;     // 0 = direct-access
  uint8_t  dev_spec_params; // bit7 = WP
  uint8_t  reserved;
  uint16_t blk_desc_len;    // Block-Descriptor Length (big-endian)
  uint8_t  reserved2;
} scsi_mode_sense10_resp_t;
_Static_assert(sizeof(scsi_mode_sense10_resp_t) == 8, "SCSI Mode Sense (10) response size mismatch");

#endif

/**
 * @brief Notify the host that the virtual disk contents have changed.
 *
 * This function should be called whenever the contents of the virtual disk
 * change, such as when partition contents have changed. It will trigger
 * the host to re-read the disk contents.
 *
 * This is a relatively heavy operation, as it will cause the host to
 * re-read the entire disk, so it should be used sparingly.
 */

static bool vd_virtual_disk_contents_changed_flag = false;

void vd_virtual_disk_contents_changed(bool hard_reset) {
    vd_virtual_disk_contents_changed_flag = true;

    // Drop the USB connection to notify the host
    // that the disk contents have changed.
    // This will cause the host to re-enumerate the device.
    // The host will then re-read the disk contents.
    // This is necessary because the host may cache the disk contents,
    // and we need to ensure it sees the new contents.
    // This is a workaround for the fact that the host may not
    // automatically re-read the disk contents when they change.
    if (hard_reset) {
      tud_disconnect();      // remove D+ pull-up
      sleep_ms(3);          // host will see device vanish
      tud_connect();         // re-enumerate as a brand-new device
    }
}


/*
 * --------------------------------------------------------------------------
 * MSC SCSI Callback Section
 *
 * This section implements callbacks for TinyUSB's Mass Storage Class (MSC)
 * using the SCSI transparent command set.
 *
 * References:
 *  - USB Mass Storage Class (MSC) Bulk-Only Transport (BOT), Rev. 1.0:
 *    https://www.usb.org/sites/default/files/usbmassbulk_10.pdf
 *  - SCSI Primary Commands - 4 (SPC-4), T10/1731-D:
 *    https://www.t10.org/members/w_spc4.htm
 * --------------------------------------------------------------------------
 */

 #if CFG_TUD_MSC

// Read10 callback: serve LBA regions defined in the lba_regions table
int32_t tud_msc_read10_cb(uint8_t lun         __unused,
                          uint32_t lba,
                          uint32_t offset,
                          void*    buffer,
                          uint32_t bufsize)
{
    // Enforce single LUN, full-sector, offset-zero semantics
    assert(lun == 0);

    return vd_virtual_disk_read(lba, offset, buffer, bufsize);
}

// SCSI Inquiry: return manufacturer, product, revision strings
void tud_msc_inquiry_cb(uint8_t lun,
                        uint8_t vendor_id[8],
                        uint8_t product_id[16],
                        uint8_t product_rev[4])
{
    // XXX FIXME: Replace with configuration strings
    memcpy(vendor_id,  "Raspberry",    8);
    memcpy(product_id, "Pico MSC Disk\0\0\0",16);
    memcpy(product_rev,"1.0 ",         4);
}

// Capacity callback: return block count and block size
void tud_msc_capacity_cb(uint8_t lun,
                         uint32_t* block_count,
                         uint16_t* block_size)
{
    *block_count = MSC_TOTAL_BLOCKS;
    *block_size  = MSC_BLOCK_SIZE;
}

// Ready callback: always ready
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    return true;
}

// Start/Stop callback: no media ejection support
bool tud_msc_start_stop_cb(uint8_t lun,
                           uint8_t power_condition,
                           bool start,
                           bool load_eject)
{
    (void) power_condition;
    (void) start;
    (void) load_eject;
    return true;
}

/**
 * @brief SCSI WRITE10 command callback, enforcing read-only medium.
 *
 * Always fails write attempts by returning an error and reporting
 * write-protected sense, conforming to SPC-4 §6.7 and USB MSC BOT spec §6.4.
 */
int32_t tud_msc_write10_cb(uint8_t lun,
                           uint32_t lba,
                           uint32_t offset,
                           uint8_t* buffer,
                           uint32_t bufsize)
{
    (void) lba;
    (void) offset;
    (void) buffer;

    // Queue sense data: Data Protect (0x07), Write Protected (0x27, 0x00)
    tud_msc_set_sense(lun,
                      SCSI_SENSE_DATA_PROTECT,
                      SCSI_ASC_WRITE_PROTECTED,
                      SCSI_ASCQ_WRITE_PROTECTED);

    // Indicate command failure to the host
    return TUD_MSC_RET_ERROR;
}

/**
 * @brief SCSI transparent command callbacks.
 *
 * XXX UPDATE the documentation!
 *
 * Handles MODE SENSE (6) to report a write-protected medium.
 * Other SCSI commands use TinyUSB's default handling.
 *
 * SPC-4 §5.5.3: Mode Sense (6) returns a 4-byte Mode Parameter Header.
 *  - Byte 0: Mode Data Length (n-1 bytes following)
 *  - Byte 1: Medium Type (0 = direct-access)
 *  - Byte 2: Device-Specific Parameter (bit7 = WP)
 *  - Byte 3: Block Descriptor Length (0)
 */
int32_t tud_msc_scsi_pre_cb(uint8_t lun,
                           uint8_t const scsi_cmd[16],
                           void* buffer,
                           uint16_t bufsize)
{
    switch (scsi_cmd[0]) {
    /*
     * Implement fully read-only disk semantics.
     */
    case SCSI_CMD_INQUIRY:
    {
        // Build Inquiry response, reporting write-protected media
        scsi_inquiry_resp_t* resp = (scsi_inquiry_resp_t*)buffer;
        memset(resp, 0, sizeof(*resp));

        // resp->peripheral_device_type = 0; // Already zeroed
        // resp->peripheral_qualifier   = 0; // Already zeroed
        resp->is_removable           = 1;
        resp->version                = 2;
        resp->response_data_format   = 2;
        resp->additional_length      = sizeof(scsi_inquiry_resp_t) - 5;

        // Set Write Protect flag (bit in byte 5)
        resp->protect                = 1;

        // Fill in vendor, product and revision strings, using TinyUSB's callback
        tud_msc_inquiry_cb(lun,
                          resp->vendor_id,
                          resp->product_id,
                          resp->product_rev);

        // Return entire inquiry response size
        return sizeof(*resp);
    }
#if 0 // Not needed, tud_msc_is_writable_cb() takes care of this
    case SCSI_CMD_MODE_SENSE_6:
    {
        // Build 4-byte Mode Parameter Header
        scsi_mode_sense6_resp_t* resp = (scsi_mode_sense6_resp_t*)buffer;
        memset(resp, 0, sizeof(*resp));
        resp->data_len = sizeof(*resp) - 1;
        // resp->medium_type = 0; // Already zeroed
        resp->write_protected = true;
        // resp->block_descriptor_len = 0; // Already zeroed
        return sizeof(*resp);
    }
#endif
    /*
    * Implement virtual-disk contents change notification.
    */
    case SCSI_CMD_TEST_UNIT_READY:
    case SCSI_CMD_READ_CAPACITY_10:
        if (vd_virtual_disk_contents_changed_flag) {
            // If the virtual disk contents have changed, notify the host
            // that it should re-read the disk.
            // This is done by setting a Unit Attention sense code.
            // See SPC-4 §6.7.1 for details on Unit Attention conditions.
            tud_msc_set_sense(0,
                SCSI_SENSE_UNIT_ATTENTION,
                SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED,
                0x00);
            vd_virtual_disk_contents_changed_flag = false;  // Only once
            return TUD_MSC_RET_ERROR;      // Signal CHECK CONDITION
        }
        // Fallback to the default handler
        break;
    }
    return TUD_MSC_RET_CALL_DEFAULT; // Fallback to default handling
}

int32_t tud_msc_scsi_cb(uint8_t lun,
                        uint8_t const scsi_cmd[16],
                        void* buffer,
                        uint16_t bufsize)
{
    switch (scsi_cmd[0]) {
    /*
     * Reject any SCSI commands that would alter the medium on a read-only device.
     * These include MODE SELECT, UNMAP, FORMAT UNIT, and BLANK. Always report
     * Data Protect (Write Protected) sense per SPC-4 §6.7.
     */
    case SCSI_CMD_MODE_SELECT_6:
    case SCSI_CMD_MODE_SELECT_10:
    case SCSI_CMD_UNMAP:
    case SCSI_CMD_FORMAT_UNIT:
    case SCSI_CMD_BLANK:
    case SCSI_CMD_WRITE12:
    case SCSI_CMD_WRITE16:
        tud_msc_set_sense(lun,
                    SCSI_SENSE_DATA_PROTECT,
                    SCSI_ASC_WRITE_PROTECTED,
                    SCSI_ASCQ_WRITE_PROTECTED);
        return TUD_MSC_RET_ERROR;

    case SCSI_CMD_MODE_SENSE_10:
    {
        scsi_mode_sense10_resp_t* resp = (scsi_mode_sense10_resp_t*)buffer;
        memset(resp, 0, sizeof(*resp));
        // Mode Data Length = total bytes following data_len field (sizeof(header)-2)
        resp->data_len = __builtin_bswap16(sizeof(*resp) - 2); // XXX FIXME: use a TinyUSB macro for this
        // Byte-3 (Device-Specific Parameter): set Write-Protect bit (0x80)
        resp->dev_spec_params = 0x80;
        // Block Descriptor Length = 0 (bytes 4-5 already zeroed)
        return sizeof(*resp);
    }
    default:
        // For all other commands, fallback to default (unrecognized command)
        return -1;
    }
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    return false; // Always read-only
}

#endif // CFG_TUD_MSC
