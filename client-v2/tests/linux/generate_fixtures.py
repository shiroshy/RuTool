#!/usr/bin/env python3
"""Public test data only. The well-known EC private scalar 1 is NOT a production key.
Requires Python cryptography for regeneration; the C++ tests need no Python packages.
"""
import hashlib
import io
import json
from pathlib import Path
import struct
import zipfile
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

out = Path(__file__).with_name('fixtures')
out.mkdir(exist_ok=True)
original, patched = b'original database fixture\n', b'translated data fixture!!\n'
assert len(original) == len(patched)
hashhex = lambda b: hashlib.sha256(b).hexdigest()
watermark = hashhex(b'public watermark fixture')
manifest = dict(FormatVersion=1, OriginalSha256=hashhex(original), PatchedSha256=hashhex(patched), WatermarkSha256=watermark, ChangedRows=1)
key = ec.derive_private_key(1, ec.SECP256R1())
public = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
message = f'RuTool.Signature.v1\n2.0.8\n{hashhex(patched)}\n{watermark}\nlinux-fixture\n'.encode()
der = key.sign(message, ec.ECDSA(hashes.SHA256(), deterministic_signing=True))
r, s = utils.decode_dss_signature(der)
signature = r.to_bytes(32, 'big') + s.to_bytes(32, 'big')
import base64
sigjson = dict(FormatVersion=1, Algorithm='ECDSA-P256-SHA256', FileSha256=hashhex(patched), WatermarkSha256=watermark, RuToolVersion='2.0.8', SignerId='linux-fixture', SignatureBase64=base64.b64encode(signature).decode())
zipdata = io.BytesIO()
with zipfile.ZipFile(zipdata, 'w', compression=zipfile.ZIP_STORED) as z:
    for name, content in [('ExcelDB.db', patched), ('ExcelDB.db.baretool.json', json.dumps(manifest).encode()), ('signature.json', json.dumps(sigjson).encode())]:
        entry = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
        z.writestr(entry, content)
password, salt, nonce_prefix = b'public-test-password', bytes(range(16)), bytes(range(8))
aes = AESGCM(hashlib.pbkdf2_hmac('sha256', password, salt, 310000, 32))
zipbytes = zipdata.getvalue()
header = b'RUT111\x00\x00' + salt + nonce_prefix
# Exercise multiple chunks, independent little-endian nonce/AAD counters.
container = bytearray(header)
for index, offset in enumerate(range(0, len(zipbytes), 127)):
    plain = zipbytes[offset:offset+127]
    encrypted = aes.encrypt(nonce_prefix + struct.pack('<I', index), plain, b'RUT111\x00\x00' + struct.pack('<I', index))
    container += struct.pack('<I', len(plain)) + encrypted
for name, content in [('sample.rut', container), ('sample.zip', zipbytes), ('original.db', original), ('patched.db', patched), ('public.der', public), ('message.bin', message), ('signature.p1363', signature)]:
    (out/name).write_bytes(content)
print('Generated public, deterministic test fixtures')
