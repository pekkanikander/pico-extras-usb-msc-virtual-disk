import struct
import pytest
import usb.core
import usb.util
import conftest

# SCSI command op‐codes
SCSI_CMD_WRITE10       = 0x2A
SCSI_CMD_MODE_SENSE_6  = 0x1A
SCSI_CMD_REQUEST_SENSE = 0x03

# USB MSC BOT signatures
CBW_SIGNATURE = 0x43425355
CSW_SIGNATURE = 0x53425355

def send_scsi(dev, lun, scsi_cmd, data_dir, data_len, data_out=b''):
    """
    Send a raw SCSI command over USB MSC BOT.
    - data_dir: 0 = OUT (host→device), 1 = IN (device→host), 2 = no data stage
    - data_len: expected transfer length
    - data_out: payload for OUT stage (if any)
    Returns (status, residue, data_in).
    """
    # find endpoints on the MSC interface
    cfg = dev.get_active_configuration()
    intf = usb.util.find_descriptor(
        cfg,
        bInterfaceNumber = conftest.USB_MSC_IFACE,
        bAlternateSetting = 0
    )
    ep_out = usb.util.find_descriptor(
        intf,
        custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
                                == usb.util.ENDPOINT_OUT
    )
    ep_in = usb.util.find_descriptor(
        intf,
        custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
                                == usb.util.ENDPOINT_IN
    )

    # build and send CBW
    tag = 0xDEADBEEF
    cbw = struct.pack(
        '<I I I B B B 16s',
        CBW_SIGNATURE,
        tag,
        data_len,
        data_dir,
        lun,
        len(scsi_cmd),
        bytes(scsi_cmd).ljust(16, b'\x00')
    )
    ep_out.write(cbw, timeout=1000)

    # data stage
    data_in = None
    if data_dir == 0 and data_len and data_out:
        ep_out.write(data_out, timeout=1000)
    elif data_dir == 1 and data_len:
        data_in = ep_in.read(data_len, timeout=1000)

    # read CSW
    csw_raw = bytes(ep_in.read(13, timeout=1000))
    sig, rtag, residue, status = struct.unpack('<I I I B', csw_raw[:13])
    assert sig == CSW_SIGNATURE, f"Bad CSW signature: {hex(sig)}"
    return status, residue, data_in

def parse_sense(data):
    """Extract (sense_key, asc, ascq) from 18-byte fixed-format sense."""
    sense_key = data[2] & 0x0F
    asc       = data[12]
    ascq      = data[13]
    return sense_key, asc, ascq

def test_write10_fails_with_protect(msc_usb_dev_device):
    dev = msc_usb_dev_device
    # 1 block (512B) write at LBA 0
    cdb = bytearray(16)
    cdb[0] = SCSI_CMD_WRITE10
    # LBA = bytes 2–5 = 0 by default, transfer length = 1 block -> byte 7=0, byte8=1
    cdb[7] = 0
    cdb[8] = 1

    status, residue, _ = send_scsi(
        dev, lun=0,
        scsi_cmd=cdb,
        data_dir=0,
        data_len=512,
        data_out=b'\x00'*512
    )
    assert status != 0, "WRITE10 should be rejected on read-only medium"

    # Now fetch sense data
    cdb = bytearray(6)
    cdb[0] = SCSI_CMD_REQUEST_SENSE
    cdb[4] = 18  # allocation length
    cdb = bytes(cdb) + b'\x00'*(16 - len(cdb))

    status, residue, sense_data = send_scsi(
        dev, lun=0,
        scsi_cmd=cdb,
        data_dir=1,
        data_len=18
    )
    skey, asc, ascq = parse_sense(sense_data)
    assert skey == 0x07, f"Expected DATA_PROTECT (0x7), got {hex(skey)}"
    assert (asc, ascq) == (0x27, 0x00), f"Expected ASC/ASCQ 27/00, got {hex(asc)}/{hex(ascq)}"

def test_mode_sense_reports_wp(msc_usb_dev_device):
    dev = msc_usb_dev_device
    # MODE SENSE(6), allocation length = 4 bytes
    cdb = bytes([SCSI_CMD_MODE_SENSE_6, 0, 0, 0, 4, 0] + [0]*10)
    status, residue, data = send_scsi(
        dev, lun=0,
        scsi_cmd=cdb,
        data_dir=1,
        data_len=4
    )
    assert status == 0, "MODE SENSE should succeed"
    # Byte 2 bit7 = Write-Protect
    assert (data[2] & 0x80) != 0, "Write-Protect bit not set in MODE SENSE response"
