"""
tests/test_exfat_upcase_table_first.py

Pytest suite for validating the first sector of the exFAT up-case table (LBA = Up-case Table start cluster).

This test module:
  - Reads the boot sector to extract ClusterHeapOffset and SectorsPerClusterShift.
  - Computes the LBA of the up-case table cluster (cluster 2 + 8).
  - Reads the first up-case table sector via the `read_raw_sector` fixture.
  - Verifies sector size is 512 bytes.
  - Checks that:
      • codepoint 0x0000 maps to 0x0000,
      • lowercase 'a' → 'A',
      • lowercase 'z' → 'Z'.
"""

import struct

def test_exfat_upcase_table_first(bootsector_data, read_raw_sector, upcase_table_entry):
    # 1) Extract key boot-sector fields
    cluster_heap_offset = struct.unpack_from('<I', bootsector_data, 88)[0]
    sectors_per_cluster_shift = struct.unpack_from('<B', bootsector_data, 109)[0]
    sectors_per_cluster = 1 << sectors_per_cluster_shift

    # 2) Extract FirstCluster from the up-case table directory entry
    first_cluster = struct.unpack_from('<I', upcase_table_entry, 8)[0]
    upcase_lba = cluster_heap_offset + (first_cluster - 2) * sectors_per_cluster

    # 3) Read the first sector of the up-case table
    data = read_raw_sector(upcase_lba)

    # 4) Verify we got exactly one full sector
    assert len(data) == 512

    # 5) Decompress the RLE-compressed up-case table
    mapping = []
    offset = 0
    idx = 0
    data_len = len(data)
    while offset + 2 <= data_len and idx < 0x80:
        entry = struct.unpack_from('<H', data, offset)[0]
        offset += 2
        if entry != 0xFFFF:
            # Explicit mapping
            mapping.append(entry)
            idx += 1
        else:
            # Identity run: next uint16 is run length
            run_len = struct.unpack_from('<H', data, offset)[0]
            offset += 2
            for _ in range(run_len):
                mapping.append(idx)
                idx += 1

    # 6) Check mappings in the mandatory up-case table
    assert mapping[0] == 0x0000         # codepoint 0 → 0
    assert mapping[0x0001] == 0x0001    # codepoint 1 → 1
    assert mapping[0x0041] == 0x0041    # 'A' → 'A'
    assert mapping[0x0060] == 0x0060    # '`' → '`'
    assert mapping[0x0061] == 0x0041    # 'a' → 'A'
    assert mapping[0x007A] == 0x005A    # 'z' → 'Z'
    assert mapping[0x007B] == 0x007B    # '{' → '{'
    assert mapping[0x007F] == 0x007F    # DEL → DEL
