# Portable installer cryptography

The library uses Mbed TLS 3.6.7 for AES-256-GCM, PBKDF2-HMAC-SHA256,
SHA-256 and ECDSA P-256 verification. It preserves the RUT111 parameters,
canonical SPKI public keys and IEEE P1363 signatures.

It has no Qt or Windows API dependency. TLS, local key storage,
installation and recovery journals are handled by the application.

## Build

Run from the application directory with CMake and a C++20 compiler:

```sh
cmake -S crypto -B build-crypto -DCMAKE_BUILD_TYPE=Release
cmake --build build-crypto
```

Standalone tests default to OFF. The public application tests cover the
crypto adapter and package handling; see the [Linux build guide](../BUILDING-LINUX.md).

## License

Mbed TLS sources are unmodified. RuTool uses the Apache-2.0 option of its
dual license. The upstream license is included in the source distribution
and in the packaged application's third-party notices.
