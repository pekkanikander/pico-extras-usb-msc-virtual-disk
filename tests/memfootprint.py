import os
import subprocess

OBJ_DIR = "build/CMakeFiles/picovd-tool.dir/src"

def get_size(obj_file):
    # Try arm-none-eabi-size first, fall back to size if not found
    try:
        result = subprocess.run(
            ["arm-none-eabi-size", obj_file],
            capture_output=True, text=True, check=True
        )
    except FileNotFoundError:
        result = subprocess.run(
            ["size", obj_file],
            capture_output=True, text=True, check=True
        )
    lines = result.stdout.strip().splitlines()
    if len(lines) < 2:
        return None
    # Parse: text data bss dec hex filename
    parts = lines[1].split()
    text, data, bss = map(int, parts[:3])
    flash = text + data  # flash: code + const data
    sram = data + bss    # sram: data + bss
    return text, data, bss, flash, sram

def main():
    print(f"{'File':40} {'FLASH':>8} {'DATA':>8} {'BSS':>8} {'SRAM':>8}")
    print("-" * 80)
    total_flash = total_data = total_bss = total_sram = 0
    for fname in sorted(os.listdir(OBJ_DIR)):
        if fname.endswith(".o"):
            path = os.path.join(OBJ_DIR, fname)
            sizes = get_size(path)
            if sizes:
                text, data, bss, flash, sram = sizes
                print(f"{fname:40} {flash:8} {data:8} {bss:8} {sram:8}")
                total_flash += flash
                total_data += data
                total_bss += bss
                total_sram += sram
    print("-" * 80)
    print(f"{'TOTAL':40} {total_flash:8} {total_data:8} {total_bss:8} {total_sram:8}")

if __name__ == "__main__":
    main()
