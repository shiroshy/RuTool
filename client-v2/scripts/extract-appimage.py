#!/usr/bin/env python3
"""Validate Type 2/ELF headers and extract without FUSE or host binfmt handlers."""
from pathlib import Path
import struct
import subprocess
import sys

image = Path(sys.argv[1]).resolve()
destination = Path(sys.argv[2]).resolve()
if destination.exists():
    sys.exit(f'Refusing to overwrite {destination}')
with image.open('rb') as stream:
    header = stream.read(64)
    if (header[:4] != b'\x7fELF' or header[4:6] != b'\x02\x01'
            or header[8:11] != b'AI\x02' or header[18:20] != b'\x3e\x00'):
        sys.exit('Expected an x86_64 little-endian AppImage Type 2')
    section_offset = struct.unpack_from('<Q', header, 40)[0]
    section_size, section_count = struct.unpack_from('<HH', header, 58)
    if section_size != 64 or section_count == 0:
        sys.exit('Unsupported ELF section table')
    offset = section_offset + section_size * section_count
    stream.seek(offset)
    if stream.read(4) != b'hsqs':
        sys.exit('No SquashFS at end of ELF section table')
subprocess.run(['unsquashfs', '-no-progress', '-d', str(destination), '-o', str(offset), str(image)], check=True)
print(f'AppImage: x86_64, Type 2, SquashFS offset {offset}; extracted {image.name}')
