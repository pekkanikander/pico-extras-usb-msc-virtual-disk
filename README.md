# PicoVD - USB "Stick" for software-managed Virtual Disk

**Work in progress**

PicoVD lets you mount your RP2350 project as a read-only USB disk over USB Mass-Storage (MSC),
making your Pico App to appear as a USB Memory Stick.
The "files" or directories are not stored anywhere, but generated on the fly,
requiring a minimal memory footprint.  The code itself takes only 5–10 kB.
Depending on the compile-time options, PicoVD may directly expose the Pico memory
contents as files, including RP2350 BootROM flash partitions, each as a separate file.

There is an API (being developed) for adding your own, application specific files.

PicoVD is designed to be used as a library within your own Pico app,
but it works also as a standalone app, running from the SRAM by default.

## Features

- Generates ExFAT files and file contents on the fly
- Supports adding your own files (work in progress)
- May expose RP2350 BootROM flash partitions, each as a read-only file (`.BIN`)
- Falls back to a single `FLASH.BIN` if no partition table is found
- May expose other memory regions as additional `.BIN` files
- Tiny memory footprint - about 5kB

### How it works

1. Implements **USB Memory Stick ** using TinyUSB MSC.

Implements a strictly read-only memory stick, allowing the Pico to be removed
or the Pico firmware to detach USB without the host OS complaining about the
memory stick having been disconnected or turned off without being ejected.

2. **Generates a Virtual exFAT disk**

Generates an exFAT disk image on the fly, requiring only a few kilobytes
of program memory and less than a kilobyte of SRAM.
Depending on compile time options, the SRAM usage can be scaled down to about one hundred bytes.

NB. At this point, PicoVD does not support subfolders. All files must be placed in the root directory.
However, nothing prevents one from extending the code to support subfolders.

Currently the virtual addresses (LBAs) of all files must be allocated at compile time.
In practice, this means that while your Pico app may decide whether any given file
is present and visible in the root directory or not, only
a fixed, compile-time defined set of files can be supported.

3. **Provides compile-time and run-time APIs for files**

Implements an API (WIP, not ready yet) that allows files to defined either at compile-time or at run-time.
However, even for the runtime-time defined files, their virtual address (LBA) must be defined at compile time.

4. **Exposes RP2350 memory regions as files**

Depending on compile time options, the RP2350 memories may be exposed as files.
This is most useful for development and debugging.
The following files are supported:
* `BOOTROM.BIN` — RP2350 bootrom code, on the die
* `SRAM.BIN` — Current contents of SRAM
* `FLASH.BIN` — Current contents of the whole flash

5. **Exposes RP2350 BootROM flash partitions**

RP2350 BootROM allows the flash to be partinoned into up to 8 partitions.
Typically, the partitions are named, reflecting their aimed used.

PicoVD allows these partitions to be exposed as individual files.

## Usage

Boot your board to BOOTTSEL. Drop the provided `picovd.uf2` binary to that USB Stick.

**Work in progress**: As of July 2025, you have to build the binary ourself, no releases yet.

Your Pico reboots and becomes visible as another USB Stick, `PicoVD`, allowing you to inspect the
the Pico memory files.  PicoVD runs from SRAM, not changing the contents of your flash.

### Using as a library in your own project

**Work in progress**: TBD.

## Overall approach

To provide the memory regions as virtual read-only files, the library does the following:
- **Presents a read-only ExFAT filesystem over USB Mass-Storage (MSC)**,
- **Exposes each memory region as a read-only exFAT file**,
- **Calls the BootROM APIs to read the partition table from the flash**
- **Requires minimal memory**, with all FAT, directory, and other metadata structures generated on the fly

The design is modular, allowing addition of other virtual read-only files or changing the way
the flash partitions are visible, if at all.

### Changing file contents on the fly

By default, most operating systems cache USB-MSC volume data and may not notice changes to a file until
their cache expires (often several seconds or more).
PicoVD’s virtual files (for example, `SRAM.BIN` or a dynamic `VERSION.txt`) can change each time they are read.
To ensure the host sees the latest data, you have two options:

1. **Bypass the host cache**

Open the file with caching disabled so every read goes straight to the device. For example, on macOS:
```c
   int fd = open("/Volumes/PICO_VD/VERSION.txt", O_RDONLY);
   fcntl(fd, F_NOCACHE, 1);  // disable buffer cache on this descriptor
   read(fd, buf, bufsize);
   close(fd);
```

In Linux, you can use the `nocache` command:
```bash
# If you have the 'nocache' utility installed (e.g. via your package manager):
   nocache cat /media/PICO_VD/VERSION.txt
```
or you can open the file with `O_DIRECT`:
```c
   int fd = open("/media/PICO_VD/VERSION.txt", O_RDONLY | O_DIRECT);
   read(fd, buf, bufsize);
   close(fd);
```
This guarantees each access fetches fresh content without waiting for the OS cache to expire.

For convenience, `tools/ncat.c` is a small non-caching utility for macOS, for testing.

2. **Force a full remount**

When you need the operating system to throw away all cached metadata and data,
for example, after changing partition tables or directory entries,
the device can simulate a very brief USB disconnect/reconnect.
This causes the host to unmount and immediately remount the drive, picking up every update at once.
Note that this will cause a short pause as the USB device re-enumerates.

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
read-only USB stick, once formatted and keeping the same identity over reboot.

In practice, we use the Pico Board Id as the exFAT Volume Serial Number.

5. An example app running in SRAM, allowing inspecting the flash without changing it.

Allows a developer to boot a Pico to BOOTSEL mode, drop the binary to the BOOTSEL USB
virtual disk, and get the Pico Flash contents visible as the `PicoVD` USB virtual disk.

### Generating directory entries on the fly

When the host reads a file, it uses the directory entry's `first_cluster` and file size `data_length`
to access only the clusters belonging to that "file", which we use to serve the data directly
from the memory or through an API callback (work in progress).

1. **Virtual exFAT**

   - **Read-only** volume, constructed as needed in RAM at run-time and thrown away when used.
   - **BPB (boot sector)**, **FAT tables**, **root directory** and other metadata are all produced on demand.
   - For RP2350 memory access, **data clusters** map *directly* to 4 KiB memory pages.

2. **USB MSC integration**
   - Implemented as TinyUSB MSC callbacks.
   - On `READ(10)` for LBAs in any metadata region, generate BPB/FAT/dir/... sectors on the fly.
   - On LBAs pointing to data, translate LBA → callback → flash page → slice to e.g. 64 B chunks for MSC.

3. **BootROM flash partition list**

   Invoke `get_partition_table_info()` in the RP2350 BootROM to obtain up to
   compose directory entries for the partitions.

This design means only a small, at most 512 byte cache buffer for sectors, and dynamically generated metadata.

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
