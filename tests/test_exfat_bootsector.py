# """
# tests/test_exfat_bootsector.py
#
# Pytest suite for validating the exFAT boot sector binary output.
#
# This test module:
#   - Locates the compiled 512-byte boot sector image (bootsector.bin) in expected build directories.
#   - Verifies the image is exactly 512 bytes.
#   - Checks the JumpBoot opcode and the "EXFAT   " filesystem name signature.
#   - Reads and asserts key header fields per the exFAT specification:
#       • PartitionOffset and VolumeLength
#       • FATOffset, FATLength, ClusterHeapOffset, and ClusterCount
#       • RootDirectoryCluster, FileSystemRevision, BytesPerSectorShift, SectorsPerClusterShift, NumberOfFATs, PercentInUse
#   - Confirms the final boot signature (0xAA55).
# """
# 

import os
import struct
import pytest

def test_bootsector_size(bootsector_data):
    """Boot sector must be exactly 512 bytes."""
    assert len(bootsector_data) == 512

def test_jump_boot_and_fs_name(bootsector_data):
    """Validate JumpBoot instruction and FileSystemName."""
    # JumpBoot: JMP + NOP (0xEB 0x76 0x90)
    assert bootsector_data[0:3] == b'\xEB\x76\x90'
    # FileSystemName: "EXFAT   "
    assert bootsector_data[3:11] == b'EXFAT   '

def test_partition_and_volume_length(bootsector_data):
    """PartitionOffset should be 0; VolumeLength computed per spec."""
    partition_offset = struct.unpack_from('<Q', bootsector_data, 64)[0]
    volume_length    = struct.unpack_from('<Q', bootsector_data, 72)[0]
    assert partition_offset == 0
    # VolumeLength = ClusterHeapOffset + ClusterCount * SectorsPerCluster
    expected_volume_length = (256 * 1024) * 8
    assert volume_length == expected_volume_length

def test_fat_and_cluster_fields(bootsector_data):
    """Check FATOffset, FATLength, ClusterHeapOffset, and ClusterCount."""
    fat_offset, fat_length, cluster_heap_offset, cluster_count = struct.unpack_from('<IIII', bootsector_data, 80)
    assert fat_offset          == 0x18
    assert fat_length          == 0x800
    assert cluster_heap_offset == 0x8010
    assert cluster_count       == 256 * 1024 - (0x8010 / 8)

def test_filesystem_parameters(bootsector_data):
    """
    RootDirectoryCluster, FS revision, shifts, number of FATs,
    percent in use, and boot signature.
    """
    fs_revision                 = struct.unpack_from('<H',  bootsector_data, 104)[0]
    bytes_per_sector_shift      = bootsector_data[108]
    sectors_per_cluster_shift   = bootsector_data[109]
    number_of_fats              = bootsector_data[110]
    percent_in_use              = bootsector_data[112]
    boot_signature              = struct.unpack_from('<H', bootsector_data, 510)[0]

    assert fs_revision               == 0x0100
    assert bytes_per_sector_shift    == 9
    assert sectors_per_cluster_shift == 3
    assert number_of_fats            == 1
    assert percent_in_use            == 0xFF
    assert boot_signature            == 0xAA55

def test_root_directory_follows_metadata(
    allocation_bitmap_entry,
    upcase_table_entry,
    volume_label_entry,
    bootsector_data
):
    """
    Ensure root directory cluster is after all metadata entries.
    """
    import struct

    def entry_start_cluster(entry):
        # Cluster field is at offset 20 (little-endian 4 bytes)
        return struct.unpack_from("<I", entry, 20)[0]

    metadata_clusters = [
        entry_start_cluster(allocation_bitmap_entry),
        entry_start_cluster(upcase_table_entry),
        entry_start_cluster(volume_label_entry),
    ]

    root_dir_cluster = struct.unpack_from("<I", bootsector_data, 96)[0]
    assert root_dir_cluster > max(metadata_clusters)
