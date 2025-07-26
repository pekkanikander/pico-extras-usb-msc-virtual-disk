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

#ifndef PICOVD_PARAM_USB_MSC_UA_MINIMUM_DELAY_MS
#define PICOVD_PARAM_USB_MSC_UA_MINIMUM_DELAY_MS 5000
#endif

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

static enum {
    VD_CHANGED_NOT_CHANGED                          = 0x00,
    // Need to fail on media non-removal request
    VD_CHANGED_NEED_MEDIUM_REQUEST_DISALLOW_FAILURE = 0x01,
    VD_CHANGED_NEED_UA_28H                          = 0x02,
    VD_CHANGED_NEED_ALL                             = 0x03,
} vd_virtual_disk_contents_status = VD_CHANGED_NEED_MEDIUM_REQUEST_DISALLOW_FAILURE;

void vd_virtual_disk_contents_changed(bool hard_reset) {
    vd_virtual_disk_contents_status = VD_CHANGED_NEED_ALL;

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

#define PICOVD_MSC_PRODUCT_NAME    PICO_PROGRAM_NAME "                "
#define PICOVD_MSC_PRODUCT_VERSION PICO_PROGRAM_VERSION_STRING "    "

// New SCSI Inquiry: set write protected
uint32_t tud_msc_inquiry2_cb(uint8_t lun,
    scsi_inquiry_resp_t* inquiry_rsp) {

    // Set Write Protect flag (bit 0 in byte 5)
    inquiry_rsp->protect |= 1;

    memcpy(inquiry_rsp->vendor_id,   PICOVD_MSC_VENDOR_ID,       sizeof(inquiry_rsp->vendor_id));
    memcpy(inquiry_rsp->product_id,  PICOVD_MSC_PRODUCT_NAME,    sizeof(inquiry_rsp->product_id));
    memcpy(inquiry_rsp->product_rev, PICOVD_MSC_PRODUCT_VERSION, sizeof(inquiry_rsp->product_rev));

    return sizeof(scsi_inquiry_resp_t);
}

// Capacity callback: return block count and block size
void tud_msc_capacity_cb(uint8_t lun,
                         uint32_t* block_count,
                         uint16_t* block_size)
{
    *block_count = MSC_TOTAL_BLOCKS;
    *block_size  = MSC_BLOCK_SIZE;
}

// XXX TBD
bool tud_msc_prevent_allow_medium_removal_cb(uint8_t lun,
                                            uint8_t prevent,
                                            uint8_t control)
{
    if (vd_virtual_disk_contents_status &   VD_CHANGED_NEED_MEDIUM_REQUEST_DISALLOW_FAILURE) {
        vd_virtual_disk_contents_status &= ~VD_CHANGED_NEED_MEDIUM_REQUEST_DISALLOW_FAILURE;
        return false;
    }
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
 * @brief SCSI WRITE10 command callback - should never be reached.
 *
 * This callback is mandatory for TinyUSB but should never be executed in practice
 * because TinyUSB's proc_write10_cmd() checks tud_msc_is_writable_cb() first and
 * fails the operation with write-protect sense codes before calling this callback.
 *
 * If this callback is ever reached, it indicates a bug in TinyUSB or our implementation.
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

    // This should never be reached - TinyUSB should have blocked the write earlier
    assert(false && "tud_msc_write10_cb should never be called for read-only device");

    // Fallback: Queue sense data: Data Protect (0x07), Write Protected (0x27, 0x00)
    tud_msc_set_sense(lun,
                      SCSI_SENSE_DATA_PROTECT,
                      SCSI_ASC_WRITE_PROTECTED,
                      SCSI_ASCQ_WRITE_PROTECTED);

    // Indicate command failure to the host
    return TUD_MSC_RET_ERROR;
}

// Invoked when received Test Unit Ready command.
// Normally returns true, but if the virtual disk contents have changed,
// it returns false and sets a Unit Attention sense code.
// See SPC-4 §6.7.1 for details on Unit Attention conditions.
// Returning UA 0x28 notifies the host that the disk contents have changed and
// that the host should re-read the disk.
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (vd_virtual_disk_contents_status &   VD_CHANGED_NEED_UA_28H) {

        // Rate limit UA requests to prevent excessive refresh requests
        static uint32_t last_ua_time;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_ua_time < PICOVD_PARAM_USB_MSC_UA_MINIMUM_DELAY_MS) {
            return true;
        }
        last_ua_time = now;

        vd_virtual_disk_contents_status &= ~VD_CHANGED_NEED_UA_28H;

        tud_msc_set_sense(0,
            SCSI_SENSE_UNIT_ATTENTION,
            SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED,
            0x00);
        return false;
    }
    return true;
}

int32_t tud_msc_scsi_cb(uint8_t lun,
                        uint8_t const scsi_cmd[16],
                        void* buffer,
                        uint16_t bufsize)
{
    switch (scsi_cmd[0]) {
    /*
     * Reject any SCSI commands that would alter the medium on a read-only device.
     * Always report Data Protect (Write Protected) sense per SPC-4 §6.7.
     */
    case SCSI_CMD_MODE_SELECT_6:
    case SCSI_CMD_MODE_SELECT_10:
    case SCSI_CMD_UNMAP:
    case SCSI_CMD_FORMAT_UNIT:
    case SCSI_CMD_WRITE12:
    case SCSI_CMD_WRITE16:
        tud_msc_set_sense(lun,
                    SCSI_SENSE_DATA_PROTECT,
                    SCSI_ASC_WRITE_PROTECTED,
                    SCSI_ASCQ_WRITE_PROTECTED);
        return TUD_MSC_RET_ERROR;

    /*
     * Handle MODE SENSE (10) to report a write-protected medium.
     * This responds with a Mode Parameter Header (SPC-4 §5.5.4) with the Write-Protect bit set,
     * so the host recognizes the device as read-only. No block descriptors or mode pages are returned.
     */
    case SCSI_CMD_MODE_SENSE_10:
    {
        scsi_mode_sense10_resp_t* resp = (scsi_mode_sense10_resp_t*)buffer;
        memset(resp, 0, sizeof(*resp));
        // Mode Data Length = total bytes following data_len field (sizeof(header)-2)
        resp->data_len = tu_htons(sizeof(*resp) - 2);
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
