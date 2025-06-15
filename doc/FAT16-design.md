# FAT16 based design

This file documents our attempt to make a FAT16 based design.

Unfortunately this design turned out to "feel" unnecessarily complex.
However, we learned a fair number of leassons and therefore are 
keeping this design here, also for the lessons learned.

## Mapping virtual disk LBAs to MCU address space

Because the metadata region always sits in the beginning of the virtual disk address space
and because the MCU address space is quite large (4 Gb), the logical block addresses (LBAs) 
cannot be directly mapped to the MCU address space.

In RP3250 case, the memories in the MCU address space are as follows:

+---------+------------+----------------+
| Segment | Base       | Length (max)   |
+---------+------------+----------------+
| ROM     | 0x00000000 | 32 kb (???)    | 
| Flash   | 0x10000000 | 2 Mb (64 Mb)   |
| SRAM    | 0x20000000 | 520 kb (2 Mb?) |
+---------+------------+----------------+

There are other segments higher in the MCU address space, providing access to the peripherals,
but we are not interested in them, for two reasons. Firstly, the host will cache data,
making real-time access through the USB "disk" interface impossible. Secondly, reading 
registers may have unwanted side effects.

Given this, to simplify our design as much as possible, for forward compatibility we 
assume that there are three memory spaces, each having a maximum size of 64 Mb,
starting at `0x0000 0000` (ROM), `0x1000 0000` (flash), `0x2000 0000` (SRAM).

## FAT design choices

FAT16 was chosen for simplicity.
- A good choice for volumes up to a few tens of MiB:  
  - FAT12 becomes unwieldy above ~16 MB (huge clusters, compatibility quirks).  
  - FAT32 carries extra overhead (FSInfo, larger reserved region) and is more complicated.
  - **FAT16** handles up to ~2 GB with a minimal BPB.
  - VFAT long-filename extensions supported.

### Cluster size: 4 KiB (8 × 512 B)

- **Aligns neatly** with 4 kB flash pages → no unaligned reads or memcpy tricks.  
- Modern OSs respect the BPB’s `SectorsPerCluster` precisely.

### FAT table considerations

The hardest design choice was to define how to construct the FAT tables algorithmically.
Representing each file itself is easy, as we need to know the first cluster and the lenght of the
file in clusters, with each FAT entry pointing to the next cluster, up till the last entry of `0xffff`.

The real issue was to place the logical files — FAT clusters — on the virtual disk address space
in such a way that the LBAs map trivially to the MCU address space.

To understand how, consider how the host reads a file. It gets a file's first cluster
and length from a directory entry. Then it reads the FAT entry for the first cluster,
finding the second cluster, etc, until it reaches the end-of-file marker `0xffff`.  
Given a list of clusters, the host makes a number of reads, addressing the disk by the LBAs.

To generate the FAT on the fly, whenever the host asks for a sector that belongs to the FAT, 
we figure out the clusters covered and determine if there are any logical files there.
Where thera re no files, we fill the FAT with zeroes.
If there is a file, we fill the FAT with sequential cluster numbers, until the end of file.  
(**NB.** We also considered a design where the FAT is just a simple sequence of 
cluster numbers, cluster N always pointing to N+1, with no zeroes or EOFs.
That might work, but it might also confuse some hosts.)

Hence, we need to consider together two algorithms:

1. Given an LBA pointing to FAT, how to generate the FAT block?
2. Given an LBA pointing to data, how to map the LBA to an MCU address?

### Data address space mapping

The maximum MCU memory segment size of 64 MB yields 128k disk segments of 512 bytes, or 16k clusters
per segment.

+----------+-------------+-----------+---------------+
| Region   | MCU Address | First LBA | First cluster |
+----------+-------------+-----------+---------------+
| Metadata |           – | 0x00000   | 0             |
| free     |           – | TBD       | 2             |
| ROM      |  0x00000000 | 0x10000   | 0x2000 - C2   |
| Flash    |  0x10000000 | 0x30000   | 0x6000 - C2   |
| SRAM     |  0x20000000 | 0x50000   | 0xA000 - C2   |
| future   |  0x30000000 | 0x70000   | 0xE000 - C2   |
+----------+-------------+-----------+---------------+

This gives us three full sized data segments, each being of max 64 MB, and one 
smaller one for future expandability, with a total logical disk size
of 256 MB. This fits nicely with our desire to use FAT16 with 4 kB clusters.

The clusters `2 … (0x2000 - C2 - 1)` are free for future use.

Computing the MCU address from the LBA becomes relatively easy:
`Addr = ((LBA - 0x10000) & 0xE0000 << 7) | ((LBA - 0x10000) & 0x1FFFF) << 9`.

This design necessitates us to have the maximum size FAT tables for FAT16.
For `0xFFF0 = 65520` cluster pointers, we need 256 sectors.
With two identical FATs, the total size of the FAT tables is 512 sectors.

At this point, the main remaining question was to decide at which LBA to put Cluster 2,
or what is the value of `C2` in the table, which also determines the value for `TBD`. 
(In FAT, Cluster numbers 0 and 1 are reserved. Cluster 2 is the first data
cluster, immediately after metadata.)

One option would be to have metadata to cover all the disk space up to
the start of the ROM at `LBA = 0x10000`. However, we don't want overly long metadata. 
Therefore, we leave a number of clusters from cluster `2` to `0x2000-C2` as empty. 
In the future, that region could also be used to generate read-only files on the fly.

### Algorithms

When the host reads a data sector, indexed by an LBA, we need to compute the address.

```
Addr 	= ((LBA – 0x10000) & 0xE0000 << 7) | ((LBA - 0x10000) & 0x1FFFF) << 9
```

While we don't need to compute LBAs from addresses or convert LBAs to
clusters or vice versa, the formulas are the following.

```
LBA	= ((Addr & 0xE0000000) >> 7) | (Addr & 0x03FFFE00) >> 9) + 0x10000
LBA	= ((Addr >> 7) & 0x01C00000) | (Addr >> 9) & 0x0001FFFF) + 0x10000

LBA	= (Clust + C2) << 3
Clust	= (LBA >> 3) – C2
```

When the host reads a directory entry, we need to populate the directory
entry with the right first cluster, computed from the partition address.

```
Clust = (Addr >> 10) & 0x00380000) | (Addr >> 12) & 0x00003FFF) + 0x00002000 - C2
```

We never need to compute the address from the cluster, as the host will not
address anything with the clusters, only with LBAs.

When generating FAT entries, we need to know when start to populate them
and when to stop. The values themselves are trivial, as in our design each
FAT entry points to the next one.

Hence, for FAT entry generation, we need a sorted table with precomputed
cluster values that allow us to decide when to start generating a file and
when to stop. Furthermore, in the future it would be good to be able to
do this optionally in with PIO & DMA, freeing the main CPU while generating a FAT table.

### Number of directory entries and first (empty) data cluster LBA

To place Cluster 2 at a "nice" LBA, we can play with the number of directory entries.
Each directory entry is 32 B, with a 512 B sector having 16 directory entries.

- Each BootROM partition becomes a single file in the root directory:  
  - The **filename** is a truncated 8.3 name (e.g. `PART01.BIN`)   
  - The **starting cluster** in the directory entry is computed from the partition’s first flash page.  
  - The **file size** is set to the partition length in bytes.

RP3250 partition names may be up to 127 UTF8 bytes long, mapping up to 254 Unicode characters,
thereby requiring up to `254 / 13 = 20` VFAT entries.  
With 16 partitions, we need `16 * (20 + 1) = 336` directory entries, 
taking `336 / 16 = 21 sectors`.  

However, it would be better to have more root directory entries, as we have ample space.
In FAT16, the number of root directory entries is limited to 64k or 256 sectors.

Hence, the minimum size for the metadata is `1 + 512 + 21 = 534` sectors, with expansion
to 8 sector boundary giving `534 / 8 ≤ 67`,
and the maximum number of used sectors is `1 + 512 + 256 = 769` sectors, with `769 / 8 ≤ 97`.

In addition to the used sectors, the received sector count in the BPB allows
additional sectors to be reserved between the BPB and the first sector of the FAT.
For FAT16, this feature is typically not used, but it may be used.
With the extreme case of `64k-1` reserved sectors, the maximu size for the 
metadata is `1 + 65535 + 512 + 256 = 66 304` sectors, with `66304 / 8 = 8288 = 0x2060`. 

Consequently, C2 should be between `67 - 2` and `0x2000`. 

The following table gives some example value we considered.

+----------+----------+––––--+----------+-----------------+------------+------------+
| Desing   | Metadata |   C2 | unused   | free            | free       | First data |
|          | secs     |      | sectors  | sectors         | clusters   | cluster #  |
+----------+----------+––––--+----------+-----------------+------------+------------+          
| Min dirs |      536 |   65 |      1–2 |    536 – 0xFFFF | 2 – 0x1FBE |     0x1FBF |
| Max dirs |      776 |   95 |      1–7 |    776 – 0xFFFF | 2 – 0x1FA0 |     0x1FA1 |
+----------+----------+––––--+----------+-----------------+------------+------------+          
| FA       |      784 |   96 |     1–15 |    784 – 0xFFFF | 2 – 0x1F9F |     0x1FA0 |
| F8       |     1040 |  128 |    1–271 | 0x0410 – 0xFFFF | 2 – 0x1F7F |     0x1F80 |
| F0       |     2064 |  256 | 1–0x050F | 0x0810 – 0xFFFF | 2 – 0x1EFF |     0x1F00 |
| 80       |    16400 | 2048 | 1–0x3D0F | 0x4010 – 0xFFFF | 2 – 0x17FF |     0x1800 |
| 00       |    32784 | 4096 | 1–0x7D0F | 0x8010 – 0xFFFF | 2 – 0x0FFF |     0x1000 |
| Max      |    65536 | 8190 | 1–0xFCFF |                 |            |          2 |
+----------+----------+––––--+----------+-----------------+------------+------------+          

Given this, the algorithms above, and the fact that unused sectors "waste" address space
and reduce the ability to use free sectors for future functionality, we chose the design "FA".

At this point, we started to feel that this design will be quite complex and that
actually a FAT32 or ExFAT based design might be simpler.

### Lessons learnt during this early design

- **On-demand generation** of FAT and directory data avoids large static arrays.  
- **Alignment matters:** matching cluster size to flash page size simplifies cache logic.  
- **File EOC markers in FATs are essential** for compatibility, but only a handful of patch writes are needed per generated FAT sector.  
- **Flexibility** to adjust metadata window (we chose LBA 0–255) gives headroom for future memory regions (RAM, extra flash).
