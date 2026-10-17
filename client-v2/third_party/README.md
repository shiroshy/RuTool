zlib 1.3.2: upstream https://github.com/madler/zlib/releases/tag/v1.3.2
Source archive: https://github.com/madler/zlib/archive/refs/tags/v1.3.2.zip
SHA-256: 31fd9fee98812abcf147d0e103bc4d2f983c35a8d7a807a328a299f3a74e0050
Original sources and LICENSE are retained. Only raw inflate/checksum routines
are compiled into the Windows client; no gzip file helpers or ZIP path handling.
The RuTool package reader performs its own strict central/local ZIP validation.

Mbed TLS 3.6.7 (3.6 LTS):
https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7
Official source archive:
https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
SHA-256, verified against the upstream release checksum file:
a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
Upstream sources are retained unmodified. RuTool selects the Apache-2.0 license
from the upstream Apache-2.0 OR GPL-2.0-or-later dual license. Full license text
is retained in the source and packaged in app/licenses/mbedtls.
Only the crypto modules listed in crypto/CMakeLists.txt are compiled, using the
minimal configuration crypto/rutool_mbedtls_config.h. No TLS, entropy provider,
persisted key storage or Windows CNG adapter is enabled in the linked library.

nlohmann/json 3.12.0: https://github.com/nlohmann/json/releases/tag/v3.12.0
Single header SHA-256 (matches upstream release):
aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
Used by the standalone Windows bootstrap before loading Qt or the client DLL.
MIT license retained alongside json.hpp.
