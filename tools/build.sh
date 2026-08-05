#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$project_root/tools/watcom-env.sh"
export WATCOM="$watcom_root"
export INCLUDE="$watcom_root/h"
export PATH="$watcom_root/$watcom_bindir:$PATH"

# KOLO_TRACE=1 builds the ring-buffer diagnostics into both executables. The
# shipping build must stay free of them, so this is opt-in and never default.
trace_define=()
if [[ "${KOLO_TRACE:-0}" != "0" ]]; then
    trace_define=(-dKOLO_TRACE)
    echo "DOS build: tracing enabled (KOLO_TRACE)"
fi

mkdir -p "$project_root/build"
rm -f "$project_root/build/KOLOBOK.EXE" "$project_root/build/KOLOEDIT.EXE" "$project_root/build/WATCOM.LOG"
pushd "$project_root/build" >/dev/null
if ! wcl -q -bt=dos -ms -3 -zastd=c++0x -ox -s -k8192 -i="$project_root/src" ${trace_define[@]+"${trace_define[@]}"} \
    -fe=KOLOBOK.EXE \
    "$project_root/src/main.cpp" "$project_root/src/game.cpp" \
    "$project_root/src/game_state.cpp" \
    "$project_root/src/assets.cpp" "$project_root/src/platform.cpp" \
    "$project_root/src/video.cpp" "$project_root/src/music.cpp" \
    "$project_root/src/trace.cpp" >WATCOM.LOG 2>&1; then
    popd >/dev/null
    sed -n '1,200p' "$project_root/build/WATCOM.LOG" >&2
    exit 1
fi
if ! wcl -q -bt=dos -ms -3 -zastd=c++0x -ox -s -k16384 -i="$project_root/src" ${trace_define[@]+"${trace_define[@]}"} \
    -fe=KOLOEDIT.EXE \
    "$project_root/src/editor.cpp" "$project_root/src/editcore.cpp" \
    "$project_root/src/game_state.cpp" \
    "$project_root/src/assets.cpp" "$project_root/src/platform.cpp" \
    "$project_root/src/video.cpp" "$project_root/src/videoedit.cpp" \
    "$project_root/src/trace.cpp" >WATCOM-EDITOR.LOG 2>&1; then
    popd >/dev/null
    sed -n '1,200p' "$project_root/build/WATCOM-EDITOR.LOG" >&2
    exit 1
fi
popd >/dev/null
if [[ -s "$project_root/build/WATCOM.LOG" || -s "$project_root/build/WATCOM-EDITOR.LOG" ]]; then
    cat "$project_root/build/WATCOM.LOG"
    cat "$project_root/build/WATCOM-EDITOR.LOG"
    echo "Open Watcom produced diagnostics" >&2
    exit 1
fi
echo "DOS build: PASS"
