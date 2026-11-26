#!/usr/bin/env python3
"""Sign Linux channel metadata with an external P-256 key using OpenSSL.

The private key is read by OpenSSL. Only the public SPKI and signature leave it.
Existing output files are never overwritten.
"""
import argparse
import base64
import json
from pathlib import Path
import re
import subprocess


def version(text):
    if not isinstance(text, str) or not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", text):
        raise ValueError("Expected a three-part version")
    parts = tuple(map(int, text.split('.')))
    if max(parts) > 2147483647:
        raise ValueError("Version component too large")
    return parts


def validate(obj):
    if (obj.get('FormatVersion') != 1 or obj.get('Platform') != 'linux-x86_64'
            or type(obj.get('Sequence')) is not int or not 0 < obj['Sequence'] <= 9007199254740991
            or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9_-][A-Za-z0-9_.-]{0,99}', obj.get('Repository', ''))):
        raise ValueError('Invalid channel header')
    routes = obj.get('Routes')
    if not isinstance(routes, list) or len(routes) > 64:
        raise ValueError('Invalid routes')
    intervals = []
    for route in routes:
        start, end, target = (version(route.get(field)) for field in ('FromVersion', 'BeforeVersion', 'Version'))
        if (start >= end or target < end or type(route.get('Bytes')) is not int or not 64 <= route['Bytes'] <= 2 * 1024**3
                or not re.fullmatch(r'[0-9a-f]{64}', route.get('Sha256', ''))
                or any(not re.fullmatch(r'[A-Za-z0-9_-][A-Za-z0-9_.-]{0,127}', route.get(field, '')) for field in ('Tag', 'Asset'))
                or not route['Asset'].endswith('.AppImage')
                or any(start < other_end and other_start < end for other_start, other_end in intervals)):
            raise ValueError('Invalid, overlapping or descending route')
        intervals.append((start, end))


def p1363(signature):
    # OpenSSL returns a short DER SEQUENCE of two positive INTEGERs for P-256.
    if len(signature) < 8 or signature[0] != 0x30 or signature[1] != len(signature) - 2:
        raise ValueError('Invalid ECDSA signature')
    offset, values = 2, []
    for _ in range(2):
        if offset + 2 > len(signature) or signature[offset] != 2:
            raise ValueError('Invalid ECDSA integer')
        size = signature[offset + 1]
        value = signature[offset + 2:offset + 2 + size]
        if not value or len(value) != size or value[0] & 0x80:
            raise ValueError('Invalid ECDSA integer')
        number = int.from_bytes(value, 'big')
        if not 0 < number < 2**256:
            raise ValueError('Not a P-256 signature')
        values.append(number.to_bytes(32, 'big'))
        offset += size + 2
    if offset != len(signature):
        raise ValueError('Trailing signature bytes')
    return b''.join(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('metadata', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--key', type=Path, required=True, help='Private P-256 key outside the repository')
    parser.add_argument('--public-key', type=Path, help='Also save the public SPKI DER for the client build')
    parser.add_argument('--openssl', default='openssl')
    args = parser.parse_args()
    if args.output.exists() or (args.public_key and args.public_key.exists()):
        parser.error('Output already exists')
    if args.metadata.stat().st_size > 512 * 1024:
        parser.error('Metadata exceeds 512 KiB')
    obj = json.loads(args.metadata.read_text(encoding='utf-8'))
    validate(obj)
    payload = json.dumps(obj, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    public = subprocess.check_output([args.openssl, 'pkey', '-in', str(args.key), '-pubout', '-outform', 'DER'])
    if len(public) != 91 or not public.startswith(bytes.fromhex('3059301306072a8648ce3d020106082a8648ce3d030107034200')):
        parser.error('Expected a P-256 key')
    signature = subprocess.check_output([args.openssl, 'dgst', '-sha256', '-sign', str(args.key)], input=payload)
    envelope = json.dumps(dict(PayloadBase64=base64.b64encode(payload).decode('ascii'),
                               SignatureBase64=base64.b64encode(p1363(signature)).decode('ascii')), separators=(',', ':')).encode('utf-8') + b'\n'
    with args.output.open('xb') as output:
        output.write(envelope)
    if args.public_key:
        with args.public_key.open('xb') as output:
            output.write(public)


if __name__ == '__main__':
    main()
