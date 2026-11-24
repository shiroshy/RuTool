"""Build a RuTool copy/literal delta from an original file and its localized copy."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


MAGIC = b"RUTD1\0"
BLOCK = 64
MAX_BYTES = 64 * 1024 * 1024


def read_input(path):
    if not 0 < path.stat().st_size <= MAX_BYTES:
        raise ValueError(f"Input beyond supported size: {path}")
    return path.read_bytes()


def records(source, target):
    offsets = {}
    for start in range(0, len(source) - BLOCK + 1, BLOCK):
        offsets.setdefault(source[start:start + BLOCK], start)
    position = literal = 0
    while position + BLOCK <= len(target):
        original = offsets.get(target[position:position + BLOCK])
        if original is None:
            position += 1
            continue
        if literal < position:
            payload = target[literal:position]
            yield struct.pack("<I", 0x80000000 | len(payload)) + payload
        length = BLOCK
        limit = min(len(source) - original, len(target) - position)
        for chunk in (65536, 4096, BLOCK, 1):
            while length + chunk <= limit and source[original + length:original + length + chunk] == target[position + length:position + length + chunk]:
                length += chunk
        yield struct.pack("<II", length, original)
        position += length
        literal = position
    if literal < len(target):
        payload = target[literal:]
        yield struct.pack("<I", 0x80000000 | len(payload)) + payload


def digest(value):
    return hashlib.sha256(value).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("localized", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    original = read_input(args.original)
    localized = read_input(args.localized)
    if len(original) != len(localized) or original == localized:
        raise ValueError("The localized bundle must differ and preserve the original length")
    delta = b"".join([MAGIC, *records(original, localized)])
    if len(delta) >= len(localized):
        raise ValueError("Delta must be smaller than the localized bundle")
    with args.output.open("xb") as output:
        output.write(delta)
    print(json.dumps({
        "packageBytes": len(delta), "packageSha256": digest(delta),
        "originalSha256": digest(original), "targetSha256": digest(localized),
        "targetBytes": len(localized)
    }))


if __name__ == "__main__":
    main()
