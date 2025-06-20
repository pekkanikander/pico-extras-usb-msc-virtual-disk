# Major bugs

* (Currently none known)

# Known minor bugs

* Fix bitmap so that macOS fsck is happy.
* Check upcase table / upcase table test case. Test case fails.

# Optimizations to be done

* Reduce stack use in generating the VBR checksum
* Try to get to work the optimised, affine field mathematics dependent VBR checksum generation

# Features

* Add support for SRAM and ROM files
* Add support for runtime generated file directory entries
* Add support for Pico BootROM partitions

* Move the PicoVD tool into examples/

# Toolchain

* Add github action for compiling the source
* Study if it is possible to run the code in an RP2350 emulator, such as WokWi
* Study if it is possible to emulate USB in such an emulator
* If emulator works, add github action for running the test suite

# Test cases to be added

* Verify allocation bitmap matches actual used clusters
* Verify cluster ranges for metadata don’t overlap



