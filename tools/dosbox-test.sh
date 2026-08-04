#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
emulator_log="$project_root/build/SELFTEST-EMULATOR.LOG"
rm -f "$project_root/build/SELFTEST.LOG" "$emulator_log"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy dosbox-x -conf "$project_root/dosbox-x.conf" \
    -silent -fastlaunch -time-limit 30 -c "mount c $project_root" -c "c:" \
    -c "cd build" -c "KOLOBOK.EXE -selftest > SELFTEST.LOG" -c "exit" \
    >"$emulator_log" 2>&1
if ! grep -q '^KOLOBOK SELFTEST PASS CRC=' "$project_root/build/SELFTEST.LOG"; then
    echo "DOSBox-X self-test failed:" >&2
    sed -n '1,80p' "$project_root/build/SELFTEST.LOG" >&2 || true
    tail -n 40 "$emulator_log" >&2 || true
    exit 1
fi
cat "$project_root/build/SELFTEST.LOG"
