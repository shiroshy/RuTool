#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$source_dir"
output_dir=${1:-dist-appimage}
[[ "$output_dir" != /* && "$output_dir" != *..* ]] || { echo 'Use a relative output directory inside client-v2' >&2; exit 1; }
[[ ! -e "$output_dir" ]] || { echo "Output already exists: $output_dir (choose a new directory)" >&2; exit 1; }
# Keep the large Qt SDK on the project disk instead of duplicating Docker layers.
mkdir -p build-linux-sdk build-linux-tools build-linux-release-tests
printf '%s\n' 11112222333344445555666677778888 > build-linux-release-tests/machine-id
docker build --target sdk-bootstrap -f packaging/linux/Dockerfile -t rutool-linux-bootstrap .
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e LANG=C.UTF-8 \
    -e RUTOOL_BUILD_JOBS="${RUTOOL_BUILD_JOBS:-4}" \
    -v "$source_dir:/work" -v "$source_dir/build-linux-sdk:/opt/Qt" \
    -v "$source_dir/build-linux-release-tests/machine-id:/etc/machine-id:ro" \
    rutool-linux-bootstrap bash -c '
        set -euo pipefail
        if [[ ! -x "$QMAKE" ]]; then
            /opt/aqt/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
                --outputdir /opt/Qt --modules qtimageformats qtshadertools
        fi
        ./scripts/fetch-appimage-tools.sh build-linux-tools
        ./scripts/build-linux.sh build-linux-release-tests
        ./scripts/build-linux.sh build-appimage -DBUILD_TESTING=OFF
        export RUTOOL_TOOLS_DIR=/work/build-linux-tools
        ./scripts/package-appimage.sh build-appimage "$1"
    ' bash "$output_dir"
