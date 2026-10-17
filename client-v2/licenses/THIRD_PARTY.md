# Third-party components

Original installer code: Apache-2.0; see ../LICENSE and ../NOTICE.

Qt 6.8.3 is dynamically linked. Qt source packages are available from
https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ and official
https://github.com/qt repositories (tag v6.8.3). Included Qt license texts
retain their original terms; component metadata is available in the SDK SBOM.
The application can be rebuilt with modified Qt libraries.

Mbed TLS 3.6.7 is statically linked under its Apache-2.0 option. Original
copyright/SPDX notices are retained in third_party/mbedtls-3.6.7/include and
library; the upstream dual-license text is in mbedtls/LICENSE. This distribution
uses Apache-2.0. Only the required source directories are exported; upstream
code is unmodified. Upstream release archive SHA-256:
a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6.

zlib 1.3.2 and nlohmann/json 3.12.0 sources retain their original licenses.
See zlib.txt and nlohmann-json.txt. GCC/MinGW/winpthreads runtime notices are
included in their respective subdirectories.

Blue Archive artwork, characters, names and related material belong to their
respective rights holders and are not licensed by this project's Apache-2.0
grant. This is an unofficial fan project.
