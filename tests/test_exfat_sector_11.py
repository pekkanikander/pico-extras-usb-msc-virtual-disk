"""
tests/test_exfat_sector_11.py

Pytest suite for validating the main VBR checksum in sector 11.

This test module:
  - Reads sectors 0 through 10 via the `read_raw_sector` fixture.
  - Applies the exFAT VBR checksum algorithm (ROR by 1, add byte) across all 11 sectors,
    zeroing the three ignored bytes in sector 0 (VolumeFlags and PercentInUse) per spec.
  - Reads sector 11 and verifies it contains 128 copies of the 4-byte checksum (little-endian).
"""

import struct
import pytest

from exfat_utils import compute_vbr_checksum, raw_sector_reader

def test_vbr_checksum_sector(read_raw_sector):
    """
    Sector 11 must contain 128 repetitions of the computed VBR checksum.
    """
    sectors = [read_raw_sector(lba) for lba in range(11)]
    checksum = compute_vbr_checksum(sectors)
    sector11 = read_raw_sector(11)
    assert len(sector11) == 512
    expected_pattern = struct.pack('<I', checksum) * (512 // 4)
    assert sector11 == expected_pattern

if __name__ == "__main__":
    # Hack: standalone computation of the VBR checksum from a raw device
    import sys
    if len(sys.argv) < 2:
        print("Usage: python test_exfat_sector_11.py <device_path>")
        sys.exit(1)
    device_path = sys.argv[1]
    read_raw_sector = raw_sector_reader(device_path)
    # Read sectors 0–10
    sectors = [read_raw_sector(lba) for lba in range(11)]
    checksum = compute_vbr_checksum(sectors)
    print(f"VBR checksum = 0x{checksum:08X}")
