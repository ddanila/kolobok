#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
capture_log="$project_root/build/CAPTURE.LOG"
capture_ppm="$project_root/build/KOLOBOK.PPM"
capture_dir="$project_root/docs/captures"
crc_manifest="$capture_dir/CRC32.txt"

mkdir -p "$capture_dir"
printf 'DOSBox-X Mode X displayed-page CRC-32 reference set\n' > "$crc_manifest"
for scene in intro garden forest deep dialogue frozen gameover home; do
    rm -f "$capture_log" "$capture_ppm"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy dosbox-x \
        -conf "$project_root/dosbox-x.conf" -silent -fastlaunch -time-limit 20 \
        -c "mount c $project_root" -c "c:" -c "cd build" \
        -c "KOLOBOK.EXE -capture $scene > CAPTURE.LOG" -c "exit" >/dev/null 2>&1
    grep -q '^KOLOBOK CAPTURE PASS KOLOBOK.PPM CRC=' "$capture_log"
    python3 - "$capture_ppm" "$capture_dir/$scene.png" <<'PY'
import sys
from pathlib import Path
from PIL import Image
source, target = map(Path, sys.argv[1:])
with Image.open(source) as image:
    assert image.size == (320, 200)
    image.save(target, optimize=False)
PY
    result=$(tr -d '\r' < "$capture_log")
    crc=${result##*CRC=}
    printf '%-10s %s\n' "$scene" "$crc" >> "$crc_manifest"
    printf '%-10s %s\n' "$scene" "$result"
done
cp "$capture_dir/garden.png" "$project_root/docs/screenshot.png"
echo "screenshots: wrote $capture_dir and docs/screenshot.png"
