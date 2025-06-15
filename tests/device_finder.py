import os
import pytest
import plistlib
import subprocess
import sys

def find_msc_disk():
    if sys.platform == "darwin":
        """
        On macOS: run `diskutil list -plist external physical` and
        return the first `/dev/rdiskN` found, or raise IOError.
        """
        proc = subprocess.run(
            ["diskutil", "list", "-plist", "external", "physical"],
            check=True, stdout=subprocess.PIPE
        )
        doc = plistlib.loads(proc.stdout)
        whole = doc.get("WholeDisks", [])
        if not whole:
            raise IOError("No external physical disks found")
        disk_id = whole[0]
        device = f"/dev/rdisk{disk_id.lstrip('disk')}"

    elif sys.platform.startswith("linux"):
        """
        On Linux: run `lsblk -o NAME,TRAN` and return the first
        disk with TRAN=usb, or raise IOError.
        """
        proc = subprocess.run(
            ["lsblk", "-o", "NAME,TRAN"],
            check=True, stdout=subprocess.PIPE, text=True
        )
        lines = proc.stdout.strip().split('\n')[1:]
        for line in lines:
            parts = line.split()
            if len(parts) != 2:
                continue
            name, tran = parts
            if tran == "usb":
                device = f"/dev/{name}"
                break
        else:
            raise IOError("No USB disks found")
    else:
        raise NotImplementedError("This function only supports macOS and Linux")

    # Verify read permission once
    if not os.access(device, os.R_OK):
        pytest.fail(f"Device {device} is not readable: permission denied")
    return device

if __name__ == "__main__": 
    try:
        print(find_msc_disk())
    except Exception as e:
        sys.exit(str(e))
