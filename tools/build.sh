#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
watcom_root="$project_root/.tools/watcom-linux"
export WATCOM="$watcom_root"
export INCLUDE="$watcom_root/h"
export PATH="$watcom_root/binl64:$PATH"

mkdir -p "$project_root/build"
rm -f "$project_root/build/KOLOBOK.EXE" "$project_root/build/WATCOM.LOG"
pushd "$project_root/build" >/dev/null
if ! wcl -q -bt=dos -ms -3 -ox -s -i="$project_root/src" \
    -fe=KOLOBOK.EXE \
    "$project_root/src/main.c" "$project_root/src/game.c" \
    "$project_root/src/assets.c" "$project_root/src/platform.c" \
    "$project_root/src/video.c" >WATCOM.LOG 2>&1; then
    popd >/dev/null
    sed -n '1,200p' "$project_root/build/WATCOM.LOG" >&2
    exit 1
fi
popd >/dev/null
if [[ -s "$project_root/build/WATCOM.LOG" ]]; then
    cat "$project_root/build/WATCOM.LOG"
    echo "Open Watcom produced diagnostics" >&2
    exit 1
fi
echo "DOS build: PASS"
