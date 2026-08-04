#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
capture_log="$project_root/build/CAPTURE.LOG"
capture_ppm="$project_root/build/KOLOBOK.PPM"
capture_png="$project_root/docs/screenshot.png"

rm -f "$capture_log" "$capture_ppm"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy dosbox-x \
    -conf "$project_root/dosbox-x.conf" -silent -fastlaunch -time-limit 20 \
    -c "mount c $project_root" -c "c:" -c "cd build" \
    -c "KOLOBOK.EXE -capture > CAPTURE.LOG" -c "exit" >/dev/null 2>&1
grep -q '^KOLOBOK CAPTURE PASS KOLOBOK.PPM' "$capture_log"
python3 - "$capture_ppm" "$capture_png" <<'PY'
import sys
from pathlib import Path

from PIL import Image

source, target = map(Path, sys.argv[1:])
with Image.open(source) as image:
    assert image.size == (320, 200)
    image.save(target, optimize=False)
print(f"screenshot: wrote {target}")
PY
