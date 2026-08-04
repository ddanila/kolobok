#!/usr/bin/env python3
from __future__ import annotations

import json
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import levels  # noqa: E402


def main() -> None:
    expected = {"garden": (96, 6), "sforest": (128, 8), "dforest": (160, 10)}
    for name, (width, red) in expected.items():
        source = json.loads((ROOT / "assets" / f"{name}.json").read_text())
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
    print("level/archive tests: PASS (round-trip, CRC, validation, bank limits)")


if __name__ == "__main__": main()
