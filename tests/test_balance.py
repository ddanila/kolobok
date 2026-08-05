#!/usr/bin/env python3
"""Campaign content counts and conservative route-balance checks."""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

EXPECTED = {
    "garden": (96, 6, {"rabbit": 6, "fox": 2}),
    "sforest": (128, 8, {"rabbit": 3, "fox": 3, "wolf": 2}),
    "dforest": (160, 10, {"fox": 1, "wolf": 5, "bear": 3}),
}


def check_level(name: str) -> None:
    level = json.loads((ROOT / "assets" / "levels" / f"{name}.json").read_text())
    width, reds, animals = EXPECTED[name]
    assert level["width"] == width and level["height"] == 11
    assert level["required_red_berries"] == reds
    placed_reds = [p for p in level["pickups"] if p["type"] == "red_berry"]
    assert len(placed_reds) == reds and all(p["at"][1] == 8 for p in placed_reds)
    actual: dict[str, int] = {}
    for animal in level["animals"]:
        actual[animal["type"]] = actual.get(animal["type"], 0) + 1
    assert actual == animals
    required = [e for e in level["encounters"] if e["required"]]
    optional = [e for e in level["encounters"] if not e["required"]]
    assert len(required) == 1 and optional and optional[0]["reward"] != "none"
    animal_by_id = {a["id"]: a for a in level["animals"]}
    assert animal_by_id[required[0]["animal_id"]]["at"][0] < level["exit"][0]
    # A one-tile pit is comfortably inside the weakest sand jump's measured
    # horizontal range, while still exercising instant fall/life-loss behavior.
    assert all(first == last for first, last in level["pits"])
    blocked_x = {x for group in level["platforms"] for first, last in group["ranges"]
                 for x in range(first, last + 1)}
    assert all(p["at"][0] not in blocked_x for p in placed_reds), \
        "required ground berry may not be trapped above/below a solid platform"
    # Both life-restoring pies must be on the main route. The hidden big pie is
    # deliberately exempt and remains an exploration reward.
    small_pies = [p for p in level["pickups"] if p["type"] == "small_pie"]
    assert len(small_pies) == 2 and all(p["at"][1] == 8 for p in small_pies)
    checkpoints = [p[0] for p in level["checkpoints"]]
    assert checkpoints == sorted(checkpoints) and checkpoints[-1] < level["exit"][0]


def main() -> None:
    for name in EXPECTED:
        check_level(name)
    print("campaign balance tests: PASS (counts, routes, pits, pies, guardians)")


if __name__ == "__main__":
    main()
