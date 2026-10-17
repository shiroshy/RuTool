# Portable installer cryptography

Mbed TLS 3.6.7, statically linked with a minimal algorithm configuration:
AES-256-GCM, PBKDF2-HMAC-SHA256, SHA-256 and ECDSA P-256 verification.
`PortableCrypto` has no Qt or Windows API dependency. It preserves the RUT111
parameters and the existing canonical SPKI / IEEE P1363 signature format.
No TLS, persistent key storage, OS entropy adapters or CNG backends are enabled
in this library. The application's Qt HTTPS backend is separate.

Build this module alone on Linux or Windows with CMake and a C++20
compiler (no Qt installation required):

```sh
cmake -S crypto -B build-crypto -DCMAKE_BUILD_TYPE=Release
cmake --build build-crypto
```

Run from the `client-v2` directory. BUILD_TESTING defaults to OFF. Synthetic
.NET/CNG interoperability vectors and the test runner are retained only in the
full private workspace, under ignored tests/. There, add -DBUILD_TESTING=ON
when configuring and run ctest --test-dir build-crypto --output-on-failure.
The public source archive does not require or include these private test files.

Windows-specific installation, launching and DPAPI recovery journals are outside
this module and still need a platform implementation for a native Linux installer.

Mbed TLS upstream sources are unmodified in `third_party/mbedtls-3.6.7`.
RuTool selects Apache-2.0 from its dual license; the upstream license is included
in the source tree and in the packaged application's third-party licenses.
