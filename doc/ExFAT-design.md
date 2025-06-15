# ExFAT based design

This file documents our ExFAT based design.

## Mapping virtual disk to MCU address space

ExFAT supports a huge address space.

However, the metadata region always sits in the beginning of the virtual disk address space.
Hence, we cannot map one-to-one the MCU addresses to the virtual disk address space.

In RP3250 case, the memories in the MCU address space are as follows:

| Segment | Base       | Length (max)   |
| ------- | ---------- | -------------- |
| ROM     | 0x00000000 | 32 kb (???)    | 
| Flash   | 0x10000000 | 2 Mb (64 Mb)   |
| SRAM    | 0x20000000 | 520 kb (2 Mb?) |

There are other segments higher in the MCU address space, providing access to the peripherals,
but we are not interested in them. 
The host will cache data anyway, making real-time access through the USB "disk" interface impossible. 
Secondly, reading registers may have unwanted side effects.

Given this, to simplify our design as much as possible, for forward compatibility we 
assume that there are three memory spaces, each having a maximum size of 64 Mb,
starting at `0x0000 0000` (ROM), `0x1000 0000` (flash), `0x2000 0000` (SRAM).

There are two options of covering these:
1. As 64 Mb memory spaces, back-to-back, totalling 128 Mb
2. As 256 Mb memory spaces, including gaps, totalling 768 Mb.

The latter has the benefit of making LBA to memory mapping trivial.

## Cluster and sector sizes: 4 kB, 8 × 512 B

We decided to use the cluster size of 4 kB and sector size of 512 B.
There is no choice for the sector size, as Pico [usb_device_msc.h]
(https://github.com/raspberrypi/pico-extras/blob/master/src/rp2_common/usb_device_msc/include/pico/usb_device_msc.h#L10)
defines `SECTOR_SIZE` as `512u`.

Notes:
- **Aligns neatly** with 4 kB flash pages → no unaligned reads or memcpy.
- Reasonable DMA reads.
- Modern OSs respect the BPB’s `SectorsPerCluster` precisely.

## Address space mapping

The easiest approach seems to map the 768 Mb of MCU address space 
directly to the virtual disk address space.
For that, we have two options:
1. Map at a sector offset. The offset is needed so that the metadata can sit in the beginning of the disk.
2. Map directly, but move the ROM segment at `0x00000000` to another virtual disk address.

We chose the second approach, especially since providing the ROM image can be made a 
compile time option, further minimizing the implementation if it is left out. 
It is also so small, and will be, that it fits almost anywhere.

Hence, in terms of address mapping, we use the following:

| Segment | MCU addr   | VD Addr    | Sector index (LBA) | Length |
| ------- | ---------- | ---------- | ------------------ | ------ |
| ROM     | 0x00000000 | varies     | varies             | varies |
| Flash   | 0x10000000 | 0x10000000 | 0x080000           | 256 Mb |
| SRAM    | 0x20000000 | 0x20000000 | 0x100000           | varies |

Depending on the compile-time options (flash-only, also SRAM, etc)
the apparent virtual disk size can be anything from `256 + 2` Mb to
`768` Mb, or even larger if so desired.

### FAT table considerations

With exFAT, we don't need to generate FAT tables. Instead, we can use `NoFatChain` entries,
as defined in [§6.3.4.2]
(https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/FileIO/exfat-specification.md#6342-nofatchain-field).
With that, "the corresponding FAT entries for the clusters are invalid and implementations shall not interpret them".

However, we need to reserve space for the primary FAT table, as some host implementations
may assume they the FAT table is there and may get confused if there is none.

To make things easy, we reserve FAT table space for (almost) 1 GB or 256k clusters.
This is well below the Microsoft recommendation of [at most 16M clusters.]
(https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/FileIO/exfat-specification.md#319-clustercount-field)

To cover that many cluster indices, of 4 bytes each, our 
virtual, non-existent FAT table occupies 1 Mb of the VD address space.
It doesn't matter where we place it, as sane hosts will never read it.

### Cluster mapping

According to [§3.1.5]
(https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/FileIO/exfat-specification.md#316-fatoffset-field),
and [§3.1.8]
(https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/FileIO/exfat-specification.md#318-clusterheapoffset-field)
the FAT table offset must be at least 24 sectors and the data area (cluster heap)
starts at a natural boundary after the FAT(s).

In the data area, the so-called cluster heap, the first cluster index is 2.
However, we prefer to align also the cluster indices nicely with the MCU memory addresses.
That helps us to compute the file directory entries on the fly.

With 24 reserved sectors and 2048 FAT table sectors, 
we can place cluster #2 at any sector index at or beyond `24 + 2048 = 2072 = 0x818`.

Below is the resulting virtual‐disk layout when choosing the location for cluster #2, 
leaving an unused gap before the cluster heap, to align flash and SRAM regions to “nice” cluster indices:

| Segment          | LBA Start   | LBA End     | MCU Addr Start | MCU Addr End  | Cluster Indices    |
| ---------------- | ----------- | ----------- | -------------- | ------------- | ------------------ |
| **Metadata**     | 0x00000     | 0x00017     | —              | —             | —                  |
| **FAT area**     | 0x00018     | 0x00817     | —              | —             | —                  |
| **Gap**          | 0x00818     | 0x0800F     | —              | —             | —                  |
| **Metadata 2**   | 0x08010     | 0x0806F     | —              | —             | 2 – 13             |
| **Free clusters**| 0x08070     | 0x7FFFF     | —              | —             | 14 – 0xEFFF        |
| **Flash**        | 0x80000     | 0x80FFF     | 0x10000000     | 0x1001FFFF    | 0xF000 – 0xF1FF    |
| **Unused**       | 0x81000     | 0xFFFFF     | –              | –             | 0xF200 – 0x1EFFF   |
| **SRAM**         | 0x100000    | 0x10040F    | 0x20000000     | 0x20081FFF    | 0x1F000 – 0x1F081  |

This layout sets the cluster‐heap offset to 32784 sectors (`0x8010`) so that 
`cluster_index = 0xF000 + page_number` maps exactly to flash page LBAs. 
The small “Gap” region is unused padding before the cluster heap. 
By choosing the value of `0xF000`, calculating the `first_cluster` for a partition 
becomes a simple `0xF000 + start_page` computation.  

The MSC callback can directly translate LBA the to MCU addresses, with a left shift.

Please note that it does not matter at all where the FAT table is, as it will be never read by
any sane host.

## Root directory, up-case table and allocation bitmap

### Length of the root directory

The most complex piece of data to generate, to fulfil the ExFAT specification, is the
root directory and the data structures (upcase table and allocation bitmap) referred from it.

Some of the root directory entries we can construct at the compile time, 
including those for the allocation bitmap, up-case table, and volume label.
Most of the actual file, stream extension and file name entries need to be generated
on the fly, at runtime, based on the data from the partition table.
The file, stream extension and file name entries for the optional non-partion files,
includling `ROM.BIN` and `SRAM.BIN` can be constructed at the compile time.  As there
are just a few of these and they are each likely to take only 3 * 32 = 96 bytes,
it is unlikely that space would be saved if trying to construct them at runtime.

For each partition, the following directory entries are needed:
- file and directory entry (1)
- stream extension entry (1)
- file name entries (1–17): maximum name length 127 / characters per entry 15 = 17

The minimum number of root directory entries is XXX, as follows:
- mandatory entries (3): allocation bitmap, up-case table, volume label
- nice-to-have entries (2): volume label, volume GUID
- file entries for bootrom partitions (304): 16 x 19, for each partition file entry, stream extension, and 1–17 name entries

For each additional file, such as `ROM.BIN` or `SRAM.BIN`, usually 3 entries are needed, unless the name is very long.

Hence, typically we need to reserve a minimum of 308 entries, needing at least 20 sectors or 3 clusters.
Consequently, we use 3 full clusters, giving us 384 directory entries in the root directory.

### Up-case table

For the upcase table, we use the minimal method in [§7.2.5 Table 24]
(https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/FileIO/exfat-specification.md#725-up-case-table)
Compressed, that takes only 60 bytes.

Out of necessity, we need to preserve one cluster for the upcase table.

### Allocation bitmap

For the allocation bitmap, if the host ever queries it, we simply lie and say that all clusters are allocated.
This makes the disk to appear full, which is good because it is read only.

In the future, if we want so support also file writing (e.g. for `UF2` files), we 
can "free" a specific section of the allocation bitmap, "forcing" the host to allocate
clusters there.  But this may be quite tricky, as different hosts may do that in 
different ways, including updating the directory…

As the virtual root directory, the virtual upcase table and allocation bitmap are also
located at the **Free clusters** section above.

To correspond with the FAT table, we support (almost) 1 GB of data or 256k clusters.
Storing 256k bits takes 32k bytes, 64 sectors, 8 clusters. 

### Placement

We place allocation bitmap at Cluster #2, then up-case table, then root directory.  
This follows their `TypeCode` numbers and appears to be the recommended order.
Alignment doesn't matter as the corresponding LBAs need only to be recognised, not mapped on memory.

| Segment               | LBA Start | LBA End  | MCU Addr Start | MCU Addr End | Cluster Indices   |
| --------------------- | --------- | -------- | -------------- | ------------ | ----------------- |
| **Allocation bitmap** | 0x08010   | 0x0804F  | —              | —            | 0x0002–0x0009     |
| **Up-case table**     | 0x08050   | 0x08057  | —              | —            | 0x000A            |
| **Root directory**    | 0x08058   | 0x0806F  | —              | —            | 0x000B–0x000D     |

## Placement of ROM (optional)

Since the ROM resides at MCU address `0x00000000` and the VD metadata occupies the early LBAs, we cannot map it directly. Instead, we reserve a compile-time cluster index, `C_ROM`, within the free cluster range. The directory entry for `ROM.BIN` uses `first_cluster = C_ROM` and `data_length = ROM_SIZE`.

When the host reads clusters of `ROM.BIN`, the MSC callback computes the byte offset into the ROM image as follows:

```text
rom_cluster_index        = 2 + (lba - CLUSTER_HEAP_OFFSET) / SECTORS_PER_CLUSTER
rom_offset_in_clusters   = rom_cluster_index - C_ROM
rom_address              = rom_offset_in_clusters * CLUSTER_SIZE
```

(**NB.** That algorithm does not work if the LBA is not at the beginning of a cluster...)

The callback then the requested sectors starting at the MCU address `rom_address`
(as the ROM starts at zero) into the MSC buffer. 

When used, this splits the **Free clusters** above into three, some free clusters before the ROM, the ROM, and some after it.

It the same way, additional "files" may be layed out into the **Free clusters** area, as desired.

## Algorithms

When the host reads a data sector, indexed by an LBA, we need to compute the address.
When the ROM (nor other extra files) are used, the mapping is trivial:
```
   flash_or_sram_address = LBA * 512 = LBA << 9.
```

When the ROM is used, the mapping is a bit more involved. To derive it:
```
  rom_address = ((2 + (LBA - CLUSTER_HEAP_OFFSET) / SECTORS_PER_CLUSTER) - C_ROM) * SECTORS_PER_CLUSTER * SECTOR_SIZE
  rom_address = ((2 + (LBA - CLUSTER_HEAP_OFFSET) / SECTORS_PER_CLUSTER) * SECTORS_PER_CLUSTER - C_ROM * SECTORS_PER_CLUSTER)  * SECTOR_SIZE
  rom_address = ((2 * SECTORS_PER_CLUSTER + (LBA - CLUSTER_HEAP_OFFSET)) - C_ROM * SECTORS_PER_CLUSTER)  * SECTOR_SIZE
```
which yields to:
```
  rom_address = LBA * SECTOR_SIZE 
                + (2 * SECTORS_PER_CLUSTER - CLUSTER_HEAP_OFFSET - C_ROM * SECTORS_PER_CLUSTER) * SECTOR_SIZE
```
where `ROM_ADDRESS_OFFSET = (2 * SECTORS_PER_CLUSTER - CLUSTER_HEAP_OFFSET - C_ROM * SECTORS_PER_CLUSTER) * SECTOR_SIZE` 
is a negative compile time constant in our case.

### Read routine outline

In C pseudocode, our msc_read (10) routine, using the values selected above,
will look something like the following:

```c
typedef struct {
    uint32_t start_lba;
    read_fn_t handler;
} lba_region_t;

// LBA regions table (sorted by start_lba), with sentinel
static const lba_region_t region_table[] = {
    { 0x00000, exfat_generate_md },       // metadata + FAT area
    { 0x00018, exfat_generate_fat },      // metadata + FAT area
    { 0x00818, exfat_bad_lba },           // gap
    { 0x08010, exfat_bad_lba },           // free clusters before ROM
    { 0x08010, exfat_generate_alloc_bm }, // allocation bitmap
    { 0x08050, exfat_generate_upcase },   // up-case table
    { 0x08058, exfat_generate_rootdir },  // root directory
#ifdef EXFAT_FEATURE_ROM_FILE
    { ROM_LBA_START, exfat_read_rom },    // optional ROM
    { ROM_LBA_END,   exfat_bad_lba },     // free clusters after ROM
#endif
    { 0x80000,  exfat_read_flash },       // flash region
    { 0x81000,  exfat_bad_lba },          // beyond current flash
#ifdef EXFAT_FEATURE_SRAM_FILE
    { 0x100000, exfat_read_sram },        // SRAM region
    { 0x100410, exfat_bad_lba },          // beoynd current SRAM
#endif 
    { UINT32_MAX, exfat_bad_lba }         // sentinel
};

void msc_read_callback(uint32_t lba, uint8_t *buf) {
    for (size_t i = 0; ; ++i) {
        uint32_t start = region_table[i].start_lba;
        uint32_t end   = region_table[i + 1].start_lba - 1;
        if (lba >= start && lba <= end) {
            region_table[i].handler(lba, buf);
            return;
        }
    }
}
```

**NB. This code can be both simplified and generalised a bit. It is so in the actual implementation.**

### File directory entry outline

TBD


## Boot sector

The boot sector is the first sector of the virtual disk (LBA 0).
It defines the BIOS Parameter Block (BPB) and other volume metadata 
required by hosts to recognize and mount the exFAT filesystem.

It consists of:
- **JumpBoot (3 bytes):** An x86-style jump instruction (`0xEB 0x76 0x90`).
- **FileSystemName (8 bytes):** The ASCII string `"EXFAT   "` identifying the filesystem type.
- **Reserved (53 bytes):** Must be zero-initialized, for compatibility with earlier FAT formats.
- **PartitionOffset (8 bytes):** The starting sector of the filesystem area; set to zero and ignored.
- **VolumeLength (8 bytes):** Total number of sectors in the volume, computed as `CLUSTER_HEAP_OFFSET + CLUSTER_COUNT × SectorsPerCluster`.
- **FATOffset (4 bytes):** Sector index of the first FAT. Set to 24, immediately after the reserved sectors.
- **FATLength (4 bytes):** Length of the FAT area in sectors; sized to cover up to 256 k clusters (1 GB of data) even though the FAT entries are never actually used.
- **ClusterHeapOffset (4 bytes):** Sector index where the cluster heap begins; aligned at `0x8010` to map cluster indices directly to MCU flash pages.
- **ClusterCount (4 bytes):** Total number of clusters in the heap (`256 k` in our default configuration).
- **RootDirectoryCluster (4 bytes):** Starting cluster of the root directory; we reserve clusters 2–10 for allocation bitmap (8 clusters) and up-case table (1 cluster), so this is 11.
- **VolumeSerialNumber (4 bytes):** A runtime-generated 32-bit serial (e.g. derived via the BootROM `get_sys_info` random field) to ensure volume-caching hosts detect changes across resets or layout updates.
- **FileSystemRevision (2 bytes):** Version of the exFAT spec; set to `0x0100` for exFAT v1.0.
- **VolumeFlags (2 bytes):** Currently zero; no special flags set.
- **BytesPerSectorShift (1 byte):** Log₂ of the sector size; `9` to represent 512 B sectors.
- **SectorsPerClusterShift (1 byte):** Log₂ of the sectors per cluster; `3` to represent 8 sectors per cluster (4 KB).
- **NumberOfFats (1 byte):** Number of FAT tables; `1` in this design.
- **DriveSelect (1 byte):** BIOS drive number; zero as this is not a fixed disk.
- **PercentInUse (1 byte):** Bitmap of cluster usage; set to `FF` since the disk is read-only this is not used.
- **Reserved (7 bytes):** Must be zero.
- **Boot code (390 bytes):** Unused filler (zeros) since no executable boot code is required.
- **BootSignature (2 bytes):** Magic signature `0xAA55` marking a valid boot sector.

All numeric fields are little-endian and match the layouts defined by the Microsoft exFAT specification. 

