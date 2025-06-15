"""
tests/test_exfat_alloc_bitmap_first.py

Pytest suite for validating the first sector of the exFAT allocation bitmap (LBA = ClusterHeapOffset).

This test module:
  - Reads the boot sector to extract ClusterHeapOffset.
  - Reads the first allocation bitmap sector at that LBA via the `read_raw_sector` fixture.
  - Verifies sector size is 512 bytes.
  - Asserts that every byte in the sector is 0xFF.
"""

import struct

def test_exfat_alloc_bitmap_first(bootsector_data, read_raw_sector):
    # Extract ClusterHeapOffset (4-byte little-endian) from boot sector at offset 88
    cluster_heap_offset = struct.unpack_from('<I', bootsector_data, 88)[0]

    # Read the allocation bitmap start sector
    data = read_raw_sector(cluster_heap_offset)

    # Verify we got exactly one full sector
    assert len(data) == 512

    # Every byte in the allocation bitmap should be 0xFF
    expected = bytes([0xFF] * 512)
    assert data == expected
