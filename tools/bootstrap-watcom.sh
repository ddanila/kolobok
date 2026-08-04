#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tool_root="$project_root/.tools"
watcom_root="$tool_root/watcom-linux"
installer="$tool_root/open-watcom-linux-x64"
installer_url="https://github.com/open-watcom/open-watcom-v2/releases/download/2026-08-01-Build/open-watcom-2_0-c-linux-x64"
installer_sha="e1bc4e88fa47191118f29e53731e7f4542803c7f4c503e15d72f1f571ac0832f"

if [[ -x "$watcom_root/binl64/wcl" && -x "$watcom_root/binl64/wlink" ]]; then
    echo "Open Watcom: already installed at $watcom_root"
    exit 0
fi
mkdir -p "$tool_root"
if [[ ! -f "$installer" ]]; then
    curl -fL --retry 2 -o "$installer" "$installer_url"
fi
printf '%s  %s\n' "$installer_sha" "$installer" | sha256sum -c -
mkdir -p "$watcom_root"
unzip -q -o "$installer" -d "$watcom_root"
chmod +x "$watcom_root"/binl64/*
if [[ ! -x "$watcom_root/binl64/wcl" || ! -x "$watcom_root/binl64/wlink" ]]; then
    echo "Open Watcom installation failed" >&2
    exit 1
fi
echo "Open Watcom: installed Linux x64 host and DOS target at $watcom_root"
