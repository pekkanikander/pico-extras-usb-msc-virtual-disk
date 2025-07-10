# conftest.py - pytest configuration for exFAT tests

USB_VENDOR_ID    = 0x2E8A
USB_PRODUCT_ID   = 0x0009
USB_MSC_IFACE    = 3

import os
import sys
import pytest
import platform
import subprocess
import usb.core, usb.util

from device_finder import find_msc_disk
from exfat_utils import raw_sector_reader, find_directory_entry

def pytest_addoption(parser):
    """
    Add --device option to specify a raw block device to read from.
    """
    parser.addoption(
        "--device",
        action="store",
        default=None,
        help="Raw block device to read exFAT image from (e.g. /dev/sdX or /dev/rdiskY)."
    )

def find_msc_usb_device():
    dev = usb.core.find(idVendor=USB_VENDOR_ID, idProduct=USB_PRODUCT_ID)
    assert dev, "Pico not plugged in"
    dev.set_configuration()
    return dev

@pytest.fixture
def msc_usb_dev_device():
    if platform.system() == "Darwin":
        pytest.skip("macOS won’t relinquish the MSC interface to libusb—run these tests on Linux")
    dev = find_msc_usb_device()

    # On Linux, this also unmounts the FS
    if platform.system() == "Linux" and dev.is_kernel_driver_active(MSC_IFACE):
        dev.detach_kernel_driver(USB_MSC_IFACE)

    return dev

@pytest.fixture
def device(request):
    """
    Resolve the raw block device path:
      1) --device CLI option
      2) TEST_EXFAT_DEVICE env var
      3) On macOS, auto-detect via diskutil
    """
    # CLI or env override
    dev = request.config.getoption("--device") or os.getenv("TEST_EXFAT_DEVICE")
    if dev:
        return dev
    # Attempt to auto-detect, should work on macOS, Linux
    try:
        return find_msc_disk()
    except Exception as e:
        pytest.skip(f"Could not auto-detect MSC disk: {e}")
    pytest.skip("No MSC device found or specified; use --device or TEST_EXFAT_DEVICE")


@pytest.fixture
def read_raw_sector(device):
    """
    Fixture: returns a callable to read a single 512-byte sector at the given LBA from:
      - A raw block device via --device or TEST_EXFAT_DEVICE.
    Skips tests if no device is specified or on errors.
    """
    reader = raw_sector_reader(device)
    def _read(lba):
        try:
            return reader(lba)
        except IOError as e:
            pytest.skip(str(e))
    return _read

@pytest.fixture
def bootsector_data(device, read_raw_sector):
    """Fixture: 512 bytes of exFAT boot sector (LBA 0)."""
    # Try raw device first
    if device:
        return read_raw_sector(0)

    # Fallback: use pre-generated bootsector binary
    for path in ('tests/bootsector.bin', 'build/bootsector.bin'):
        if os.path.exists(path):
            with open(path, 'rb') as f:
                data = f.read(512)
            if len(data) < 512:
                pytest.skip(f"Boot sector file {path} is only {len(data)} bytes")
            return data

    pytest.skip(
        "No boot sector source found; specify --device or TEST_EXFAT_DEVICE, "
        "or generate tests/bootsector.bin"
    )


@pytest.fixture
def dir_entry_finder(read_raw_sector, bootsector_data):
    """
    Returns a function that looks up a 32-byte root-directory entry by entry_type.
    """
    def _finder(entry_type):
        entry = find_directory_entry(read_raw_sector, bootsector_data, entry_type)
        if entry is None:
            pytest.skip(f"Directory entry type {entry_type:#x} not found in root directory")
        return entry
    return _finder

@pytest.fixture
def allocation_bitmap_entry(dir_entry_finder):
    """Fixture: raw 32-byte Allocation Bitmap directory entry."""
    return dir_entry_finder(0x81)

@pytest.fixture
def upcase_table_entry(dir_entry_finder):
    """Fixture: raw 32-byte Up-case Table directory entry."""
    return dir_entry_finder(0x82)

@pytest.fixture
def volume_label_entry(dir_entry_finder):
    """Fixture: raw 32-byte Volume Label directory entry."""
    return dir_entry_finder(0x83)

@pytest.fixture
def cluster_chain_reader(read_raw_sector, bootsector_data):
    """
    Fixture: returns a function to read a sequential cluster chain starting at given cluster.
    Assumes cluster chains are sequential (non-fragmented).
    """
    from exfat_utils import cluster_chain_reader as _ccr
    def _reader(start_cluster):
        return _ccr(read_raw_sector, bootsector_data, start_cluster)
    return _reader
