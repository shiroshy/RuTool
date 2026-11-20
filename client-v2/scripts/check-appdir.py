#!/usr/bin/env python3
"""Check deployment contents and ELF metadata without trusting host Qt search paths."""
import os
import hashlib
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
errors = []
required = ['AppRun', 'rutool.desktop', 'rutool.svg', 'usr/bin/RuTool',
            'usr/bin/app/config/installer-keys.json',
            'usr/bin/app/config/update-public.der', 'usr/plugins/platforms/libqxcb.so',
            'usr/plugins/platforms/libqoffscreen.so', 'usr/qml/QtQuick/qmldir',
            'usr/qml/QtQuick/Controls/qmldir', 'usr/qml/QtQuick/Dialogs/qmldir',
            'usr/qml/QtQuick/Shapes/qmldir', 'usr/qml/QtQuick/Templates/qmldir',
            'usr/qml/QtQuick/Window/qmldir',
            'usr/qml/QtQuick/Layouts/qmldir', 'usr/qml/QtQml/WorkerScript/qmldir']
for name in required:
    if not (root/name).is_file():
        errors.append(f'Missing {name}')
for group in ['tls', 'imageformats', 'xcbglintegrations', 'wayland-shell-integration', 'wayland-decoration-client']:
    if not list((root/'usr/plugins'/group).glob('*.so')):
        errors.append(f'Missing plugin group {group}')
if not list((root/'usr/plugins/platforms').glob('libqwayland*.so')):
    errors.append('No Wayland platform plugin')
if not (root/'usr/plugins/tls/libqopensslbackend.so').is_file():
    errors.append('OpenSSL TLS backend missing')
if not (root/'usr/plugins/imageformats/libqgif.so').is_file():
    errors.append('GIF plugin required for Arona.gif')
for library in ['libssl.so.3', 'libcrypto.so.3', 'libstdc++.so.6', 'libOpenGL.so.0']:
    if not (root/'usr/lib'/library).exists():
        errors.append(f'Missing {library}')
for path in root.rglob('*'):
    relative = path.relative_to(root)
    if any(part.lower() in {'tests', 'test', 'fixtures', '__pycache__', 'cmakefiles', 'qmltooling', 'include'} for part in relative.parts):
        errors.append(f'Development/test directory: {relative}')
    if path.is_symlink():
        if not path.resolve().is_relative_to(root):
            errors.append(f'External symlink: {path.relative_to(root)}')
        continue
    if not path.is_file():
        continue
    if path.name == 'installer-keys.json':
        original = Path(__file__).resolve().parents[1]/'config/installer-keys.json'
        if str(relative) != 'usr/bin/app/config/installer-keys.json' or hashlib.sha256(path.read_bytes()).digest() != hashlib.sha256(original.read_bytes()).digest():
            errors.append(f'Unexpected installer configuration: {relative}')
    if path.suffix.lower() in {'.pem', '.key', '.pfx', '.p12'}:
        errors.append(f'Potential secret in AppDir: {path.relative_to(root)}')
    if (path.suffix.lower() in {'.rut', '.zip', '.py', '.pyc', '.o', '.a', '.cmake', '.cpp', '.hpp', '.h', '.debug'}
            or path.name in {'CMakeCache.txt', 'build.ninja', 'compile_commands.json'}
            or any(word in path.name.lower() for word in ['qttest', 'rutool_linux_tests', 'ctest', 'generate_fixtures', 'diagnostics', 'benchmarks'])):
        errors.append(f'Development/test file: {relative}')
    with path.open('rb') as f:
        header = f.read(20)
    if not header.startswith(b'\x7fELF'):
        continue
    if header[4] != 2 or header[18:20] != b'\x3e\x00':
        errors.append(f'ELF is not x86_64: {path.relative_to(root)}')
    metadata = subprocess.check_output(['readelf', '-dW', str(path)], text=True)
    interpreter = subprocess.check_output(['readelf', '-lW', str(path)], text=True)
    if '/nix/store/' in metadata + interpreter:
        errors.append(f'Nix store dependency: {path.relative_to(root)}')
    sections = subprocess.check_output(['readelf', '-SW', str(path)], text=True)
    if '.debug_info' in sections or '.zdebug_info' in sections:
        errors.append(f'Debug information in release ELF: {path.relative_to(root)}')
    env = dict(os.environ, LD_LIBRARY_PATH=str(root/'usr/lib'))
    dependencies = subprocess.run(['ldd', str(path)], text=True, capture_output=True, env=env)
    if 'not found' in dependencies.stdout + dependencies.stderr:
        errors.append(f'Unresolved library: {path.relative_to(root)}\n{dependencies.stdout}{dependencies.stderr}')
    # Qt, C++ runtime and TLS must come from AppDir, not an installed host SDK.
    for line in dependencies.stdout.splitlines():
        if any(name in line for name in ['libQt6', 'libssl.so', 'libcrypto.so', 'libstdc++.so']):
            if '=>' in line and str(root) not in line:
                errors.append(f'Host dependency: {path.relative_to(root)}: {line.strip()}')
if errors:
    sys.exit('\n'.join(errors))
print('AppDir: x86_64 ELF dependencies, Qt/QML plugins, TLS, original public configuration; no tests/build files')
