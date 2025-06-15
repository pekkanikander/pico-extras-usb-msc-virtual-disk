# PicoVD - USB "Stick" for RP2350 BootROM Flash Partitions

**Work in progress**

PicoVD lets you mount the RP2350's BootROM flash partitions as read-only `.BIN` files
over USB Mass-Storage (MSC), making your Pico 2 flash to appear as a USB Memory Stick.

PicoVD is designed to be used as a library within your own Pico app but it works also
as a standalon app, running from the SRAM.

## Features

- Reads the RP2350 BootROM flash partition table
- Exposes each partition as a read-only file (`.BIN`)
- Falls back to a single `FLASH.BIN` if no partition table is found
- Optionally shows other memory regions as additional `.BIN` files
- Tiny memory footprint - most metadata generated on the fly

### How it works

1. **Uses BootROM APIs to read the flash partition table**  
2. **Generates a Virtual exFAT disk**  
3. Implements **USB MSC** using TinyUSB MSC

*No large in-RAM structures, no flash writes.*

## Usage

Boot your board to BOOTTSEL. Drop the provided `picovd.uf2` binary to that USB Stick.

**Work in progress**: You have to build the binary ourself, no releases yet.

Your Pico reboots and becomes visible as another USB Stick, `PicoVD`, allowing you to inspect the
flash contents as files.  PicoVD runs from SRAM, not changing the contents of your flash.

**Work in progress**: No partitions visible yet, just an empty directory.

### Using as a library in your own project

**Work in progress**: TBD.

## Overall approach

To provide the flash partitions as virtual read-only files, the library does the following:
- **Calls the BootROM APIs to read the partition table from the flash**
- **Exposes each partition as a read-only exFAT file, or the whole flash as a single read-only file**
- **Presents the filesystem over USB Mass-Storage (MSC)**, 
- **Requires minimal memory**, with all FAT and directory structures generated on the fly  

The library code is packaged into a few source afiles:
- `include/pico/extras/usb_msc_vd.h` - API (TBD not implemented yet)
- `src/vd_exfat_params.h` - compile time parameters
- `src/vd_exfat.h` - data types and internal APIs
- `src/vd_virtual_disk.c` - implements USB MSC virtual disk
   - implements the callback functions for TinyUSB MSC
- `src/vd_exfat.c` - generates exFAT BPB and overall logic
- `src/vd_exfat_dirs.c` - generate mandator exFAT directory entries (for allocation bitmap, upcase table, ...)
- `src/vd_exfat_dirs_pt.c` - generate directory entries specifically for the BootROM flash partition table

The example application consists of a single file:
- `picovd.c` - the example app (TBD to be moved under `examples`)
- `usb_descriptors.c` - example USB descriptors, supporting Pico USB stdio and Pico USB Reset

The design is modular, allowing addition of virtual read-only files or changing the way
the flash partitions are visible, if at all.

## Design choices

Our goals for PicoVD were simplicity, compact memory footprint, and forward compatibility.
As far as we know, the specific approach chosen is original.

For minimality and simplicity, only read-only access is supported.  The design allows
for some writing support in the future, but that is for future study.

### Design goals

**Work in progress**  - XXX remove or edit this subsection once there is working code

1. A lightweight library to be included into any project that supports USB.

Allows a developer (or user) to read the flash contents.
Allows USB MSC access *in parallel* with the Pico SDK USB stdio and Pico USB Reset support.
Small runtime memory requirements.
Minimal code size, just a few kilobytes.

2. Flexible USB descriptors

**Work in progress**

Provide a flexible way to compose USB descriptors.

The basic idea is that each USB library could define its own USB
descriptors as compile-time constants and the USB layer would
compose these into the needed device level configuration, preferably
at compile time.

We presume that the Pico SDK may provide such a flexible way in the future.
Therefore this feature is not a top priority as of now.

3. Flexible virtual file layer

Cleanly separated virtual disk, virtual FAT/metadata and virtual file layers.
Flexibility in generating virtual files, allowing the design to be used also
for application-specific read-only files.

4. Sensible virtual disk recognition

Approximate USB stick semantics, with each Pico recognized as a separate
USB stick, once formatted and keeping the same identity over reboot.

In practice, we use the Pico Board Id as the exFAT Volume Serial Number.

5. An example app running in SRAM, allowing inspecting the flash without changing it.

Allows a developer to boot a Pico to BOOTSEL mode, drop the binary to the BOOTSEL USB 
virtual disk, and get the Pico Flash contents visible as the `PicoVD` USB virtual disk.

### Generating directory entries on the fly

When the host reads a file, it uses the directory entry's `first_cluster` and file size `data_length`
to access only the clusters belonging to that partition, which we use to serve data directly from the flash.

1. **BootROM API → Partition list**  

   Invoke `get_partition_table_info()` in the RP2350 BootROM to obtain up to
   compose directory entries for the partitions.

2. **Virtual exFATt**  

   - **Read-only** volume, defined entirely in RAM at run-time.
   - **BPB (boot sector)**, **FAT tables** and **root directory** are produced on demand.
   - **Data clusters** map *directly* to 4 KiB flash pages.

3. **USB MSC integration**  
   - Implemented as TinyUSB MSC virtual disk callbacks.  
   - On `READ(10)` for LBAs in any metadata region, generate BPB/FAT/dir/... sectors on the fly.
   - On LBAs pointing to data, translate LBA → flash page → slice to e.g. 64 B chunks for MSC.

This design means only a small, at most 4 KiB cache buffer for flash pages, and dynamically generated metadata.

**Work in progress**: Shall we use XIP or bypass XIP?

### Minimal exFAT

See [`doc/ExFAT-design.md`](./doc/ExFAT-design.md) for further details.

## Background information

### RP2350 BootROM partition table

The RP2350 BootROM embeds a flexible "partition table" in on-flash metadata, see:

- **Datasheet § 5.9.4.1 "PARTITION_TABLE item"** describes the on-flash format: each entry holds a 16-byte name, a start and end page (4 KiB pages), and flags.
- **Datasheet § 5.9.1** limits the partition-table block to 640 bytes (enough for 16 entries).  
- **SDK spec § 4.5.5.5.22** and **BootROM API § 5.4.8.16** (`get_partition_table_info`) explain how to invoke the ROM routine and retrieve those entries directly into RAM.

## Testing

We provide a suite of pytest-based unit tests to verify the exFAT implementation over USB MSC.

**Prerequisites:**
- A Raspberry Pico (RP2350) running the `picovd.uf2` firmware.
- Python 3 and pytest installed (`pip3 install pytest`).
- On macOS: add your user to the `operator` group to access `/dev/rdiskN` (or run pytest with sudo).
- On Linux: identify your raw block device (e.g. `/dev/sdX`) and ensure read permissions.

**Running tests:**
```bash
python3 -m pytest tests/test_*
```
This will run tests for the boot sector, reserved sectors, VBR checksum, etc.

For a comprehensive testing guide, see `docs/Testing.md` (coming soon).

### Old information, to be ignored for now and to be removed

We call `get_partition_table_info()` with only the `PT_INFO_PT_INFO` and `PT_INFO_PARTITION_LOCATION_AND_FLAGS` flags. The ROM then returns a 36-word array:

1. `returned_flags` (which bits were honoured)  
2. `partition_count_and_present` (count + “present” bit)  
3. `unpartitioned_space` permissions & location  
4. same for `unpartitioned_space` flags  
5. up to 16 × (`permissions_and_location`, `permissions_and_flags`) pairs

We overlay that buffer with a simple C struct and extract each partition's `last_page` to drive FAT generation.
