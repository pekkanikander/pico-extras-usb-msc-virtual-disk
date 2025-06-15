

"""
tests/test_exfat_fat_sector_first.py

Pytest suite for validating the first FAT sector (LBA = FATOffset) on the exFAT virtual disk.

This test module:
  - Reads the boot sector to extract FATOffset.
  - Reads the FAT sector at that LBA via the `read_raw_sector` fixture.
  - Verifies sector size is 512 bytes.
  - Asserts that the first two FAT entries (4 bytes each) are the reserved values:
    - Entry 0 == 0xFFFFFFF8
    - Entry 1 == 0xFFFFFFFF
"""

import struct

def test_fat_sector_reserved_entries(bootsector_data, read_raw_sector):
    # Extract FATOffset (4-byte little-endian) from boot sector at offset 80
    fat_offset = struct.unpack_from('<I', bootsector_data, 80)[0]
    # Read the FAT sector
    data = read_raw_sector(fat_offset)
    assert len(data) == 512

    # Unpack the first two FAT entries
    entry0, entry1 = struct.unpack_from('<II', data, 0)
    assert entry0 == 0xFFFFFFF8  # reserved cluster 0
    assert entry1 == 0xFFFFFFFF  # reserved cluster 1

    # Unpack the first 15 FAT entries (covers clusters 0–14)
    entries = struct.unpack_from('<15I', data, 0)

    # 2–9: Allocation bitmap chain (clusters 2→3→…→9→EOC)
    for cluster in range(2, 9):
        assert entries[cluster] == cluster + 1, \
            f"Allocation bitmap chain: FAT[{cluster}] == {entries[cluster]:#x}, expected {cluster+1:#x}"
    assert entries[9] == 0xFFFFFFFF, \
        f"Allocation bitmap end-of-chain: FAT[9] == {entries[9]:#x}"

    # 10: Up-case table chain (single cluster)
    assert entries[10] == 0xFFFFFFFF, \
        f"Up-case table end-of-chain: FAT[10] == {entries[10]:#x}"

    # 11–13: Root directory chain (clusters 11→12→13→EOC)
    assert entries[11] == 12, \
        f"Root dir chain: FAT[11] == {entries[11]:#x}, expected 0xc"
    assert entries[12] == 13, \
        f"Root dir chain: FAT[12] == {entries[12]:#x}, expected 0xd"
    assert entries[13] == 0xFFFFFFFF, \
        f"Root dir end-of-chain: FAT[13] == {entries[13]:#x}"
