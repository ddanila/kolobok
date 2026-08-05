#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$project_root/tools/watcom-env.sh"

tool_root="$project_root/.tools"

linux_installer_sha="e1bc4e88fa47191118f29e53731e7f4542803c7f4c503e15d72f1f571ac0832f"
snapshot_sha="6279e1bf7aea4ceba24539d7924f095142047fa55d352b3ba96f33c81ededd32"

if [[ -x "$watcom_root/$watcom_bindir/wcl" && -x "$watcom_root/$watcom_bindir/wlink" ]]; then
    echo "Open Watcom: already installed at $watcom_root"
    exit 0
fi
mkdir -p "$tool_root"

case "$watcom_host" in
    linux-x64)
        installer="$tool_root/open-watcom-linux-x64"
        if [[ ! -f "$installer" ]]; then
            curl -fL --retry 2 -o "$installer" "$watcom_release_base/open-watcom-2_0-c-linux-x64"
        fi
        watcom_verify_sha256 "$linux_installer_sha" "$installer"
        mkdir -p "$watcom_root"
        unzip -q -o "$installer" -d "$watcom_root"
        ;;
    macos-arm64|macos-x64)
        # No macOS installer is published, but the release snapshot carries the
        # CI-built macOS hosts. Only the host binaries, headers, and 16-bit DOS
        # libraries are unpacked; the snapshot also holds every other host.
        snapshot="$tool_root/ow-snapshot.tar.xz"
        if [[ ! -f "$snapshot" ]]; then
            curl -fL --retry 2 -o "$snapshot" "$watcom_release_base/ow-snapshot.tar.xz"
        fi
        watcom_verify_sha256 "$snapshot_sha" "$snapshot"
        mkdir -p "$watcom_root"
        tar -xJf "$snapshot" -C "$watcom_root" --strip-components=1 \
            "./$watcom_bindir" ./h ./lib286 ./lib386
        # curl does not set com.apple.quarantine, but a manually placed archive
        # can, and a quarantined toolchain fails with an opaque kill.
        xattr -dr com.apple.quarantine "$watcom_root" 2>/dev/null || true
        ;;
    *)
        echo "No Open Watcom install recipe for host $watcom_host" >&2
        exit 1
        ;;
esac

chmod +x "$watcom_root/$watcom_bindir"/* 2>/dev/null || true
if [[ ! -x "$watcom_root/$watcom_bindir/wcl" || ! -x "$watcom_root/$watcom_bindir/wlink" ]]; then
    echo "Open Watcom installation failed: $watcom_bindir/wcl or wlink missing" >&2
    exit 1
fi
# wcl exits 1 after printing usage, so check the banner rather than the status.
# This catches a wrong-architecture or unsigned binary, which dies without output.
# The output is captured before matching because pipefail would otherwise report
# wcl's usage exit status for the whole pipeline.
banner=$("$watcom_root/$watcom_bindir/wcl" 2>&1 || true)
if ! printf '%s' "$banner" | grep -q "Open Watcom"; then
    echo "Open Watcom installed but $watcom_bindir/wcl will not execute" >&2
    exit 1
fi
echo "Open Watcom: installed $watcom_host host and DOS target at $watcom_root"
