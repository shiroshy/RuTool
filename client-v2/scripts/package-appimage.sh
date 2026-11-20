#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(realpath -- "${1:-$source_dir/build-linux}")
output_dir=${2:-"$source_dir/dist-linux"}
: "${RUTOOL_TOOLS_DIR:?Set RUTOOL_TOOLS_DIR to a directory containing pinned deployment tools and SHA256SUMS}"
tools_dir=$(realpath -- "$RUTOOL_TOOLS_DIR")
: "${QMAKE:?Set QMAKE to the Qt 6.8+ qmake used for this build}"
[[ $(uname -m) == x86_64 ]] || { echo 'x86_64 build host required' >&2; exit 1; }
# Refuse to dress a Nix-store-linked binary as a portable release.
if readelf -lWd "$build_dir/RuTool" | grep -q '/nix/store/'; then
    echo 'Portable packaging requires a conventional Linux SDK/container, without /nix/store dependencies.' >&2
    exit 1
fi
for tool in linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage appimagetool-x86_64.AppImage runtime-x86_64; do
    [[ -s "$tools_dir/$tool" ]] || { echo "Missing $tool" >&2; exit 1; }
    # Each required tool must actually appear in the caller-supplied lock manifest.
    grep -Eq "^[0-9a-fA-F]{64} [ *]${tool//./\.}$" "$tools_dir/SHA256SUMS" || { echo "No pinned checksum for $tool" >&2; exit 1; }
done
(cd -- "$tools_dir" && sha256sum --check --strict SHA256SUMS)
[[ ! -e "$output_dir" ]] || { echo "Output directory already exists: $output_dir (use a new directory)" >&2; exit 1; }
mkdir -p -- "$output_dir"
output_dir=$(realpath -- "$output_dir")
app_dir="$output_dir/RuTool.AppDir"
ctest --test-dir "$build_dir" --output-on-failure
cmake --install "$build_dir" --prefix "$app_dir/usr"
install -m755 "$source_dir/packaging/linux/AppRun" "$app_dir/AppRun"
install -m644 "$source_dir/packaging/linux/rutool.desktop" "$app_dir/rutool.desktop"
install -m644 "$source_dir/packaging/linux/rutool.svg" "$app_dir/rutool.svg"
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE
export QML_SOURCES_PATHS="$source_dir/qml"
export EXTRA_QT_MODULES='svg;waylandclient'
export EXTRA_PLATFORM_PLUGINS='libqoffscreen.so;libqwayland-egl.so;libqwayland-generic.so'
export PATH="$tools_dir:$PATH"
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1767225600}
export ARCH=x86_64 VERSION=2.0.8
# Dynamic plugins are not visible in the main ELF's DT_NEEDED entries.
qt_plugins=$("$QMAKE" -query QT_INSTALL_PLUGINS)
extra=()
for group in imageformats tls xcbglintegrations wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    [[ -d "$qt_plugins/$group" ]] || continue
    while IFS= read -r -d '' library; do extra+=(--library "$library"); done < <(find "$qt_plugins/$group" -name '*.so' -type f -print0)
done
# Bundle OpenSSL explicitly: Qt loads its TLS backend at runtime.
for library in libssl.so.3 libcrypto.so.3 libstdc++.so.6; do
    found=$(ldconfig -p | awk -v name="$library" '$1 == name && /x86-64/ && !found {found=$NF} END {print found}')
    [[ -n "$found" ]] || { echo "Required runtime library missing: $library" >&2; exit 1; }
    extra+=(--library "$found")
done
# Ensure dynamic plugin groups retain their Qt directory layout.
for group in imageformats tls xcbglintegrations wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    [[ -d "$qt_plugins/$group" ]] || continue
    mkdir -p "$app_dir/usr/plugins/$group"
    cp -a "$qt_plugins/$group/." "$app_dir/usr/plugins/$group/"
done
"$tools_dir/linuxdeploy-x86_64.AppImage" --appdir "$app_dir" \
    --executable "$app_dir/usr/bin/RuTool" --desktop-file "$app_dir/rutool.desktop" \
    --icon-file "$app_dir/rutool.svg" --custom-apprun "$source_dir/packaging/linux/AppRun" \
    "${extra[@]}" --plugin qt
cat > "$app_dir/usr/bin/qt.conf" <<'QTCONF'
[Paths]
Prefix=..
Libraries=lib
Plugins=plugins
QmlImports=qml
QTCONF
python3 "$source_dir/scripts/check-appdir.py" "$app_dir"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$app_dir/AppRun" --smoke-test --smoke-screenshot "$output_dir/appdir-smoke.png"
cp -- "$tools_dir/SHA256SUMS" "$output_dir/deployment-tools.sha256"
find "$app_dir" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
"$tools_dir/appimagetool-x86_64.AppImage" --runtime-file "$tools_dir/runtime-x86_64" \
    "$app_dir" "$output_dir/RuTool-$VERSION-x86_64.AppImage"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    "$output_dir/RuTool-$VERSION-x86_64.AppImage" --smoke-test
(cd -- "$output_dir" && sha256sum "RuTool-$VERSION-x86_64.AppImage" > SHA256SUMS)
echo "Created $output_dir/RuTool-$VERSION-x86_64.AppImage"
