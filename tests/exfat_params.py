"""
tests/exfat_params.py

Lightweight parser for extracting exFAT parameters from the C header.
Currently a placeholder for future use; exposes selected macros as Python constants.
"""

import os
import re

# XXX TODO: Should bring in CFG_TUD_MSC_BUFSIZE or rethink.

# === Editable list of exFAT macro names to extract ===
# These C macro names will be bound directly as Python variables.
_PARAMS = [
    'VIRTUAL_DISK_SIZE',
    'MSC_BLOCK_SIZE',
    'MSC_TOTAL_BLOCKS',
    'EXFAT_BYTES_PER_SECTOR_SHIFT',
    'EXFAT_SECTORS_PER_CLUSTER_SHIFT',
    'EXFAT_FAT_REGION_START_LBA',
    'EXFAT_FAT_REGION_LENGTH',
    'EXFAT_CLUSTER_HEAP_START_LBA',
    'EXFAT_CLUSTER_COUNT',
    'EXFAT_ROOT_DIR_FIRST_CLUSTER',
    'EXFAT_VOLUME_LENGTH',
]

# Adjust the path to point to your header file location
_HEADER_PATH = os.path.join(
    os.path.dirname(__file__),
    os.pardir,  # tests/..
    'src',
    'vd_exfat_params.h'
)

# Read the header and capture macro definitions, handling C preprocessor continuations
_macros = {}
_pattern = re.compile(r'^\s*#define\s+([A-Za-z0-9_]+)\s+(.+?)(?:\s*//.*)?$')

# Read the entire header file
with open(_HEADER_PATH, 'r') as f:
    raw = f.read()

# Join lines ending with backslash (line continuations) into one logical line
raw = raw.replace('\\\n', ' ')

# Process each logical line
for line in raw.splitlines():
    match = _pattern.match(line)
    if not match:
        continue
    name, value = match.groups()
    value = value.strip()
    _macros[name] = value

# Parse and assign constants from macros
for macro_name in _PARAMS:
    globals()[macro_name] = _macros.get(macro_name)

# XXX: TODO: Evaluate the values into Python constants

if __name__ == '__main__':
    for macro_name in _PARAMS:
        print(f"{macro_name}={globals().get(macro_name)}")
