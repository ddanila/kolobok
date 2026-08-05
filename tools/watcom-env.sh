# shellcheck shell=bash
# shellcheck disable=SC2034  # these variables are consumed by the sourcing script
# Shared Open Watcom host detection. Source this file; do not execute it.
#
# Sets watcom_root, watcom_bindir, watcom_host, and watcom_release, and defines
# watcom_verify_sha256. Both tools/bootstrap-watcom.sh and tools/build.sh rely
# on this so the install location and the compile step can never disagree.
#
# Open Watcom v2 publishes ready-made installers for Linux and Windows only.
# Its CI also builds macOS x64 (bino64) and macOS arm64 (armo64) hosts, and
# those binaries ship inside the release's ow-snapshot.tar.xz, so macOS uses
# the snapshot instead of an installer.

watcom_release="2026-08-01-Build"
watcom_release_base="https://github.com/open-watcom/open-watcom-v2/releases/download/$watcom_release"

watcom_env_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

case "$(uname -s)" in
    Linux)
        case "$(uname -m)" in
            x86_64|amd64)
                watcom_host="linux-x64"
                watcom_bindir="binl64"
                ;;
            *)
                echo "Unsupported Linux architecture: $(uname -m)" >&2
                echo "Open Watcom v2 publishes x86-64 and x86 Linux hosts only." >&2
                return 1 2>/dev/null || exit 1
                ;;
        esac
        ;;
    Darwin)
        case "$(uname -m)" in
            arm64)
                watcom_host="macos-arm64"
                watcom_bindir="armo64"
                ;;
            x86_64)
                watcom_host="macos-x64"
                watcom_bindir="bino64"
                ;;
            *)
                echo "Unsupported macOS architecture: $(uname -m)" >&2
                return 1 2>/dev/null || exit 1
                ;;
        esac
        ;;
    *)
        echo "Unsupported host system: $(uname -s)" >&2
        echo "Supported hosts are Linux x86-64 and macOS (arm64 or x86-64)." >&2
        return 1 2>/dev/null || exit 1
        ;;
esac

watcom_root="$watcom_env_root/.tools/watcom-$watcom_host"

watcom_verify_sha256() {
    local expected="$1" file="$2" actual
    if command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$file" | awk '{print $1}')
    else
        actual=$(shasum -a 256 "$file" | awk '{print $1}')
    fi
    if [[ "$actual" != "$expected" ]]; then
        echo "Checksum mismatch for $file" >&2
        echo "  expected $expected" >&2
        echo "  actual   $actual" >&2
        return 1
    fi
}
