"""
tests/test_exfat_sectors_1-10.py

Pytest suite for validating reserved sectors 1-10 on the exFAT virtual disk.

This test module:
  - Reads sectors 1 through 10 via the `read_raw_sector` fixture.
  - Verifies each sector is exactly 512 bytes.
  - Asserts that every byte in these sectors is zero.
"""
"""
tests/test_exfat_sectors_1-10.py

Pytest suite for validating reserved sectors 1–10 on the exFAT virtual disk.

This test module:
  - Reads sectors 1 through 10 via the `read_raw_sector` fixture.
  - Verifies each sector is exactly 512 bytes.
  - For sectors 1-8 (Extended Boot Sectors), ensures all bytes except the final 4 are zero,
    and the 4-byte ExtendedBootSignature at offset 508 equals 0xAA550000.
  - For sectors 9-10, asserts every byte is zero.
"""

import struct
import pytest

def test_extended_boot_sectors_signature(device, read_raw_sector):
    """
    Sectors 1-8 are Main Extended Boot Sectors.
    Each must have zeros in bytes 0-507 and an ExtendedBootSignature (AA550000h) at bytes 508-511.
    """
    for lba in range(1, 9):
        data = read_raw_sector(lba)
        assert len(data) == 512
        # ExtendedBootCode defaults to 0x00 when no boot code is present citeturn1view0
        assert data[:508] == b'\x00' * 508
        signature = struct.unpack_from('<I', data, 508)[0]
        assert signature == 0xAA550000

def test_excess_reserved_sectors_zero(device, read_raw_sector):
    """
    Sectors 9-10 are reserved and must be entirely zero.
    """
    for lba in (9, 10):
        data = read_raw_sector(lba)
        assert len(data) == 512
        assert data == b'\x00' * 512

