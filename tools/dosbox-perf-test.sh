#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
target_cycles=7350
minimum_fps10=360
minimum_paced10=295
perf_log="$project_root/build/PERF.LOG"
emulator_log="$project_root/build/PERF-EMULATOR.LOG"

rm -f "$perf_log" "$emulator_log"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy dosbox-x \
    -conf "$project_root/dosbox-x.conf" \
    -set "cpu cycles=fixed $target_cycles" \
    -silent -fastlaunch -time-limit 30 \
    -c "mount c $project_root" -c "c:" -c "cd build" \
    -c "KOLOBOK.EXE -benchmark > PERF.LOG" -c "exit" \
    >"$emulator_log" 2>&1

result=$(sed -n 's/\r$//; s/^KOLOBOK BENCH .* fps10=\([0-9][0-9]*\) paced10=[0-9][0-9]*$/\1/p' "$perf_log")
paced=$(sed -n 's/\r$//; s/^KOLOBOK BENCH .* paced10=\([0-9][0-9]*\)$/\1/p' "$perf_log")
if [[ -z "$result" || -z "$paced" ]]; then
    echo "386DX-40 performance test produced no result" >&2
    sed -n '1,80p' "$perf_log" >&2 || true
    exit 1
fi
if (( result < minimum_fps10 )); then
    printf '386DX-40 performance: FAIL (%d.%d fps; minimum %d.%d)\n' \
        "$((result / 10))" "$((result % 10))" \
        "$((minimum_fps10 / 10))" "$((minimum_fps10 % 10))" >&2
    exit 1
fi
if (( paced < minimum_paced10 )); then
    printf '386DX-40 pacing: FAIL (%d.%d fps; minimum %d.%d)\n' \
        "$((paced / 10))" "$((paced % 10))" \
        "$((minimum_paced10 / 10))" "$((minimum_paced10 % 10))" >&2
    exit 1
fi
printf '386DX-40 performance: PASS (%d.%d fps; minimum %d.%d at %d cycles)\n' \
    "$((result / 10))" "$((result % 10))" \
    "$((minimum_fps10 / 10))" "$((minimum_fps10 % 10))" "$target_cycles"
printf '386DX-40 pacing: PASS (%d.%d fps; 30 Hz gameplay target)\n' \
    "$((paced / 10))" "$((paced % 10))"
