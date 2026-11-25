#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tools_dir=${1:-"$source_dir/build-linux-tools"}
mkdir -p -- "$tools_dir"
tools_dir=$(realpath -- "$tools_dir")
while read -r name checksum url; do
    [[ -n "$name" && "$name" != \#* ]] || continue
    if [[ ! -f "$tools_dir/$name" ]]; then
        curl --fail --location --show-error --retry 3 --proto '=https' --proto-redir '=https' \
            "$url" --output "$tools_dir/$name.download"
        printf '%s  %s\n' "$checksum" "$tools_dir/$name.download" | sha256sum --check --strict
        mv -- "$tools_dir/$name.download" "$tools_dir/$name"
    fi
    printf '%s  %s\n' "$checksum" "$tools_dir/$name" | sha256sum --check --strict
    chmod 755 "$tools_dir/$name"
done < "$source_dir/packaging/linux/deployment-tools.lock"
awk '!/^#/ && NF {print $2 "  " $1}' "$source_dir/packaging/linux/deployment-tools.lock" > "$tools_dir/SHA256SUMS"
