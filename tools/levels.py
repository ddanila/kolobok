#!/usr/bin/env python3
"""Deterministic Kolobok v4 JSON <-> KLV level compiler.

KLV deliberately contains no pointers and uses only little-endian fixed-size
records so both the DOS editor and host tools can rewrite it safely.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"KLV4"
VERSION = 4
HEIGHT = 11
HEADER = struct.Struct("<4sHHIHHBBI5H")
POINT = struct.Struct("<HH")
PICKUP = struct.Struct("<BBHHH")
ANIMAL = struct.Struct("<BBHHHHHHHHH")
TREE = struct.Struct("<BBHHH")
ENCOUNTER = struct.Struct("<HHBBBBH")

THEMES = {"garden": 0, "forest": 1, "deep": 2}
MATERIALS = {"grass": (1, 2, 3), "sand": (5, 6, 7), "ice": (8, 9, 10)}
PICKUPS = {"red_berry": 0, "blue_berry": 1, "small_pie": 2, "big_pie": 3}
ANIMALS = {"rabbit": 0, "hare": 0, "fox": 1, "wolf": 2, "bear": 3}
TREES = {"fir": 0, "birch": 1, "oak": 2}
REWARDS = {"none": 0, "blue_berry": 1, "small_pie": 2}


def _point(value: list[int], width: int, name: str) -> tuple[int, int]:
    if len(value) != 2 or not (0 <= value[0] < width and 0 <= value[1] < HEIGHT):
        raise ValueError(f"{name} is outside the {width}x{HEIGHT} level")
    return value[0], value[1]


def normalize(data: dict) -> dict:
    """Validate and return a canonical, directly reviewable level object."""
    width = int(data["width"])
    height = int(data.get("height", HEIGHT))
    if not 32 <= width <= 256 or height != HEIGHT:
        raise ValueError("level dimensions must be 32..256 tiles by exactly 11 rows")
    theme = data["theme"]
    if theme not in THEMES:
        raise ValueError(f"unknown theme {theme!r}")
    out = {
        "name": str(data.get("name", "UNTITLED"))[:16].upper(),
        "width": width,
        "height": HEIGHT,
        "theme": theme,
        "cloud_seed": int(data.get("cloud_seed", 1)) & 0xFFFFFFFF,
        "required_red_berries": int(data["required_red_berries"]),
        "ground_y": int(data.get("ground_y", 9)),
        "pits": [list(map(int, p)) for p in data.get("pits", [])],
        "surfaces": data.get("surfaces", []),
        "platforms": data.get("platforms", []),
        "start": list(_point(data["start"], width, "start")),
        "exit": list(_point(data["exit"], width, "exit")),
        "home": list(_point(data.get("home", data["exit"]), width, "home")),
        "checkpoints": [list(_point(p, width, "checkpoint")) for p in data.get("checkpoints", [])],
        "pickups": [], "animals": [], "trees": [], "encounters": [],
    }
    if not 0 <= out["ground_y"] < HEIGHT - 1:
        raise ValueError("ground_y must leave room for a body row")
    for first, last in out["pits"]:
        if not 0 <= first <= last < width:
            raise ValueError("pit range is outside the level")
    for item in data.get("pickups", []):
        if item["type"] not in PICKUPS:
            raise ValueError(f"unknown pickup {item['type']!r}")
        x, y = _point(item["at"], width, "pickup")
        out["pickups"].append({"id": int(item["id"]), "type": item["type"],
                               "at": [x, y], "flags": int(item.get("flags", 0))})
    ids = {p["id"] for p in out["pickups"]}
    if len(ids) != len(out["pickups"]):
        raise ValueError("pickup IDs must be unique")
    red_count = sum(p["type"] == "red_berry" for p in out["pickups"])
    if red_count < out["required_red_berries"]:
        raise ValueError("required red-berry count exceeds placed red berries")
    for item in data.get("animals", []):
        if item["type"] not in ANIMALS:
            raise ValueError(f"unknown animal {item['type']!r}")
        x, y = _point(item["at"], width, "animal")
        patrol = item.get("patrol", [x, x])
        if len(patrol) != 2 or not 0 <= patrol[0] <= x <= patrol[1] < width:
            raise ValueError("animal patrol must contain its position")
        climb = item.get("climb", [y, y])
        out["animals"].append({
            "id": int(item["id"]), "type": item["type"], "at": [x, y],
            "patrol": list(map(int, patrol)), "tree_id": int(item.get("tree_id", 0xFFFF)),
            "climb": list(map(int, climb)), "dialogue_id": int(item.get("dialogue_id", 0xFFFF)),
            "reward": item.get("reward", "none"), "flags": int(item.get("flags", 0)),
        })
    animal_ids = {a["id"] for a in out["animals"]}
    if len(animal_ids) != len(out["animals"]):
        raise ValueError("animal IDs must be unique")
    for item in data.get("trees", []):
        if item["type"] not in TREES:
            raise ValueError(f"unknown tree {item['type']!r}")
        x, y = _point(item["at"], width, "tree")
        out["trees"].append({"id": int(item["id"]), "type": item["type"],
                             "at": [x, y], "height": int(item.get("height", 3)),
                             "flags": int(item.get("flags", 0))})
    tree_ids = {t["id"] for t in out["trees"]}
    for animal in out["animals"]:
        if animal["tree_id"] != 0xFFFF and animal["tree_id"] not in tree_ids:
            raise ValueError(f"animal {animal['id']} refers to a missing tree")
    for item in data.get("encounters", []):
        animal_id = int(item["animal_id"])
        if animal_id not in animal_ids:
            raise ValueError("encounter refers to a missing animal")
        correct = int(item["correct"])
        if not 0 <= correct < 3:
            raise ValueError("encounter correct answer must be 0, 1, or 2")
        reward = item.get("reward", "none")
        if reward not in REWARDS:
            raise ValueError(f"unknown reward {reward!r}")
        out["encounters"].append({
            "id": int(item["id"]), "animal_id": animal_id,
            "dialogue_id": int(item.get("dialogue_id", item["id"])),
            "required": bool(item.get("required", False)), "correct": correct,
            "reward": reward, "retry_seconds": int(item.get("retry_seconds", 5)),
        })
    required_guardians = sum(e["required"] for e in out["encounters"])
    if required_guardians != 1:
        raise ValueError("each campaign level must have exactly one required guardian")
    if len(out["pickups"]) > 32 or len(out["animals"]) > 16 or len(out["trees"]) > 32:
        raise ValueError("object count exceeds DOS runtime limits")
    return out


def build_map(level: dict) -> bytes:
    width = level["width"]
    cells = bytearray(width * HEIGHT)
    ground = level["ground_y"]
    material_at = ["grass"] * width
    for surface in level["surfaces"]:
        material = surface["material"]
        if material not in MATERIALS:
            raise ValueError(f"unknown surface material {material!r}")
        first, last = surface["range"]
        if not 0 <= first <= last < width:
            raise ValueError("surface range is outside level")
        material_at[first:last + 1] = [material] * (last - first + 1)
    pits = {x for a, b in level["pits"] for x in range(a, b + 1)}
    for x in range(width):
        if x in pits:
            cells[(ground + 1) * width + x] = 4
        else:
            top, body, _ = MATERIALS[material_at[x]]
            cells[ground * width + x] = top
            cells[(ground + 1) * width + x] = body
    for group in level["platforms"]:
        material = group.get("material", "grass")
        tile = MATERIALS[material][2]
        y = int(group["y"])
        for first, last in group["ranges"]:
            if not 0 <= first <= last < width or not 0 <= y < HEIGHT:
                raise ValueError("platform range is outside level")
            for x in range(first, last + 1):
                cells[y * width + x] = tile
    for marker in (level["start"], level["exit"], level["home"], *level["checkpoints"]):
        if cells[marker[1] * width + marker[0]] != 0:
            raise ValueError(f"marker at {marker[0]},{marker[1]} is inside terrain")
    return bytes(cells)


def encode(source: dict) -> bytes:
    level = normalize(source)
    cells = build_map(level)
    body = bytearray(cells)
    for key in ("start", "exit", "home"):
        body.extend(POINT.pack(*level[key]))
    for p in level["checkpoints"]:
        body.extend(POINT.pack(*p))
    for p in level["pickups"]:
        body.extend(PICKUP.pack(PICKUPS[p["type"]], p["flags"], p["id"], *p["at"]))
    for a in level["animals"]:
        body.extend(ANIMAL.pack(ANIMALS[a["type"]], a["flags"], a["id"],
            a["at"][0], a["at"][1], a["patrol"][0], a["patrol"][1],
            a["tree_id"], a["climb"][0], a["climb"][1], a["dialogue_id"]))
    for t in level["trees"]:
        body.extend(TREE.pack(TREES[t["type"]], t["flags"], t["id"],
                              t["at"][0], t["at"][1] | (t["height"] << 8)))
    for e in level["encounters"]:
        body.extend(ENCOUNTER.pack(e["id"], e["animal_id"], e["dialogue_id"],
            int(e["required"]), e["correct"], REWARDS[e["reward"]], e["retry_seconds"] * 30))
    crc = zlib.crc32(body) & 0xFFFFFFFF
    header = HEADER.pack(MAGIC, VERSION, HEADER.size, crc, level["width"], HEIGHT,
        THEMES[level["theme"]], level["required_red_berries"], level["cloud_seed"],
        len(level["checkpoints"]), len(level["pickups"]), len(level["animals"]),
        len(level["trees"]), len(level["encounters"]))
    return header + body


def decode(blob: bytes) -> dict:
    if len(blob) < HEADER.size:
        raise ValueError("truncated KLV header")
    (magic, version, header_size, expected_crc, width, height, theme, required,
     seed, checkpoint_count, pickup_count, animal_count, tree_count,
     encounter_count) = HEADER.unpack_from(blob)
    if magic != MAGIC or version != VERSION or header_size != HEADER.size:
        raise ValueError("unsupported KLV format (expected KLV4 version 4)")
    if not 32 <= width <= 256 or height != HEIGHT or theme >= len(THEMES):
        raise ValueError("invalid KLV metadata")
    body = blob[header_size:]
    if zlib.crc32(body) & 0xFFFFFFFF != expected_crc:
        raise ValueError("KLV checksum mismatch")
    expected = width * height + 3 * POINT.size + checkpoint_count * POINT.size + \
        pickup_count * PICKUP.size + animal_count * ANIMAL.size + tree_count * TREE.size + \
        encounter_count * ENCOUNTER.size
    if len(body) != expected:
        raise ValueError("unexpected KLV payload size")
    # Export a canonical low-level JSON representation. It round-trips byte-for-byte
    # through encode_map_json and is suitable for recovering DOS editor changes.
    pos = width * height
    cells = list(body[:pos])
    def take(fmt: struct.Struct) -> tuple:
        nonlocal pos
        value = fmt.unpack_from(body, pos); pos += fmt.size; return value
    inv_theme = {v: k for k, v in THEMES.items()}
    inv_pickup = {v: k for k, v in PICKUPS.items()}
    inv_animal = {v: k for k, v in ANIMALS.items() if k != "hare"}
    inv_tree = {v: k for k, v in TREES.items()}
    inv_reward = {v: k for k, v in REWARDS.items()}
    result = {"format": "KLV4", "width": width, "height": height,
              "theme": inv_theme[theme], "cloud_seed": seed,
              "required_red_berries": required, "tiles": cells,
              "start": list(take(POINT)), "exit": list(take(POINT)),
              "home": list(take(POINT)), "checkpoints": [], "pickups": [],
              "animals": [], "trees": [], "encounters": []}
    for _ in range(checkpoint_count): result["checkpoints"].append(list(take(POINT)))
    for _ in range(pickup_count):
        kind, flags, ident, x, y = take(PICKUP)
        result["pickups"].append({"id": ident, "type": inv_pickup[kind], "at": [x, y], "flags": flags})
    for _ in range(animal_count):
        kind, flags, ident, x, y, p0, p1, tree, c0, c1, dialogue = take(ANIMAL)
        result["animals"].append({"id": ident, "type": inv_animal[kind], "at": [x, y],
            "patrol": [p0, p1], "tree_id": tree, "climb": [c0, c1],
            "dialogue_id": dialogue, "flags": flags})
    for _ in range(tree_count):
        kind, flags, ident, x, yh = take(TREE)
        result["trees"].append({"id": ident, "type": inv_tree[kind],
            "at": [x, yh & 0xff], "height": yh >> 8, "flags": flags})
    for _ in range(encounter_count):
        ident, animal, dialogue, req, correct, reward, retry = take(ENCOUNTER)
        result["encounters"].append({"id": ident, "animal_id": animal,
            "dialogue_id": dialogue, "required": bool(req), "correct": correct,
            "reward": inv_reward[reward], "retry_seconds": retry // 30})
    return result


def encode_map_json(source: dict) -> bytes:
    """Encode canonical JSON produced by decode without reconstructing terrain."""
    if source.get("format") != "KLV4" or "tiles" not in source:
        return encode(source)
    data = dict(source)
    width = int(data["width"])
    cells = bytes(data["tiles"])
    if len(cells) != width * HEIGHT or any(t > 10 for t in cells):
        raise ValueError("canonical tile map is invalid")
    # Derive semantic terrain only for validation-free byte preservation.
    theme = THEMES[data["theme"]]
    body = bytearray(cells)
    for key in ("start", "exit", "home"): body.extend(POINT.pack(*data[key]))
    for p in data["checkpoints"]: body.extend(POINT.pack(*p))
    for p in data["pickups"]: body.extend(PICKUP.pack(PICKUPS[p["type"]], p.get("flags", 0), p["id"], *p["at"]))
    for a in data["animals"]: body.extend(ANIMAL.pack(ANIMALS[a["type"]], a.get("flags", 0), a["id"], *a["at"], *a["patrol"], a.get("tree_id", 0xffff), *a.get("climb", a["at"][1:]*2), a.get("dialogue_id", 0xffff)))
    for t in data["trees"]: body.extend(TREE.pack(TREES[t["type"]], t.get("flags", 0), t["id"], t["at"][0], t["at"][1] | (t.get("height", 3) << 8)))
    for e in data["encounters"]: body.extend(ENCOUNTER.pack(e["id"], e["animal_id"], e.get("dialogue_id", e["id"]), int(e.get("required", False)), e["correct"], REWARDS[e.get("reward", "none")], e.get("retry_seconds", 5) * 30))
    crc = zlib.crc32(body) & 0xffffffff
    return HEADER.pack(MAGIC, VERSION, HEADER.size, crc, width, HEIGHT, theme,
        data["required_red_berries"], data.get("cloud_seed", 1), len(data["checkpoints"]),
        len(data["pickups"]), len(data["animals"]), len(data["trees"]), len(data["encounters"])) + body


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    imp = sub.add_parser("import", help="compile JSON to KLV")
    imp.add_argument("json", type=Path); imp.add_argument("klv", type=Path)
    exp = sub.add_parser("export", help="export KLV to canonical JSON")
    exp.add_argument("klv", type=Path); exp.add_argument("json", type=Path)
    val = sub.add_parser("validate", help="validate a KLV")
    val.add_argument("klv", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "import":
            blob = encode_map_json(json.loads(args.json.read_text()))
            args.klv.parent.mkdir(parents=True, exist_ok=True); args.klv.write_bytes(blob)
            print(f"levels: wrote {args.klv} ({len(blob)} bytes)")
        elif args.command == "export":
            result = decode(args.klv.read_bytes())
            args.json.parent.mkdir(parents=True, exist_ok=True)
            args.json.write_text(json.dumps(result, indent=2) + "\n")
            print(f"levels: wrote {args.json}")
        else:
            result = decode(args.klv.read_bytes())
            print(f"levels: PASS {args.klv} ({result['width']}x{result['height']})")
    except (OSError, ValueError, KeyError, struct.error, json.JSONDecodeError) as exc:
        print(f"levels: {exc}", file=sys.stderr); raise SystemExit(2)


if __name__ == "__main__":
    main()
