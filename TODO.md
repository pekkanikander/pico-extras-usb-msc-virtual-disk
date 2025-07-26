# Major bugs

Currently no known major bugs

# Known minor bugs

* Add dynamic name allocation for long partition names. Now they fail with printf.

* Fix bitmap so that macOS fsck is happy.
* Remove stray invalid file names so that macOS fsck is happy.
* Check upcase table / upcase table test case. Test case fails.

# Optimizations to be done

* Refactor vd_virtual_disk.c into 2-3 files
* Check if there are layering violations and fix them, if easy
  * Check UA 0x28 delays, now in many layers, replace with an API?

* Cleanup TinyUSB MSC integration, once stabilised

* Reduce stack use in generating the VBR checksum
* Try to get to work the optimised, affine field mathematics dependent VBR checksum generation

# Features

* Add static file handler API
  * Rewrite / optimise LBA handling table for the static file handler APi

* Use compile time as base also for dynamic file time stamps, if no RTC
* Add tested support and instructions for including into another project

* Move the PicoVD tool into examples/

# Toolchain

* Study if it is possible to run the code in an RP2350 emulator, such as WokWi
* Study if it is possible to emulate USB in such an emulator
* If emulator works, add github action for running the test suite

# Test cases to be added

* Verify allocation bitmap matches actual used clusters
* Verify cluster ranges for metadata don't overlap

# HIL testing ideas

- **TinyHCI (tinygo-org/tinyhci):** HIL integration via GitHub events running
tests locally on actual RP2040 hardware
- listens to GitHub and executes tests with a local runner:
https://github.com/tinygo-org/tinyhci

- **TinyTapeout/tt-micropython-firmware:** Uses Cocotb framework to run the same
test harness on both Wokwi simulator and physical RP2040 hardware:
https://github.com/TinyTapeout/tt-micropython-firmware

- **Wokwi RP2040 JS Emulator:** Browser-based simulator for RP2040 firmware;
useful for early smoke tests but requires backporting BootROM partition support for RP2350:
https://github.com/wokwi/rp2040js

- **rp2040-pio-emulator:** Pure-software PIO peripheral emulator for unit-testing
PIO programs without hardware:
https://github.com/NathanY3G/rp2040-pio-emulator
