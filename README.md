# PicoVD - USB "Stick" for software-managed Virtual Disk

**Work in progress**

PicoVD lets you mount your RP2350 project as a read-only USB disk
over USB Mass-Storage (MSC), making your Pico App to appear as a USB Memory Stick.
The "files" or directories are not stored anywhere, but generated on the fly,
requiring a minimal memory footprint.
The code itself takes only 5–10 kB, depending on the compile-time options.
PicoVD may directly expose the Pico memory contents as files,
including the RP2350 BootROM flash partitions, if any, each as a separate file.

There is an API (being developed) for adding application specific files.

PicoVD is designed to be used as a library within your own Pico app,
but it works also as a standalone app, running from the SRAM by default.

## Features

- Generates ExFAT files and file contents on the fly
- Supports adding your own files (work in progress)
- May expose RP2350 BootROM flash partitions, each as a read-only file (`.BIN`)
- May also expose the whole flash as a single `FLASH.BIN` file
- May expose other memory regions (ROM, SRAM) as additional `.BIN` files
- Supports `STDOUT.TXT` for debugging
- Tiny memory footprint

### How it works

1. Implements **USB Memory Stick ** using TinyUSB MSC.

Implements a strictly read-only memory stick, allowing the Pico to be removed
or the Pico firmware to detach USB without the host OS complaining (much) about the
memory stick having been disconnected or turned off without being ejected.

The exFAT layer is implemented as TinyUSB MSC callbacks and largely independent from RP2350 code.
Adding support for other MCUs and SDKs should be relatively straigtforward.

2. **Generates a Virtual exFAT disk**

Generates an exFAT disk image on the fly, requiring only a few kilobytes
of program memory and less than a kilobyte of SRAM.
Depending on compile time options,
the sram usage can be scaled down to about two hundred bytes
(other than stack),
if you leave out all the included file examples and just implement
your own file.

NB. At this point, PicoVD does not support subfolders.
All files must be placed in the root directory.
However, nothing prevents one from extending the code to support subfolders.

3. **Provides compile-time and run-time APIs for files**

Implements an API (WIP, not ready yet) that allow files to defined
either at compile-time or at run-time.

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

6. **Exposes `stdout` or `printf` output as files**

When using the Pico SDK `stdout`, by default PicoVD
allows the print out to be stored into a ring buffer,
whose contents are made available as two different files:

* `STDOUT.TXT` — Complete log file showing output from the beginning.
  If the ring buffer becomes full, the discarded contents are replaced with NULs.
* `STDOUT-TAIL.TXT` — Tail file showing only unread output, for `tail -F`

The contents of these files change dynamically.
The files implementation periodically updates the host OS about this,
by emulating a media change, i.e. disk ejection and reinsertion.

## Usage

Boot your board to BOOTTSEL.
Drop the provided `picovd-tool.uf2` binary to the USB Stick.

Your Pico reboots and becomes visible as another USB Stick, `PicoVD`,
allowing you to inspect the the Pico memory files, stdout, etc.
By default, PicoVD runs from SRAM, not changing the contents of your flash.

## Adding files and changing file contents on the fly

By default, most operating systems cache USB MSC volume data
and may not notice changes to a file until
their cache expires (often several seconds or more).
PicoVD’s virtual files (for example, `SRAM.BIN` or a dynamic `CHANGING.TXT`)
can change each time they are read.

### Bypassing the host cache

Open the file with caching disabled so every read goes straight to the device.
For example, on macOS:
```c
   int fd = open("/Volumes/PICO_VD/CHANGING.TXT", O_RDONLY);
   fcntl(fd, F_NOCACHE, 1);  // disable buffer cache on this descriptor
   read(fd, buf, bufsize);
   close(fd);
```

In Linux, you can use the `nocache` command:
```bash
   # If you have the 'nocache' utility installed (e.g. via your package manager):
   nocache cat /media/PICO_VD/CHANGING.TXT
```
or you can open the file with `O_DIRECT`:
```c
   int fd = open("/media/PICO_VD/CHANGING.TXT", O_RDONLY | O_DIRECT);
   read(fd, buf, bufsize);
   close(fd);
```
This guarantees each access fetches fresh content.

For convenience, `tools/ncat.c` is a small non-caching utility for macOS.

### Forcing a remount by simulating media change

When you need the operating system to throw away all cached metadata and data,
for example, after changing root directory entries,
the device can either simulate a media change or a very brief USB disconnect/reconnect.
(Technically, the so-called media change is an SCSI Unit Attention (UA) 0x28 notification,
which most operating systems interpret as a media change.)
This causes the host to unmount and immediately remount the drive,
picking up every update at once.

Note that this will also cause a short break, as the USB device re-enumerates.
During that break, some system calls accessing the file system will return errors.

### Using stdout files

The stdout files provide real-time access to your Pico's `printf` output:

**Reading the complete log:**
```bash
cat /Volumes/PICO_VD/STDOUT.TXT
```
If the log has grown larger than what fits into the ring buffer,
the initial contents is read as NULs, which by default are invisible
at your terminal.
However, if you copy the output to a file, you may notice that the
beginning of the file contains NULs.

**Monitoring new output (tail -F style):**
```bash
tail -F /Volumes/PICO_VD/STDOUT-TAIL.TXT 2> /dev/null
```

By default, `tail -F` re-opens the file whenever it "disappears"
and starts reading from the beginning.
The `STDOUT-TAIL.TXT` implementation keeps track how much of the
file has been read and exposes the next printouts.
This gives pretty good semantics for `tail -F` so that you can
use it for continuously printing out `stdout`.
However, the data is provided in chunks of 64 bytes (the MSC endpoint buffer size),
due to limitations of the MSC SCSI layer and due to
[a bug in TinyUSB](https://github.com/pekkanikander/pico-tinyusb-msc-panic/tree/main).

## Using as a library in your own project

**Work in progress**

### Adding your own files

You can add your own virtual files to the PicoVD virtual disk,
either as static (compile-time) or dynamic (runtime) files.

#### Dynamic (runtime) files

Dynamic files are created and registered at runtime.
Their contents (and optionally size) can change while the device is running.
This is useful for exposing sensor data, logs,
or any information that may change or be generated on demand.

To define a dynamic file, use the `PICOVD_DEFINE_FILE_RUNTIME` macro and register it with `vd_add_file()`:

```c
// Define a callback to provide file content
void my_file_content_cb(uint32_t offset, void* buf, uint32_t bufsize) {
    // Fill buf with up to bufsize bytes starting at offset of the file
    // For example, generate a string or copy from a buffer
}

// Define the file (e.g., "DYNAMIC.TXT" with initial size 128 bytes)
PICOVD_DEFINE_FILE_RUNTIME(my_dynamic_file, "DYNAMIC.TXT", 128, my_file_content_cb);

// Register the file at runtime (e.g., during initialization), with maximum size of 4kB.
vd_add_file(&my_dynamic_file, (4 * 1024));
```
- The `vd_dynamic_file_t` struct must remain valid while the file is registered.
- To update the file size later, use `vd_update_file()`.

#### Static (compile-time) files

Static files are defined at compile time and their contents
do not change while the device is running.
This is useful for exposing firmware images, documentation,
or any data that is fixed in the firmware binary.

To define a static file, use the `PICOVD_DEFINE_FILE_STATIC` macro:

```c
// Define a static file, "README.TXT", starting at cluster 100 with size 256 bytes)
PICOVD_DEFINE_FILE_STATIC(my_static_file, "README.TXT", 100, 256);
```
- The file is always read-only. Other file attributes are currently not supported.
- The file name must be a string literal (e.g., "README.TXT"),
  or a macro expanding into a string literal.
- The macro ensures the file name is encoded as UTF-16LE for exFAT.
- All static files are auto-collected at link time and
  automatically provided into the virtual disk root directory.
- Currently there is no clear API to provide the file contents.
  You have to implement your own sector reader for reading the
  sectors that serve your indicated cluster(s).
  Adding an API for this is work in progress.

## Design choices

Our goals for PicoVD were simplicity, compact memory footprint,
and forward compatibility.
As far as we know, the specific approach chosen is original.

For minimality and simplicity, only read-only access is supported.
The design allows for some writing support in the future,
but that is for future study.

### Design goals

1. A lightweight library to be included into any project that supports USB.

Allows a developer (or user) to read the flash contents.
Allows USB MSC access *in parallel* with the Pico SDK USB stdio
and Pico USB Reset support.
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
Flexibility in generating virtual files, allowing the design to be used
for application-specific read-only files.

4. Sensible virtual disk recognition

Approximate USB stick semantics, with each Pico recognized as a separate
read-only USB stick, once formatted and keeping the same identity over reboots.

In practice, we use the Pico Board Id as the exFAT Volume Serial Number.

5. An example app running in SRAM, allowing inspecting the flash without changing it.

Allows a developer to boot a Pico to BOOTSEL mode,
drop the binary to the BOOTSEL USB virtual disk,
and get the Pico Flash contents visible as the `PicoVD` USB virtual disk.

### Generating directory entries on the fly

When the host reads a file,
it uses the directory entry's `first_cluster` and file size `data_length`
to access only the clusters belonging to that "file".
We map each file into a linear sequence of LBAs.

When reading a file, our code selects the handler to serve the file contents
based on the LBA.
Currently these linear LBA regions have to be defined at compile time,
but this restriction may be lifted later, as a compile-time option.

1. **Virtual exFAT**

   - **Read-only** volume, constructed as needed in RAM at run-time.
   - **BPB (boot sector)**, **FAT tables**, **root directory** and
     other metadata are all produced on demand, not stored on flash.
   - For RP2350 memory access, **data clusters** map *directly* to 4 KiB memory pages.
   - On LBAs pointing to RP2350 memory regions,
     translate LBA → callback → memory page → slice to e.g. 64 B chunks for MSC.

2. **BootROM flash partition list**

   Invoke `get_partition_table_info()` in the RP2350 BootROM to obtain up to
   compose directory entries for the partitions.

This design means only a small,
at most 512 byte cache buffer for sectors,
and dynamically generated metadata.

### Minimal exFAT

See [`doc/ExFAT-design.md`](./doc/ExFAT-design.md) for further details.

## Testing

We provide a small suite of pytest-based unit tests to verify
the exFAT implementation over USB MSC.
These tests are useful to isolate crashes in the handlers.

For further comformance testing, using the Linux ExFAT test suite would be good.
Currently we use mainly the macOS `fsck.exfat`.

**Prerequisites:**

- A Raspberry Pico (RP2350) running the `picovd.uf2` firmware.
- Python 3 and pytest installed (`pip3 install pytest`).
- On macOS: add your user to the `operator` group to access `/dev/rdiskN`
  (or run pytest with sudo).
- On Linux: identify your raw block device (e.g. `/dev/sdX`)
  and ensure read permissions.

**Running tests:**

```bash
python3 -m pytest tests/test_*
```
This will run tests for the boot sector, reserved sectors, VBR checksum, etc.

## Background information

### RP2350 BootROM partition table

The RP2350 BootROM embeds a flexible "partition table" in on-flash metadata, see:

- **Datasheet § 5.9.4.1 "PARTITION_TABLE item"** describes the on-flash format:
  each entry holds a 16-byte name, a start and end page (4 KiB pages), and flags.
- **Datasheet § 5.9.1** limits the partition-table block to 640 bytes.
- **SDK spec § 4.5.5.5.22** and **BootROM API § 5.4.8.16**
  (`get_partition_table_info`) explain how to invoke the ROM routine and
  retrieve those entries directly into RAM.
