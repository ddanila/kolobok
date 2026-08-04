#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
exec dosbox-x -conf "$project_root/dosbox-x.conf" -fastlaunch \
    -c "mount c $project_root/build" -c "c:" -c "KOLOBOK.EXE"

