#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
playtest_log="$project_root/build/PLAYTEST.LOG"
emulator_log="$project_root/build/PLAY-EMU.LOG"
rm -f "$playtest_log" "$emulator_log"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy dosbox-x -conf "$project_root/dosbox-x.conf" \
    -silent -fastlaunch -time-limit 45 -c "mount c $project_root" -c "c:" \
    -c "cd build" -c "KOLOBOK.EXE -playtest > PLAYTEST.LOG" -c "exit" \
    >"$emulator_log" 2>&1
if ! grep -q '^KOLOBOK PLAYTEST PASS sequential carry' "$playtest_log"; then
    echo "DOSBox-X campaign playthrough failed:" >&2
    cat "$playtest_log" >&2 || true
    tail -n 40 "$emulator_log" >&2 || true
    exit 1
fi
cat "$playtest_log"
