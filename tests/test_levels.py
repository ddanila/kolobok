#!/usr/bin/env python3
from __future__ import annotations

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KLVCHECK = ROOT / "build" / "klvcheck"
sys.path.insert(0, str(ROOT / "tools"))
import levels  # noqa: E402


def klvcheck(*paths: Path) -> subprocess.CompletedProcess[str]:
    """Runs the shipping C++ level_validate over KLV files."""
    return subprocess.run([KLVCHECK, *map(str, paths)], capture_output=True, text=True)


def check_compiler_agrees_with_runtime() -> None:
    """Whatever tools/levels.py accepts, the DOS runtime's validator must accept.

    The two rule sets are written independently, so only a test can keep them
    from drifting -- and the drift is not hypothetical: the compiler still has
    no opinion on bear climb rows, which the runtime now rejects.
    """
    # Derived from the sources rather than globbing build/, which also collects
    # scratch levels the DOS editor self-test leaves behind when it aborts.
    built = [ROOT / "build" / f"{source.stem.upper()}.KLV"
             for source in sorted((ROOT / "assets" / "levels").glob("*.json"))]
    assert built and all(path.exists() for path in built), "no compiled levels to check"
    done = klvcheck(*built)
    assert done.returncode == 0, f"compiler emitted a level the runtime rejects:\n{done.stdout}"

    source = json.loads((ROOT / "assets" / "levels" / "dforest.json").read_text())
    bear = next(a for a in source["animals"] if a["type"] == "bear")
    bear["climb"] = [400, 400]
    with tempfile.TemporaryDirectory() as work:
        bad = Path(work) / "BAD.KLV"
        bad.write_bytes(levels.encode(source))  # the compiler has no climb check
        done = klvcheck(bad)
        assert done.returncode == 1 and "climb" in done.stdout, \
            f"runtime accepted an out-of-range climb range: {done.stdout!r}"


def main() -> None:
    expected = {"garden": (96, 6), "sforest": (128, 8), "dforest": (160, 10)}
    for name, (width, red) in expected.items():
        source = json.loads((ROOT / "assets" / "levels" / f"{name}.json").read_text())
        blob = levels.encode(source)
        assert levels.encode_map_json(levels.decode(blob)) == blob
        decoded = levels.decode(blob)
        assert decoded["width"] == width and decoded["required_red_berries"] == red
        corrupt = bytearray(blob); corrupt[-1] ^= 1
        try: levels.decode(corrupt)
        except ValueError as exc: assert "checksum" in str(exc)
        else: raise AssertionError("corrupt KLV accepted")
    archive = (ROOT / "build" / "KOLOBOK.DAT").read_bytes()
    assert archive[:8] == b"KOLODAT4" and len(archive) > 65535
    version, count = struct.unpack_from("<HH", archive, 8)
    assert version == 4 and count == 4
    names = []
    bank_payloads = []
    for i in range(count):
        name, offset, size = struct.unpack_from("<8sII", archive, 12 + i * 16)
        names.append(name.rstrip(b"\0").decode())
        assert size < 60 * 1024 and archive[offset:offset + 8] == b"KBANK4\0\0"
        bank_payloads.append(archive[offset:offset + size])
    assert names == ["INTRO", "GARDEN", "FOREST", "DEEP"]
    assert len(set(bank_payloads)) == 4, "resource banks must not be duplicates"
    assert len({bank[20:20 + 768] for bank in bank_payloads}) == 4, "bank palettes must be distinct"
    check_compiler_agrees_with_runtime()
    print("level/archive tests: PASS (round-trip, CRC, validation, runtime gate, bank limits)")


if __name__ == "__main__": main()
