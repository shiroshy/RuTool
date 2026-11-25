#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${RUTOOL_QT_SDK:?Set RUTOOL_QT_SDK to an existing Qt SDK root containing 6.8.3/gcc_64}"
image=${RUTOOL_BUILD_IMAGE:-rutool-linux-bootstrap:latest}
test_dir=${RUTOOL_TEST_BUILD_DIR:-build-linux-preview}
release_dir=${RUTOOL_RELEASE_BUILD_DIR:-build-linux-release}
output_dir=${1:-}
for path in "$test_dir" "$release_dir"; do
    [[ "$path" == build-* && "$path" != */* && "$path" != *..* ]] || { echo 'Use build-* directory names without slashes' >&2; exit 1; }
done
[[ "$test_dir" != "$release_dir" ]] || { echo 'Test and release directories must differ' >&2; exit 1; }
sdk=$(realpath -- "$RUTOOL_QT_SDK")
[[ -x "$sdk/6.8.3/gcc_64/bin/qmake" ]] || { echo 'Qt 6.8.3 SDK missing' >&2; exit 1; }
docker image inspect "$image" >/dev/null
mounts=(-v "$source_dir:/work" -v "$sdk:/opt/Qt:ro")
if [[ -n "$output_dir" ]]; then
    [[ "$output_dir" =~ ^build-[a-zA-Z0-9_.-]+/dist-[a-zA-Z0-9_.-]+$ && "$output_dir" != *..* ]] || { echo 'Use build-*/dist-* inside client-v2' >&2; exit 1; }
    [[ ! -e "$source_dir/$output_dir" ]] || { echo 'Output already exists; choose a new directory' >&2; exit 1; }
    : "${RUTOOL_APPIMAGE_TOOLS:?Set RUTOOL_APPIMAGE_TOOLS to an existing deployment-tool directory with SHA256SUMS}"
    tools=$(realpath -- "$RUTOOL_APPIMAGE_TOOLS")
    mounts+=(-v "$tools:/tools:ro")
fi
mkdir -p "$source_dir/$test_dir"
printf '%s\n' 11112222333344445555666677778888 > "$source_dir/$test_dir/machine-id"
mounts+=(-v "$source_dir/$test_dir/machine-id:/etc/machine-id:ro")
# The image, SDK and packaging tools must already exist. No network or installs.
docker run --rm --pull never --network none --read-only --cap-drop ALL \
    --security-opt no-new-privileges --user "$(id -u):$(id -g)" \
    --tmpfs /tmp:rw,exec,nosuid,size=1g -e HOME=/tmp -e LANG=C.UTF-8 \
    -e RUTOOL_BUILD_JOBS="${RUTOOL_BUILD_JOBS:-4}" "${mounts[@]}" "$image" \
    bash -c '
        set -euo pipefail
        ./scripts/build-linux.sh "$1"
        if [[ -n "$3" ]]; then
            ./scripts/build-linux.sh "$2" -DBUILD_TESTING=OFF
            ./scripts/package-appimage.sh "$2" "$3"
        fi
    ' bash "$test_dir" "$release_dir" "$output_dir"
