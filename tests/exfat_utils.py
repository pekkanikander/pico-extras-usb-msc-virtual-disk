# tests/exfat_utils.py

import os
import struct

def raw_sector_reader(device_path=None):
    """
    Returns a function read_sector(lba) that opens device_path or raises IOError.
    """
    def read_sector(lba):
        if not device_path:
            raise IOError("No device specified")
        with open(device_path, "rb") as f:
            f.seek(lba * 512)
            data = f.read(512)
        if len(data) != 512:
            raise IOError(f"Short read: {len(data)} bytes")
        return data
    return read_sector

def _compute_exfat_checksum(sectors, skip_indices_in_sector0=()):
    """
    Compute the 32-bit checksum over a list of 512-byte sectors using exFAT algorithm.
    For sector 0, bytes in skip_indices_in_sector0 will be skipped.
    """
    checksum = 0
    for lba, data in enumerate(sectors):
        for index in range(len(data)):
            if lba == 0 and index in skip_indices_in_sector0:
                continue
            byte = data[index]
            checksum = ((checksum >> 1) | ((checksum & 1) << 31)) & 0xFFFFFFFF
            checksum = (checksum + byte) & 0xFFFFFFFF
    return checksum

def compute_vbr_checksum(sectors):
    """
    Compute the 32-bit checksum over sectors 0–10 as specified in exFAT spec §3.5.5,
    ignoring VolumeFlags (offsets 106–107) and PercentInUse (offset 112) in sector 0.
    """
    return _compute_exfat_checksum(sectors, skip_indices_in_sector0={106, 107, 112})

def compute_upcase_checksum(data):
    """
    Compute the 32-bit checksum over the Up-case Table byte stream using exFAT algorithm.
    """
    return _compute_exfat_checksum([data])

def find_directory_entry(read_raw_sector, bootsector_data, entry_type):
    """
    Locate and return the raw 32-byte directory entry with the given entry_type
    (e.g. 0x81 for allocation bitmap, 0x82 for up-case table, 0x83 for volume label)
    from the root directory cluster. Returns bytes or None if not found.
    """
    # Parse necessary boot-sector fields
    cluster_heap_offset = struct.unpack_from('<I', bootsector_data, 88)[0]
    root_dir_cluster    = struct.unpack_from('<I', bootsector_data, 0x60)[0]
    spc_shift           = struct.unpack_from('<B', bootsector_data, 0x6D)[0]
    sectors_per_cluster = 1 << spc_shift

    # Compute LBA of first root-directory sector
    root_lba = cluster_heap_offset + (root_dir_cluster - 2) * sectors_per_cluster

    # Read entire root directory cluster
    data = bytearray()
    for i in range(sectors_per_cluster):
        data.extend(read_raw_sector(root_lba + i))

    # Scan 32-byte entries for matching entry_type
    for offset in range(0, len(data), 32):
        if data[offset] == entry_type:
            return bytes(data[offset:offset+32])
    return None

def cluster_chain_reader(read_sector, bootsector_data, start_cluster):
    """
    Returns a function to read a sequential cluster chain starting from `start_cluster`.

    Assumes clusters are sequential and contiguous. If fragmentation is detected,
    raises an error.

    :param read_sector: callable that takes an LBA and returns 512 bytes
    :param bootsector_data: 512 bytes of boot sector, to extract layout
    :param start_cluster: first cluster of the chain
    :return: function(data_length) -> bytes
    """
    import math

    bytes_per_sector = 1 << bootsector_data[108]
    sectors_per_cluster = 1 << bootsector_data[109]
    cluster_heap_offset = struct.unpack_from("<I", bootsector_data, 88)[0]

    def _read_chain(data_length):
        total_clusters = (data_length + bytes_per_sector * sectors_per_cluster - 1) // (bytes_per_sector * sectors_per_cluster)
        result = bytearray()

        for i in range(total_clusters):
            cluster_num = start_cluster + i
            lba = cluster_heap_offset + (cluster_num - 2) * sectors_per_cluster
            for s in range(sectors_per_cluster):
                result += read_sector(lba + s)

        return bytes(result[:data_length])

    return _read_chain
