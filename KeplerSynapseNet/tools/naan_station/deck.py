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
# Frozen campus constants. Same numbers as stationLayout.json (Svelte agent).
SUITE_W = 25
SUITE_H = 12
SPINE = 3
HOLD_KIND = "hold"
# Matches NaanStation.svelte paintDeck rim / dest-in hull.
HULL_RIM = 11
HULL_SKIRT = 10


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
    suite_w: int
    suite_h: int
    spine: int


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
        primary_cols=int(layout.get("gridCols", SUITE_W)),
        st_rows=int(layout.get("gridRows", SUITE_H)),
        view_cols=int(layout.get("viewCols", 80)),
        view_rows=int(layout.get("viewRows", 48)),
        origin_y=int(layout.get("originY", 10)),
        suite_w=int(layout.get("suiteW", SUITE_W)),
        suite_h=int(layout.get("suiteH", SUITE_H)),
        spine=int(layout.get("spine", SPINE)),
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
    # Leftover helper. build_deck uses campus slots, not these 15/22 strides.
    members = list(crew)
    n = len(members)
    if not n:
        return []
    prefer = [22 if "bed" in c.rooms else 15 for c in members]
    budget = view_cols - 2 - primary_cols
    if sum(prefer) <= budget:
        return prefer
    out = list(prefer)
    shaved = True
    while sum(out) > budget and shaved:
        shaved = False
        for i, c in enumerate(members):
            if "bed" in c.rooms and out[i] > 15:
                out[i] -= 1
                shaved = True
                if sum(out) <= budget:
                    return out
    if primary_cols + 15 * n <= view_cols - 2:
        return [15] * n
    return [12] * n


def campus_cols_per_row(view_cols: int, suite_w: int = SUITE_W, spine: int = SPINE) -> int:
    cell_w = suite_w + spine
    # 80 -> 2 columns: primary | extra0 on row 0; extra1 wraps under primary
    return 1 + (view_cols - 2 - suite_w) // cell_w


def campus_origin(
    slot: int,
    view_cols: int,
    suite_w: int = SUITE_W,
    suite_h: int = SUITE_H,
    spine: int = SPINE,
) -> tuple[int, int]:
    # Extra crew i = 0..n-1 occupy campus slots. slot 0 is primary.
    cell_w = suite_w + spine  # 28
    cell_h = suite_h + spine  # 15
    cols_per_row = campus_cols_per_row(view_cols, suite_w, spine)
    col = slot % cols_per_row
    row = slot // cols_per_row
    return (col * cell_w, row * cell_h)


def _suite_col_x(bx: int) -> tuple[tuple[int, int], tuple[int, int], tuple[int, int]]:
    # west pad 2, COL_W 7, GAP 2
    # col0: bx+2 .. bx+8
    # col1: bx+10 .. bx+16
    # col2: bx+18 .. bx+24
    return ((bx + 2, bx + 8), (bx + 10, bx + 16), (bx + 18, bx + 24))


def _paints(room: Room, enabled: dict[str, list[str]]) -> bool:
    # Paint filter: hall OR hold OR kind in enabled[owner].
    if room.kind == "hall" or room.kind == HOLD_KIND:
        return True
    return room.kind in enabled.get(room.owner, [])


def _walks(room: Room, kinds: list[str]) -> bool:
    # HOLD tiles are walkable for that owner.
    if room.kind == "hall" or room.kind == HOLD_KIND:
        return True
    return room.kind in kinds


def _deck_extent(rooms: list[Room], halls: list[tuple[int, int, int, int]]) -> tuple[int, int]:
    max_x = 0
    max_y = 0
    for r in rooms:
        max_x = max(max_x, r.x2)
        max_y = max(max_y, r.y2)
    for x1, y1, x2, y2 in halls:
        max_x = max(max_x, x1, x2)
        max_y = max(max_y, y1, y2)
    return max_x + 1, max_y + 1


def _cover_suite(
    halls: list[tuple[int, int, int, int]],
    local: list[tuple[int, int, int, int]],
    bx: int,
    by: int,
    suite_w: int,
    suite_h: int,
) -> None:
    # The 25x12 suite rectangle must be 100% in_deck. Fill unused cells with
    # halls (plaza + the GAP columns + west pad + y=6 spine). No Swiss cheese.
    cover = (bx, by, bx + suite_w - 1, by + suite_h - 1)
    halls.append(cover)
    local.append(cover)
    west = (bx, by, bx + 1, by + suite_h - 1)
    gap0 = (bx + 9, by, bx + 9, by + suite_h - 1)
    gap1 = (bx + 17, by, bx + 17, by + suite_h - 1)
    spine_y = (bx, by + 6, bx + suite_w - 1, by + 6)
    for rect in (west, gap0, gap1, spine_y):
        halls.append(rect)
        local.append(rect)


def _plaza(
    halls: list[tuple[int, int, int, int]],
    local: list[tuple[int, int, int, int]],
    x1: int,
    y1: int,
    x2: int,
    y2: int,
) -> None:
    rect = (x1, y1, x2, y2)
    halls.append(rect)
    local.append(rect)


def clamp_overlay_to_room(ov: Overlay, room: Room) -> Overlay:
    max_x = max(room.x1, room.x2 - max(0, ov.w - 1))
    max_y = max(room.y1, room.y2 - max(0, ov.h - 1))
    tx = min(max(room.x1, ov.tx), max_x)
    ty = min(max(room.y1, ov.ty), max_y)
    if tx == ov.tx and ty == ov.ty and ov.room == room.id and ov.owner == room.owner:
        return ov
    return Overlay(
        room.id, ov.kind, room.owner, ov.file, tx, ty, ov.role,
        ov.w, ov.h, ov.bx, ov.by, ov.bw, ov.bh,
    )


def _shift_prop(ov: Overlay, src: Room, dest: Room) -> Overlay:
    dx = dest.x1 - src.x1
    dy = dest.y1 - src.y1
    shifted = Overlay(
        dest.id, dest.kind, dest.owner, ov.file,
        ov.tx + dx, ov.ty + dy, ov.role,
        ov.w, ov.h, ov.bx, ov.by, ov.bw, ov.bh,
    )
    return clamp_overlay_to_room(shifted, dest)


def _room_overlays(data: StationData, kind: str, owner: str, room_id: str) -> list[Overlay]:
    out: list[Overlay] = []
    lr = data.layout["rooms"][kind]
    bounds = Room(room_id, kind, owner, "", "", lr["x1"], lr["y1"], lr["x2"], lr["y2"])
    for prop in lr["props"]:
        file = resolve_prop(data, kind, prop["file"])
        if not file:
            continue
        view = view_of(data, file)
        ov = Overlay(
            room_id, kind, owner, file,
            int(prop["tx"]), int(prop["ty"]),
            prop.get("role") or "dress",
            int(view["w"]), int(view["h"]),
            float(view["bx"]), float(view["by"]),
            float(view["bw"]), float(view["bh"]),
        )
        out.append(clamp_overlay_to_room(ov, bounds))
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
        # Row 0 stacks the banner above room labels (-40). Wrapped rows only
        # have a 3-tile spine; a -40 offset lands on the suite above.
        clearance = 40 if y1 <= 2 else 8
        banners.append(Label(
            "banner", owner, text,
            (ox + x1) * tile + 2,
            (oy + y1) * tile - h - clearance,
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

    hold_src = _base_room(data, HOLD_KIND) if HOLD_KIND in data.layout.get("rooms", {}) else None
    spec = data.catalog["rooms"]
    suite_w = data.suite_w
    suite_h = data.suite_h
    spine = data.spine
    cols_per_row = campus_cols_per_row(data.view_cols, suite_w, spine)
    origins: dict[int, tuple[int, int]] = {0: (0, 0)}

    for i, member in enumerate(crew_list):
        # For extra index i: slot = i+1 (slot 0 is primary)
        slot = i + 1
        bx, by = campus_origin(slot, data.view_cols, suite_w, suite_h, spine)
        origins[slot] = (bx, by)
        want = [k for k in (member.rooms or ("tor",)) if k in KINDS]
        enabled[member.id] = want
        local: list[tuple[int, int, int, int]] = []
        _cover_suite(halls, local, bx, by, suite_w, suite_h)
        rooms.append(Room(
            member.id + ":hall", "hall", member.id, "HALL", "",
            bx, by + 6, bx + suite_w - 1, by + 6,
        ))
        col_x = _suite_col_x(bx)
        # north y: by+0 .. by+5 ; south y: by+7 .. by+11
        north_plan = ("tor", "recipe", "poe")
        south_plan = ("lymph", HOLD_KIND, "bed")
        for col_i, kind in enumerate(north_plan):
            x1, x2 = col_x[col_i]
            y1, y2 = by, by + 5
            if kind in want:
                dest = Room(
                    member.id + ":" + kind, kind, member.id,
                    spec[kind]["title"], spec[kind]["sn"],
                    x1, y1, x2, y2,
                )
                rooms.append(dest)
                for ov in _room_overlays(data, kind, member.id, dest.id):
                    overlays.append(_shift_prop(ov, bases[kind], dest))
            else:
                _plaza(halls, local, x1, y1, x2, y2)
        for col_i, kind in enumerate(south_plan):
            x1, x2 = col_x[col_i]
            y1, y2 = by + 7, by + 11
            place = kind == HOLD_KIND or kind in want
            if place:
                dest = Room(
                    member.id + ":" + kind, kind, member.id,
                    spec[kind]["title"], spec[kind]["sn"],
                    x1, y1, x2, y2,
                )
                rooms.append(dest)
                src = hold_src if kind == HOLD_KIND else bases[kind]
                if src is not None:
                    for ov in _room_overlays(data, kind, member.id, dest.id):
                        overlays.append(_shift_prop(ov, src, dest))
            else:
                _plaza(halls, local, x1, y1, x2, y2)
        walk_halls[member.id] = list(local)

    # Spines: only 3-tile connectors, NEVER a hall from x=16 across empty space.
    occupied = set(origins)
    for slot in sorted(occupied):
        bx, by = origins[slot]
        row = slot // cols_per_row
        right = slot + 1
        if right in occupied and right // cols_per_row == row:
            # Horizontally adjacent occupied slots: 3-wide connector on hall y
            # (by+5 .. by+7) between the two 25-col suites.
            halls.append((bx + suite_w, by + 5, bx + suite_w + spine - 1, by + 7))
        down = slot + cols_per_row
        if down in occupied:
            # Vertically adjacent occupied slots in the same column: 3-tall
            # connector around x = bx+7..bx+9 (aligns with primary hall x=8).
            halls.append((bx + 7, by + suite_h, bx + 9, by + suite_h + spine - 1))

    st_cols, st_rows = _deck_extent(rooms, halls)
    st_cols = max(data.primary_cols, st_cols)
    ox = max(1, (data.view_cols - st_cols) // 2)
    if ox + st_cols > data.view_cols:
        ox = max(1, data.view_cols - st_cols)

    paint = [r for r in rooms if _paints(r, enabled)]
    walk: dict[str, set[str]] = {}
    for owner, kinds in enabled.items():
        cells: set[str] = set()
        for room in rooms:
            if room.owner != owner:
                continue
            if not _walks(room, kinds):
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
        st_rows=st_rows,
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


def is_wall_row(deck: Deck, x: int, y: int) -> bool:
    for room in deck.rooms:
        if room.kind == "hall":
            continue
        if room.x1 <= x <= room.x2 and y == room.y1:
            return True
    return False


def in_corridor(deck: Deck, x: int, y: int) -> bool:
    if any(r.kind != "hall" and r.contains(x, y) for r in deck.rooms):
        return False
    return any(h[0] <= x <= h[2] and h[1] <= y <= h[3] for h in deck.halls)


def prop_sprite_rect(
    ov: Overlay, ox: int, oy: int, tile: int, dump_t: int
) -> tuple[float, float, float, float]:
    u = tile / float(dump_t)
    left = (ox + ov.tx) * tile + ov.bx * u
    top = (oy + ov.ty) * tile + ov.by * u
    return left, top, ov.bw * u, ov.bh * u


def clip_rect(
    src: tuple[float, float, float, float],
    clip: tuple[float, float, float, float],
) -> tuple[float, float, float, float] | None:
    sx, sy, sw, sh = src
    cx, cy, cw, ch = clip
    left = max(sx, cx)
    top = max(sy, cy)
    right = min(sx + sw, cx + cw)
    bottom = min(sy + sh, cy + ch)
    if right <= left or bottom <= top:
        return None
    return (left, top, right - left, bottom - top)


def room_clip_rect(room: Room, ox: int, oy: int, tile: int) -> tuple[float, float, float, float]:
    # Same box as NaanStation.svelte pctPropClip: owning room tiles, no hull rim.
    # Isometric dump by is often negative; overflow is cut at this north wall.
    px, py, pw, ph = room.pixels(ox, oy, tile)
    return (float(px), float(py), float(pw), float(ph))


def room_hull_rect(room: Room, ox: int, oy: int, tile: int) -> tuple[float, float, float, float]:
    # Dilated hull of one room. Props may sit on the wall rim but not in the void.
    px, py, pw, ph = room.pixels(ox, oy, tile)
    return (
        float(px - HULL_RIM),
        float(py - HULL_RIM),
        float(pw + HULL_RIM * 2),
        float(ph + HULL_RIM + HULL_SKIRT),
    )


def clip_prop_to_room(
    ov: Overlay,
    room: Room,
    ox: int,
    oy: int,
    tile: int,
    dump_t: int,
) -> tuple[float, float, float, float] | None:
    # Tight clip: dump sprite intersect the room tiles, then the owning hull.
    # Negative by cannot paint into a neighbor or the void past the hull rim.
    sprite = prop_sprite_rect(ov, ox, oy, tile, dump_t)
    hit = clip_rect(sprite, room_clip_rect(room, ox, oy, tile))
    if hit is None:
        return None
    return clip_rect(hit, room_hull_rect(room, ox, oy, tile))


def crop_offsets(
    dest: tuple[float, float, float, float],
    clipped: tuple[float, float, float, float],
) -> tuple[float, float, float, float]:
    dx, dy, _dw, _dh = dest
    cx, cy, cw, ch = clipped
    return (cx - dx, cy - dy, cw, ch)


def dilated_hull_rects(deck: Deck, scale: int = 1) -> list[tuple[int, int, int, int]]:
    tile = deck.tile * scale
    rim = HULL_RIM * scale
    skirt = HULL_SKIRT * scale
    ox, oy = deck.ox, deck.oy
    out: list[tuple[int, int, int, int]] = []
    for y in range(deck.st_rows):
        for x in range(deck.st_cols):
            if not in_deck(deck, x, y):
                continue
            dx = (ox + x) * tile
            dy = (oy + y) * tile
            # Path2D.rect(dx-rim, dy-rim, TILE+2*rim, TILE+rim+skirt)
            out.append((dx - rim, dy - rim, dx + tile + rim, dy + tile + skirt))
    return out


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
