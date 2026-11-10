#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export MAICORD_LINUX_BUILD="${MAICORD_LINUX_BUILD:-$root/../maicord-dome-build}"
# Upstream updates must never overwrite the DomeD fork. Plugin catalog verification is retained.
export MAICORD_UPDATE_REPOSITORY=
export MAICORD_UPDATE_PUBLIC_KEY=
arguments=("$@")
testing=0
selected=0
for argument in "$@"; do
    [[ "$argument" != --test ]] || testing=1
    case "$argument" in --test-names|--test-shard|--exclude-test-names) selected=1 ;; esac
done
if [[ "$testing" == 1 && "$selected" == 0 ]]; then
    # Outline-font contracts are intentionally retired in this fork.
    arguments+=(--test-names domed_bitmap_native,maicord_native_cbdt_fixture,maicord_plugin_abi,maicord_plugin_loader_elf)
fi
bash "$root/tools/build_linux.sh" "${arguments[@]}"
