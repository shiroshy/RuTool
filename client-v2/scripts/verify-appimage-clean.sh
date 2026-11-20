#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=$(realpath -- "${1:?Usage: verify-appimage-clean.sh AppImage [report-directory]}")
report_dir=${2:-"$source_dir/build-linux-validation"}
mkdir -p -- "$report_dir"
report_dir=$(realpath -- "$report_dir")
docker run --rm --tmpfs /tmp:exec,size=768m \
    -v "$image:/release/RuTool.AppImage:ro" -v "$report_dir:/report" \
    -v "$source_dir/scripts/extract-appimage.py:/verify/extract-appimage.py:ro" \
    ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852 \
    bash -c '
        set -euo pipefail
        apt-get update -qq
        apt-get install -y --no-install-recommends ca-certificates libgl1 libegl1 \
            libfontconfig1 libxkbcommon0 libdbus-1-3 fonts-dejavu-core python3 squashfs-tools >/dev/null
        if find /usr /opt -name "libQt6*.so*" -print -quit | grep .; then
            echo "Unexpected host Qt in clean container" >&2; exit 1
        fi
        echo "Clean Ubuntu 24.04: no installed Qt, SDK or /nix/store"
        test ! -e /nix/store
        python3 /verify/extract-appimage.py /release/RuTool.AppImage /tmp/RuTool.AppDir
        env -i PATH=/usr/bin:/bin HOME=/tmp LANG=C.UTF-8 \
            QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
            /tmp/RuTool.AppDir/AppRun --smoke-test \
                --smoke-screenshot /report/clean-smoke.png
        echo "Clean container: production QML rendered successfully"
    ' 2>&1 | tee "$report_dir/clean-smoke.log"
