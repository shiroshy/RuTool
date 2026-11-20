# Linux audit (2.0.8)

The public source archive has no private tests, fixtures or benchmarks. Windows
CMake refers to those files; public Linux tests are maintained separately.

| Area | Original dependency | Linux implementation / decision |
|---|---|---|
| CMake | unconditional Windows gate, Win32 libraries, RC resources | platform source selection, native executable, Windows targets conditional |
| Bootstrap | LoadLibrary payload DLL, EXE launcher and updater | native Qt executable; application self-update disabled |
| FileOps | Toolhelp, CreateFile share modes, reparse checks, ReplaceFile | /proc, openat/O_NOFOLLOW, flock, renameat and fsync |
| LocalData | CryptProtectData, MachineGuid registry | Mbed TLS AES-GCM, private random user key, hashed machine-id + uid |
| Crypto | PortableCrypto / Mbed TLS | retained: PBKDF2-SHA256, AES-GCM, ECDSA P-256/P1363, SHA256 |
| Installation | shared transactional install, rollback journals, hashes | retained; Linux paths remain case-sensitive |
| Discovery | manual folder picker | Steam libraryfolders.vdf, manifests, Flatpak and Proton locations, manual picker retained |
| Settings/data | QStandardPaths + JSON/INI | XDG locations; no writes into AppImage |
| QML | Qt Quick / Controls / Dialogs, Windows font choices | same QML assets; system font fallback |
| Distribution | Windows deployment scripts | AppDir + linuxdeploy Qt plugin + appimagetool; separate portable Linux build required |

Security constraints: no signature/hash/authentication bypasses. Public signing
key is bundled; the private installer-keys.json is never copied by default.
Local Linux encrypted records are intentionally not DPAPI-compatible. Losing
local.key prevents decrypting saved codes/recovery journals: retain the private
data directory with backups. A key file protects other OS users, not hostile
processes running as the same user (the same general boundary as user DPAPI).

POSIX locks are advisory: they coordinate RuTool instances, but cannot prevent
an unrelated process from ignoring locks. Every path component used by platform
file operations is resolved without symlinks; discovered Steam aliases are
canonicalized before selection. Replacement is same-filesystem and fails on
EXDEV. Core uses Qt file I/O outside platform functions, so this is not a claim
of complete resistance to concurrent malicious directory mutations. Normal
installation requires a trusted game directory owned by the current user.

Implementation sequence: conditional build; Linux platform and discovery;
public regression tests and native/QML build; packaging and relocation checks.
Real game directories must never be used for destructive tests.

The archive also omits zlib's required zconf.h. Restored the standard header
from the locally installed zlib 1.3.2 development package (same version and
upstream license). No Nix paths or platform patches are present in that header.
Production installer-keys.json was not inspected or copied; Linux reads the
user-provided XDG config file instead. See BUILDING-LINUX.md for release gaps.

Verified locally: GCC 15.3.0, Qt 6.11.2, Release with warnings as errors.
CTest: 2/2 passed; platform QtTest: 11 passed, 0 failed, 0 skipped.
Includes an injected directory-fsync failure after rename, with rollback to
original content; production QML renders offscreen to linux-smoke.png.
Native ELF has no unresolved dependencies in the local SDK environment.
The installed executable was also smoke-tested with explicit local SDK
library paths; a plain cmake install does not bundle Qt by itself.
Shell scripts pass bash -n; Python sources pass py_compile. The AppImage
packaging guard rejects the local Nix-linked executable as intended.
Not verified: a completed portable AppImage, the container recipe end to end,
Windows compilation/runtime, real-display Wayland/X11, live API/authentication,
production package compatibility vectors, real game installation. Public test
fixtures validate the documented RUT111/P1363 format independently using
Python cryptography; they are not a substitute for production fixtures.

Fork transfer: the non-secret client-v2 files at upstream commit 060f097
match the supplied 2.0.8 ZIP byte-for-byte. The port is maintained on the
linux-port branch, with native implementation/tests, packaging tooling and
documentation in separate commits. Public test fixtures are explicitly
marked binary in .gitattributes to prevent checkout-time conversion.
The native Release build and both CTest suites were rerun successfully from
the Git checkout, rather than relying on the earlier remake build directory.
