#!/usr/bin/env python3
# Station deck packing. Keep this in lockstep with NaanStation.svelte buildDeck.
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import json
from typing import Iterable

HERE = Path(__file__).resolve().parent
KEPLER = HERE.parents[1]
LIB = KEPLER / "tauri-app" / "src" / "lib"
PUBLIC = KEPLER / "tauri-app" / "public" / "station"
KINDS = ("bed", "tor", "lymph", "recipe", "poe")


@dataclass(frozen=True)
class CrewMember:
    id: str
    skin: str
    rooms: tuple[str, ...]


@dataclass
class Room:
    id: str
    kind: str
    owner: str
    name: str
    sn: str
    x1: int
    y1: int
    x2: int
    y2: int

    @property
    def w(self) -> int:
        return self.x2 - self.x1 + 1

    @property
    def h(self) -> int:
        return self.y2 - self.y1 + 1

    def contains(self, x: int, y: int) -> bool:
        return self.x1 <= x <= self.x2 and self.y1 <= y <= self.y2

    def pixels(self, ox: int, oy: int, tile: int) -> tuple[int, int, int, int]:
        return (
            (ox + self.x1) * tile,
            (oy + self.y1) * tile,
            self.w * tile,
            self.h * tile,
        )


@dataclass
class Overlay:
    room: str
    kind: str
    owner: str
    file: str
    tx: int
    ty: int
    role: str
    w: int
    h: int
    bx: float
    by: float
    bw: float
    bh: float


@dataclass
class Label:
    kind: str
    owner: str
    text: str
    x: float
    y: float
    w: float
    h: float


@dataclass
class Deck:
    rooms: list[Room]
    halls: list[tuple[int, int, int, int]]
    overlays: list[Overlay]
    ox: int
    oy: int
    st_cols: int
    st_rows: int
    view_cols: int
    view_rows: int
    tile: int
    dump_t: int
    labels: list[Label]
    banners: list[Label]
    walk: dict[str, set[str]]
    enabled: dict[str, list[str]]


@dataclass
class StationData:
    layout: dict
    catalog: dict
    views: dict
    public: Path
    tile: int
    dump_t: int
    primary_cols: int
    st_rows: int
    view_cols: int
    view_rows: int
    origin_y: int


def load_station_data() -> StationData:
    layout = json.loads((LIB / "stationLayout.json").read_text(encoding="utf-8"))
    catalog = json.loads((LIB / "stationCatalog.json").read_text(encoding="utf-8"))
    views = json.loads((LIB / "propViews.json").read_text(encoding="utf-8"))
    return StationData(
        layout=layout,
        catalog=catalog,
        views=views,
        public=PUBLIC,
        tile=int(layout.get("tile", 16)),
        dump_t=int(layout.get("dumpT", 12)),
        primary_cols=int(layout.get("gridCols", 26)),
        st_rows=int(layout.get("gridRows", 12)),
        view_cols=int(layout.get("viewCols", 64)),
        view_rows=int(layout.get("viewRows", 36)),
        origin_y=int(layout.get("originY", 14)),
    )


def agent_label(data: StationData, skin: str) -> str:
    for agent in data.catalog.get("agents", []):
        if agent.get("id") == skin:
            return str(agent.get("label") or skin)
    return skin


def agent_short_name(data: StationData, skin: str) -> str:
    label = agent_label(data, skin).strip()
    first = label.split()[0] if label else skin
    return first[:8].upper()


def resolve_prop(data: StationData, kind: str, file: str) -> str | None:
    spec = data.catalog["rooms"][kind]
    if file == "$prop":
        return spec.get("prop")
    if file == "$terminal":
        return spec.get("terminal")
    if file == "$chair":
        return spec.get("chair")
    return file


def view_of(data: StationData, file: str) -> dict:
    return data.views.get(file) or {"w": 1, "h": 1, "bx": 0, "by": 0, "bw": data.dump_t, "bh": data.dump_t}


def screenshot_crew() -> tuple[list[str], list[CrewMember]]:
    # Matches the live NAAN tab: Station Minion primary + Rick + Alien.
    return (
        ["bed", "tor", "lymph", "recipe", "poe"],
        [
            CrewMember("crew-rick", "ricksanchez", ("bed", "tor", "lymph", "recipe", "poe")),
            CrewMember("crew-alien", "alien", ("tor", "recipe", "poe")),
        ],
    )


def pack_wings(crew: Iterable[CrewMember], primary_cols: int, view_cols: int) -> list[int]:
    members = list(crew)
    n = len(members)
    if not n:
        return []
    prefer = [22 if "bed" in c.rooms else 15 for c in members]
    if primary_cols + sum(prefer) <= view_cols - 2:
        return prefer
    if primary_cols + 15 * n <= view_cols - 2:
        return [15] * n
    return [12] * n


def _shift_prop(ov: Overlay, src: Room, dest: Room) -> Overlay:
    dx = dest.x1 - src.x1
    dy = dest.y1 - src.y1
    tx = ov.tx + dx
    ty = ov.ty + dy
    max_x = max(dest.x1, dest.x2 - max(0, ov.w - 1))
    max_y = max(dest.y1, dest.y2 - max(0, ov.h - 1))
    tx = min(max(dest.x1, tx), max_x)
    ty = min(max(dest.y1, ty), max_y)
    return Overlay(dest.id, dest.kind, dest.owner, ov.file, tx, ty, ov.role, ov.w, ov.h, ov.bx, ov.by, ov.bw, ov.bh)


def _room_overlays(data: StationData, kind: str, owner: str, room_id: str) -> list[Overlay]:
    out: list[Overlay] = []
    lr = data.layout["rooms"][kind]
    for prop in lr["props"]:
        file = resolve_prop(data, kind, prop["file"])
        if not file:
            continue
        view = view_of(data, file)
        out.append(Overlay(
            room_id, kind, owner, file,
            int(prop["tx"]), int(prop["ty"]),
            prop.get("role") or "dress",
            int(view["w"]), int(view["h"]),
            float(view["bx"]), float(view["by"]),
            float(view["bw"]), float(view["bh"]),
        ))
    return out


def _base_room(data: StationData, kind: str) -> Room:
    lr = data.layout["rooms"][kind]
    spec = data.catalog["rooms"][kind]
    return Room(kind, kind, "primary", spec["title"], spec["sn"], lr["x1"], lr["y1"], lr["x2"], lr["y2"])


def _label_px(text: str, tile: int) -> tuple[float, float]:
    # 6px pixel face + 3px pad each side. Used by tests and the inspector.
    return max(tile * 1.6, len(text) * 5.2 + 8), 11.0


def _place_labels(deck_rooms: list[Room], ox: int, oy: int, tile: int, crew: list[CrewMember], data: StationData) -> tuple[list[Label], list[Label]]:
    labels: list[Label] = []
    banners: list[Label] = []
    skins = {c.id: c.skin for c in crew}
    for room in deck_rooms:
        if room.kind == "hall":
            continue
        w, h = _label_px(room.name, tile)
        x = (ox + room.x1) * tile + 2
        if room.y1 <= 2:
            y = (oy + room.y1) * tile - h - 24
        else:
            y = (oy + room.y2 + 1) * tile + 4
        labels.append(Label("room", room.owner, room.name, x, y, w, h))
    owners = []
    for room in deck_rooms:
        if room.kind == "hall" or room.owner == "primary":
            continue
        if room.owner not in owners:
            owners.append(room.owner)
    for owner in owners:
        owned = [r for r in deck_rooms if r.owner == owner and r.kind != "hall"]
        if not owned:
            continue
        text = agent_short_name(data, skins.get(owner, owner))
        w, h = _label_px(text, tile)
        x1 = min(r.x1 for r in owned)
        y1 = min(r.y1 for r in owned)
        banners.append(Label(
            "banner", owner, text,
            (ox + x1) * tile + 2,
            (oy + y1) * tile - h - 40,
            w, h,
        ))
    return labels, banners


def build_deck(primary_rooms: Iterable[str] | None = None, crew: Iterable[CrewMember] | None = None, data: StationData | None = None) -> Deck:
    data = data or load_station_data()
    enabled_primary = [k for k in (primary_rooms or KINDS) if k in KINDS] or ["bed"]
    crew_list = list(crew or [])
    layout = data.layout
    rooms: list[Room] = [Room(
        "hall", "hall", "primary", "HALL", "",
        layout["hall"]["x1"], layout["hall"]["y1"],
        layout["hall"]["x2"], layout["hall"]["y2"],
    )]
    halls = [(c["x1"], c["y1"], c["x2"], c["y2"]) for c in layout["corridors"]]
    overlays: list[Overlay] = []
    enabled: dict[str, list[str]] = {"primary": list(enabled_primary)}
    walk_halls: dict[str, list[tuple[int, int, int, int]]] = {"primary": list(halls)}
    bases = {kind: _base_room(data, kind) for kind in KINDS}

    for kind in enabled["primary"]:
        room = Room(**{**bases[kind].__dict__})
        rooms.append(room)
        overlays.extend(_room_overlays(data, kind, "primary", kind))

    strides = pack_wings(crew_list, data.primary_cols, data.view_cols)
    cursor = data.primary_cols
    for i, member in enumerate(crew_list):
        w = strides[i] if i < len(strides) else 15
        bx = cursor
        cursor += w
        left_w = 5 if w <= 12 else 6
        right_w = left_w
        gap = 1
        hall_w = 1 if w <= 12 else 2
        left_x1 = bx + hall_w
        left_x2 = left_x1 + left_w - 1
        right_x1 = left_x2 + 1 + gap
        right_x2 = right_x1 + right_w - 1
        want = list(member.rooms) or ["tor"]
        enabled[member.id] = want
        trunk_x = left_x2 + 1
        local = [
            (bx, 6, max(bx, right_x2), 6),
            (left_x1 + 2, 5, left_x1 + 2, 7),
            (trunk_x, 2, trunk_x, 9),
            (left_x2, 2, trunk_x, 3),
            (left_x2, 8, trunk_x, 9),
        ]
        halls.append((16, 6, bx + hall_w, 6))
        halls.extend(local)
        walk_halls[member.id] = list(local)
        rooms.append(Room(member.id + ":hall", "hall", member.id, "HALL", "", bx, 6, bx + max(0, hall_w - 1), 8))
        spec = data.catalog["rooms"]
        slot: dict[str, Room] = {
            "tor": Room(member.id + ":tor", "tor", member.id, spec["tor"]["title"], spec["tor"]["sn"], left_x1, 0, left_x2, 5),
            "lymph": Room(member.id + ":lymph", "lymph", member.id, spec["lymph"]["title"], spec["lymph"]["sn"], left_x1, 7, left_x2, 11),
            "recipe": Room(member.id + ":recipe", "recipe", member.id, spec["recipe"]["title"], spec["recipe"]["sn"], right_x1, 0, right_x2, 5),
            "poe": Room(member.id + ":poe", "poe", member.id, spec["poe"]["title"], spec["poe"]["sn"], right_x1, 7, right_x2, 11),
        }
        if w >= 22 and "bed" in want:
            slot["bed"] = Room(member.id + ":bed", "bed", member.id, spec["bed"]["title"], spec["bed"]["sn"], right_x2 + 2, 4, right_x2 + 7, 11)
            bed_hall = (right_x2, 6, right_x2 + 2, 6)
            halls.append(bed_hall)
            walk_halls[member.id].append(bed_hall)
        elif "bed" in want and "lymph" not in want:
            slot["bed"] = Room(member.id + ":bed", "bed", member.id, spec["bed"]["title"], spec["bed"]["sn"], left_x1, 4, left_x2, 11)
        for kind in want:
            dest = slot.get(kind)
            if not dest:
                continue
            rooms.append(dest)
            src = bases[kind]
            for ov in _room_overlays(data, kind, member.id, dest.id):
                overlays.append(_shift_prop(ov, src, dest))

    st_cols = max(data.primary_cols, cursor)
    ox = max(1, (data.view_cols - st_cols) // 2)
    if ox + st_cols > data.view_cols:
        ox = max(1, data.view_cols - st_cols)

    paint = [r for r in rooms if r.kind == "hall" or r.kind in enabled.get(r.owner, [])]
    walk: dict[str, set[str]] = {}
    for owner, kinds in enabled.items():
        cells: set[str] = set()
        for room in rooms:
            if room.owner != owner:
                continue
            if room.kind != "hall" and room.kind not in kinds:
                continue
            for y in range(room.y1, room.y2 + 1):
                for x in range(room.x1, room.x2 + 1):
                    cells.add(f"{x},{y}")
        for x1, y1, x2, y2 in walk_halls.get(owner, []):
            for y in range(y1, y2 + 1):
                for x in range(x1, x2 + 1):
                    cells.add(f"{x},{y}")
        walk[owner] = cells

    labels, banners = _place_labels(paint, ox, data.origin_y, data.tile, crew_list, data)
    return Deck(
        rooms=paint,
        halls=halls,
        overlays=overlays,
        ox=ox,
        oy=data.origin_y,
        st_cols=st_cols,
        st_rows=data.st_rows,
        view_cols=data.view_cols,
        view_rows=data.view_rows,
        tile=data.tile,
        dump_t=data.dump_t,
        labels=labels,
        banners=banners,
        walk=walk,
        enabled=enabled,
    )


def in_deck(deck: Deck, x: int, y: int) -> bool:
    if x < 0 or y < 0 or x >= deck.st_cols or y >= deck.st_rows:
        return False
    return any(r.contains(x, y) for r in deck.rooms) or any(
        h[0] <= x <= h[2] and h[1] <= y <= h[3] for h in deck.halls
    )


def rects_overlap(a: tuple[float, float, float, float], b: tuple[float, float, float, float], pad: float = 0) -> bool:
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return ax < bx + bw - pad and ax + aw - pad > bx and ay < by + bh - pad and ay + ah - pad > by


def label_hits_room(deck: Deck, label: Label) -> list[Room]:
    box = (label.x, label.y, label.w, label.h)
    hits = []
    for room in deck.rooms:
        if room.kind == "hall":
            continue
        px, py, pw, ph = room.pixels(deck.ox, deck.oy, deck.tile)
        if rects_overlap(box, (px, py, pw, ph), pad=0.5):
            hits.append(room)
    return hits
