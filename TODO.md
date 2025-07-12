# Major bugs

* (Currently none known)

# Known minor bugs

* Fix bitmap so that macOS fsck is happy.
* Check upcase table / upcase table test case. Test case fails.
* Go carefully through TinyUSB MSC callback layer. Currently hacky and maybe faulty for some SCSI commands.
  * in tud_msc_scsi_cb don't return error on default, maybe we should?
  * we handle some commands in tud_msc_scsi_pre_cb in some cases, but fail to
    handle them in tud_msc_scsi_cb if they default driver doesn't handle them.

# Optimizations to be done

* Cleanup TinyUSB MSC integration, once stabilised
* Reduce stack use in generating the VBR checksum
* Try to get to work the optimised, affine field mathematics dependent VBR checksum generation

# Features

* Add tested support and instructions for including into another project
* Add support for Pico SDK STDOUT.TXT and STDERR.TXT

* Move the PicoVD tool into examples/

# Toolchain

* Add github action for compiling the source
* Study if it is possible to run the code in an RP2350 emulator, such as WokWi
* Study if it is possible to emulate USB in such an emulator
* If emulator works, add github action for running the test suite

# Test cases to be added

* Verify allocation bitmap matches actual used clusters
* Verify cluster ranges for metadata don't overlap
