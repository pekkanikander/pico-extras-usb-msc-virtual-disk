"""
tests/test_exfat_root_dir_metadata_entries.py

Verify that the root‐directory cluster contains each of the required
exFAT metadata entries — Volume Label (0x83), Allocation Bitmap (0x81)
and Up-case Table (0x82) — in any order.
"""

import struct

def test_exfat_root_dir_metadata_entries(bootsector_data, read_raw_sector):
    # --- 1) Parse key boot‐sector fields (all little‐endian) ---
    # Byte-shifts for addressing:
    bytes_per_sector_shift   = struct.unpack_from('<B', bootsector_data, 0x6C)[0]
    sectors_per_cluster_shift= struct.unpack_from('<B', bootsector_data, 0x6D)[0]
    # ClusterHeapOffset and root‐directory start cluster:
    cluster_heap_offset      = struct.unpack_from('<I', bootsector_data, 0x58)[0]
    root_dir_cluster         = struct.unpack_from('<I', bootsector_data, 0x60)[0]

    bytes_per_sector   = 1 << bytes_per_sector_shift
    sectors_per_cluster= 1 << sectors_per_cluster_shift

    # --- 2) Compute LBA of first root‐directory sector ---
    first_dir_lba = cluster_heap_offset \
                  + (root_dir_cluster - 2) * sectors_per_cluster

    # --- 3) Read the entire first directory cluster ---
    data = bytearray()
    for i in range(sectors_per_cluster):
        sector = read_raw_sector(first_dir_lba + i)
        assert len(sector) == bytes_per_sector
        data.extend(sector)

    # --- 4) Walk through every 32‐byte directory entry and collect types ---
    entry_types = [data[offset] for offset in range(0, len(data), 32)]

    # --- 5) Assert that each metadata entry is present somewhere ---
    assert 0x83 in entry_types, "Missing Volume Label entry (0x83)"
    assert 0x81 in entry_types, "Missing Allocation Bitmap entry (0x81)"
    assert 0x82 in entry_types, "Missing Up-case Table entry (0x82)"
