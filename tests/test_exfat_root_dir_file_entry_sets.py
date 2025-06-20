"""
tests/test_exfat_root_dir_file_entry_sets.py

Walk the root-directory cluster, identify every *File Directory Entry Set*
(Primary 0x85 + secondary entries), and verify that it is internally
consistent as required by the Microsoft exFAT specification §7.4.

A   File DirectoryEntry (0x85)  **must** be followed immediately by:
    • exactly *SecondaryCount* secondary entries
    • the first secondary entry **must** be a Stream Extension (0xC0)
    • at least one of the remaining secondary entries **must** be a
      File Name entry (0xC1)
    • only 0xC0 or 0xC1 types are allowed inside the set
    • the EntrySetChecksum stored in the primary entry matches the value
      recomputed per spec §6.3.3
"""

import struct
import pytest


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _read_root_dir_cluster(bootsector_data, read_raw_sector) -> bytearray:
    """Return the complete bytearray for the first root-directory cluster."""
    # --- 1) Parse key boot-sector fields (all little-endian) ---------------
    bytes_per_sector_shift    = struct.unpack_from('<B', bootsector_data, 0x6C)[0]
    sectors_per_cluster_shift = struct.unpack_from('<B', bootsector_data, 0x6D)[0]
    cluster_heap_offset       = struct.unpack_from('<I', bootsector_data, 0x58)[0]
    root_dir_cluster          = struct.unpack_from('<I', bootsector_data, 0x60)[0]

    bytes_per_sector    = 1 << bytes_per_sector_shift
    sectors_per_cluster = 1 << sectors_per_cluster_shift

    # --- 2) Locate first sector of the root directory ----------------------
    first_dir_lba = cluster_heap_offset \
                  + (root_dir_cluster - 2) * sectors_per_cluster

    # --- 3) Read the entire first directory cluster ------------------------
    data = bytearray()
    for i in range(sectors_per_cluster):
        sector = read_raw_sector(first_dir_lba + i)
        assert len(sector) == bytes_per_sector
        data.extend(sector)

    return data


# ---------------------------------------------------------------------------
# EntrySetChecksum helper
# ---------------------------------------------------------------------------

def _compute_entry_set_checksum(data: bytearray, primary_offset: int, entry_count: int) -> int:
    """
    Compute the 16‑bit EntrySetChecksum for a File Directory Entry Set
    as defined in the Microsoft exFAT specification §6.3.3 (Figure 2).

    Parameters
    ----------
    data : bytearray
        Full directory‑cluster contents.
    primary_offset : int
        Byte offset of the primary (0x85) entry within *data*.
    entry_count : int
        Total number of 32‑byte directory entries in the set
        (primary + secondaries).
    """
    checksum = 0
    for i in range(entry_count):
        entry_offset = primary_offset + i * 32
        entry_bytes = data[entry_offset : entry_offset + 32]
        for j, b in enumerate(entry_bytes):
            # Skip bytes 2‑3 of the primary entry (the checksum field itself)
            # entirely.  Per spec §6.3.3 they are *not* rotated into nor added
            # to the running checksum.
            if i == 0 and j in (2, 3):
                continue
            # Rotate existing checksum right by one bit, then add the byte.
            checksum = ((checksum >> 1) | ((checksum & 1) << 15))
            checksum = (checksum + b) & 0xFFFF
    return checksum


# ---------------------------------------------------------------------------
# The actual test
# ---------------------------------------------------------------------------

def test_exfat_root_dir_file_entry_sets(bootsector_data, read_raw_sector):
    """
    Iterate over every 32-byte directory entry in the first root-directory
    cluster and validate each *File Directory Entry Set*.
    """
    data = _read_root_dir_cluster(bootsector_data, read_raw_sector)
    offset = 0
    cluster_size = len(data)

    while offset < cluster_size:
        entry_type = data[offset]

        # 0x00 = Unused entry  → we can safely stop here (spec §7.1.2)
        if entry_type == 0x00:
            break

        # 0x80 = End-of-directory entry  → stop (no active entries after this)
        if entry_type == 0x80:
            break

        # Skip the well-tested metadata single-entry types
        if entry_type in (0x81, 0x82, 0x83):     # Allocation Bitmap, Up-case, Volume Label
            offset += 32
            continue

        # ---------------- Check a File Directory Entry Set -----------------
        if entry_type == 0x85:                   # Primary File DirEntry
            secondary_count = data[offset + 1]
            total_entries_in_set = secondary_count + 1      # primary + secondaries
            set_end_offset = offset + 32 * total_entries_in_set

            # Guard: the set must fit entirely inside the same cluster
            assert set_end_offset <= cluster_size, (
                f"Entry set starting at {offset} crosses cluster boundary"
            )

            # Collect the entry-type byte of every entry in the set
            entry_types = [
                data[offset + 32 * i] for i in range(total_entries_in_set)
            ]

            # 1) First entry must be 0x85 (already guaranteed by condition)
            assert entry_types[0] == 0x85

            # 2) First secondary must be a Stream Extension (0xC0)
            if secondary_count == 0:
                pytest.fail(
                    f"0x85 entry at offset {offset} declares zero secondary entries"
                )
            assert entry_types[1] == 0xC0, (
                f"0x85 entry at {offset}: first secondary is {entry_types[1]:#04x}, "
                "expected 0xC0 Stream Extension"
            )

            # 3) The set must contain ≥1 File Name entry (0xC1)
            assert 0xC1 in entry_types[1:], (
                f"0x85 entry at {offset}: no File Name (0xC1) secondaries found"
            )

            # 4) Only 0xC0 or 0xC1 types are permitted inside the set
            illegal = [
                et for et in entry_types[1:]
                if et not in (0xC0, 0xC1)
            ]
            assert not illegal, (
                f"0x85 entry at {offset}: illegal secondary types {illegal}"
            )

            # 5) SecondaryCount field must match actual number of secondaries
            assert len(entry_types) - 1 == secondary_count, (
                f"0x85 entry at {offset}: SecondaryCount={secondary_count}, "
                f"but {len(entry_types) - 1} secondaries observed"
            )

            # 6) Verify EntrySetChecksum accuracy
            stored_checksum = struct.unpack_from('<H', data, offset + 2)[0]
            computed_checksum = _compute_entry_set_checksum(
                data, offset, total_entries_in_set
            )
            assert computed_checksum == stored_checksum, (
                f"0x85 entry at {offset}: EntrySetChecksum mismatch; "
                f"stored={stored_checksum:#06x}, computed={computed_checksum:#06x}"
            )

            # All good — advance past the entire entry set
            offset = set_end_offset
            continue

        # Any other entry type is unexpected in the root directory
        pytest.fail(
            f"Unexpected directory entry type {entry_type:#04x} at offset {offset}"
        )
