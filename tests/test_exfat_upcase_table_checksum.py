"""
tests/test_exfat_upcase_table_checksum.py

Pytest suite for validating the exFAT Up-case Table checksum.

This test module:
  - Locates the Up-case Table via the root directory entry.
  - Reads all sectors of the Up-case Table based on its cluster and length.
  - Computes the checksum using the exFAT-specified algorithm (32-bit additive).
  - Compares against the 4-byte checksum stored in the directory entry.
  - Also provides a __main__ hook to print parameters for manual inspection.
"""

import struct
import pytest

from exfat_utils import cluster_chain_reader, compute_upcase_checksum, raw_sector_reader

def test_upcase_table_checksum(
    upcase_table_entry,
    cluster_chain_reader
):
    """
    The computed checksum over the Up-case Table must match the stored value.
    """
    # Extract start cluster, length, and stored checksum from the 32-byte directory entry
    start_cluster   = struct.unpack_from("<I", upcase_table_entry, 20)[0]
    data_length     = struct.unpack_from("<Q", upcase_table_entry, 24)[0]
    stored_checksum = struct.unpack_from("<I", upcase_table_entry, 4)[0]

    # Read entire up-case table as one bytes object
    read_chain = cluster_chain_reader(start_cluster)
    upcase_data = read_chain(data_length)

    # Compute checksum as per exFAT spec
    computed_checksum = compute_upcase_checksum(upcase_data)
    assert computed_checksum == stored_checksum

if __name__ == "__main__":
    # Standalone utility to inspect up-case table parameters
    import sys
    if len(sys.argv) < 2:
        print("Usage: python test_exfat_upcase_table_checksum.py <device_path>")
        sys.exit(1)

    device_path = sys.argv[1]
    read_sector = raw_sector_reader(device_path)

    from exfat_utils import find_directory_entry, cluster_chain_reader

    # Read boot sector to locate root directory cluster
    bootsector = read_sector(0)
    root_dir_cluster = struct.unpack_from("<I", bootsector, 96)[0]

    # Manually locate the up-case table directory entry
    entry = find_directory_entry(read_sector, bootsector, entry_type=0x82)
    start_cluster   = struct.unpack_from("<I", entry, 20)[0]
    data_length     = struct.unpack_from("<Q", entry, 24)[0]
    stored_checksum = struct.unpack_from("<I", entry, 4)[0]

    # Read up-case table and compute checksum
    read_chain = cluster_chain_reader(read_sector, bootsector, start_cluster)
    upcase_data = read_chain(data_length)
    computed_checksum = compute_upcase_checksum(upcase_data)

    print("Up-case Table info:")
    print(f"  Start cluster:    {start_cluster}")
    print(f"  Length:           {data_length} bytes")
    print(f"  Cluster count:    {(data_length + 4095) // 4096}")
    print(f"  Stored checksum:  0x{stored_checksum:08X}")
    print(f"  Computed checksum:0x{computed_checksum:08X}")
    if computed_checksum != stored_checksum:
        print("  ❌ Checksum MISMATCH")
    else:
        print("  ✅ Checksum OK")
