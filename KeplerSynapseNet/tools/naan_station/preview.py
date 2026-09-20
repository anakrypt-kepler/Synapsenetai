#!/usr/bin/env python3
# Clickable NAAN station inspector.
#   python3 KeplerSynapseNet/tools/naan_station/preview.py
# Keys: click a tile. 1 primary-only, 2 screenshot crew, s save PNG, q quit.
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from PIL import Image, ImageChops, ImageDraw, ImageFont

from deck import (
    HULL_RIM,
    HULL_SKIRT,
    CrewMember,
    Deck,
    StationData,
    build_deck,
    clip_prop_to_room,
    clip_rect,
    crop_offsets,
    dilated_hull_rects,
    in_corridor,
    in_deck,
    is_wall_row,
    label_hits_room,
    load_station_data,
    prop_sprite_rect,
    screenshot_crew,
)

ACCENT = (255, 77, 13)
VOID = (6, 8, 14)
INK = (244, 244, 245)
MUTED = (161, 161, 166)
HIT = (255, 70, 70)


def _font(size: int):
    for path in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    ):
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def _load(path: Path) -> Image.Image | None:
    try:
        return Image.open(path).convert("RGBA")
    except OSError:
        return None


def _skin_file(data: StationData, group: str, default_key: str) -> str | None:
    tid = data.catalog.get("defaults", {}).get(default_key)
    for tile in data.catalog.get(group, []):
        if tile.get("id") == tid:
            return tile.get("file")
    return None


def _paste_safe(canvas: Image.Image, sprite: Image.Image, x: int, y: int) -> None:
    if sprite.mode != "RGBA":
        sprite = sprite.convert("RGBA")
    dest = (float(x), float(y), float(sprite.width), float(sprite.height))
    hit = clip_rect(dest, (0.0, 0.0, float(canvas.width), float(canvas.height)))
    if hit is None:
        return
    sx, sy, sw, sh = crop_offsets(dest, hit)
    sx_i = max(0, int(round(sx)))
    sy_i = max(0, int(round(sy)))
    sw_i = min(max(1, int(round(sw))), sprite.width - sx_i)
    sh_i = min(max(1, int(round(sh))), sprite.height - sy_i)
    if sw_i <= 0 or sh_i <= 0:
        return
    canvas.alpha_composite(
        sprite.crop((sx_i, sy_i, sx_i + sw_i, sy_i + sh_i)),
        (int(round(hit[0])), int(round(hit[1]))),
    )


def _stamp_floor(canvas: Image.Image, atlas: Image.Image | None, dx: int, dy: int, tx: int, ty: int, tile: int, scale: int):
    w = tile * scale
    if atlas is None:
        color = (46, 49, 54, 255) if (tx + ty) % 2 == 0 else (40, 43, 48, 255)
        ImageDraw.Draw(canvas).rectangle((dx, dy, dx + w, dy + w), fill=color)
        return
    period = 8
    cell_w = atlas.width // period
    cell_h = atlas.height // period
    sx = (tx % period) * cell_w
    sy = (ty % period) * cell_h
    crop = atlas.crop((sx, sy, sx + cell_w, sy + cell_h)).resize((w, w), Image.NEAREST)
    _paste_safe(canvas, crop, dx, dy)


def _stamp_strip(canvas: Image.Image, atlas: Image.Image | None, dx: int, dy: int, w: int, h: int, tx: int) -> None:
    w = max(1, int(w))
    h = max(1, int(h))
    if atlas is None:
        ImageDraw.Draw(canvas).rectangle((dx, dy, dx + w, dy + h), fill=(74, 69, 64, 255))
        return
    period = 4
    cell_w = max(1, atlas.width // period)
    sx = (int(tx) % period) * cell_w
    crop = atlas.crop((sx, 0, sx + cell_w, atlas.height)).resize((w, h), Image.NEAREST)
    _paste_safe(canvas, crop, dx, dy)


def _apply_hull_clip(layer: Image.Image, deck: Deck, scale: int) -> Image.Image:
    # Same dest-in dilated hull as NaanStation.svelte paintDeck.
    mask = Image.new("L", layer.size, 0)
    draw = ImageDraw.Draw(mask)
    for x0, y0, x1, y1 in dilated_hull_rects(deck, scale):
        draw.rectangle((x0, y0, x1 - 1, y1 - 1), fill=255)
    r, g, b, a = layer.split()
    return Image.merge("RGBA", (r, g, b, ImageChops.multiply(a, mask)))


def _blit_prop(
    canvas: Image.Image,
    src: Image.Image | None,
    dest: tuple[float, float, float, float],
    clip: tuple[float, float, float, float],
) -> None:
    hit = clip_rect(dest, clip)
    if hit is None:
        return
    hit = clip_rect(hit, (0.0, 0.0, float(canvas.width), float(canvas.height)))
    if hit is None:
        return
    # Round the blit inward so nearest-neighbor scale cannot paint past the clip.
    hx, hy, hw, hh = hit
    x0 = math.ceil(hx - 1e-9)
    y0 = math.ceil(hy - 1e-9)
    x1 = math.floor(hx + hw + 1e-9)
    y1 = math.floor(hy + hh + 1e-9)
    if x1 <= x0 or y1 <= y0:
        return
    hit = (float(x0), float(y0), float(x1 - x0), float(y1 - y0))
    _dx, _dy, dw, dh = dest
    w = max(1, int(round(dw)))
    h = max(1, int(round(dh)))
    if src is None:
        px, py, pw, ph = hit
        ImageDraw.Draw(canvas).rectangle((px, py, px + pw, py + ph), outline=(0, 229, 255, 255))
        return
    sprite = src.resize((w, h), Image.NEAREST)
    sx, sy, sw, sh = crop_offsets(dest, hit)
    sx_i = max(0, min(sprite.width - 1, int(round(sx))))
    sy_i = max(0, min(sprite.height - 1, int(round(sy))))
    sw_i = min(max(1, int(round(sw))), sprite.width - sx_i)
    sh_i = min(max(1, int(round(sh))), sprite.height - sy_i)
    if sw_i <= 0 or sh_i <= 0:
        return
    canvas.alpha_composite(
        sprite.crop((sx_i, sy_i, sx_i + sw_i, sy_i + sh_i)),
        (int(round(hit[0])), int(round(hit[1]))),
    )


def render_deck(deck: Deck, data: StationData, crew: list[CrewMember], scale: int = 2) -> Image.Image:
    tw = deck.view_cols * deck.tile * scale
    th = deck.view_rows * deck.tile * scale
    canvas = Image.new("RGBA", (tw, th), VOID + (255,))
    hull = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    draw_hull = ImageDraw.Draw(hull)
    floor_file = _skin_file(data, "floors", "floor") or "grate.png"
    wall_file = _skin_file(data, "walls", "wall")
    shell_file = _skin_file(data, "shells", "shell")
    atlas = _load(data.public / "floors" / floor_file)
    wall = _load(data.public / "walls" / wall_file) if wall_file else None
    shell = _load(data.public / "shells" / shell_file) if shell_file else None
    tile = deck.tile
    ts = tile * scale
    ox, oy = deck.ox, deck.oy
    rim = HULL_RIM * scale
    skirt = HULL_SKIRT * scale

    for y in range(deck.st_rows):
        for x in range(deck.st_cols):
            if not in_deck(deck, x, y):
                continue
            dx = (ox + x) * ts
            dy = (oy + y) * ts
            _stamp_floor(hull, atlas, dx, dy, x, y, tile, scale)
            draw_hull.rectangle((dx, dy, dx + ts - 1, dy + ts - 1), outline=(20, 20, 24, 80))
            if is_wall_row(deck, x, y):
                _stamp_strip(hull, wall, dx, dy, ts, max(1, ts // 2), x)
            if in_corridor(deck, x, y):
                overlay = Image.new("RGBA", (ts, ts), (0, 0, 0, 30))
                _paste_safe(hull, overlay, dx, dy)
            # Shell rim is intentional hull trim; dest-in below keeps the dilated hull.
            if not in_deck(deck, x - 1, y):
                _stamp_strip(hull, shell, dx - rim, dy, rim, ts, x)
            if not in_deck(deck, x + 1, y):
                _stamp_strip(hull, shell, dx + ts, dy, rim, ts, x)
            if not in_deck(deck, x, y - 1):
                _stamp_strip(hull, shell, dx - rim, dy - rim, ts + rim * 2, rim, x)
            if not in_deck(deck, x, y + 1):
                _stamp_strip(hull, shell, dx - rim, dy + ts, ts + rim * 2, rim + skirt, x)

    canvas.alpha_composite(_apply_hull_clip(hull, deck, scale))

    rooms = {r.id: r for r in deck.rooms}
    for ov in deck.overlays:
        room = rooms.get(ov.room)
        if room is None:
            continue
        src = _load(data.public / "props" / ov.file)
        raw = prop_sprite_rect(ov, ox, oy, tile, deck.dump_t)
        dest = (raw[0] * scale, raw[1] * scale, raw[2] * scale, raw[3] * scale)
        clipped = clip_prop_to_room(ov, room, ox, oy, tile, deck.dump_t)
        if clipped is None:
            continue
        clip = (clipped[0] * scale, clipped[1] * scale, clipped[2] * scale, clipped[3] * scale)
        _blit_prop(canvas, src, dest, clip)

    draw = ImageDraw.Draw(canvas)
    small = _font(max(9, 5 * scale))
    for room in deck.rooms:
        if room.kind == "hall":
            continue
        px, py, pw, ph = room.pixels(ox, oy, tile)
        draw.rectangle(
            (px * scale, py * scale, (px + pw) * scale, (py + ph) * scale),
            outline=ACCENT if room.owner == "primary" else MUTED,
        )

    for label in deck.banners + deck.labels:
        box = [int(label.x * scale), int(label.y * scale), int((label.x + label.w) * scale), int((label.y + label.h) * scale)]
        bad = label_hits_room(deck, label)
        fill = (40, 8, 8, 220) if bad else (6, 5, 4, 210)
        draw.rectangle(box, fill=fill, outline=HIT if bad else (185, 121, 28))
        draw.text((box[0] + 3, box[1] + 1), label.text, font=small, fill=ACCENT if label.kind == "banner" else INK)

    info = f"cols {deck.st_cols}/{deck.view_cols}  rooms {len([r for r in deck.rooms if r.kind != 'hall'])}  crew {len(crew)}"
    draw.text((8 * scale, 6 * scale), info, font=_font(max(10, 6 * scale)), fill=MUTED)
    draw.text((8 * scale, 18 * scale), "click a tile · 1 primary · 2 screenshot crew · s save · q quit", font=small, fill=MUTED)
    return canvas.convert("RGB")


def _tile_at(deck: Deck, mx: float, my: float, scale: int) -> tuple[int, int]:
    x = mx / (deck.tile * scale) - deck.ox
    y = my / (deck.tile * scale) - deck.oy
    return int(x), int(y)


def describe(deck: Deck, data: StationData, tx: int, ty: int) -> str:
    lines = [f"tile {tx},{ty}  in_deck={in_deck(deck, tx, ty)}"]
    for room in deck.rooms:
        if room.contains(tx, ty):
            lines.append(f"room {room.id}  {room.owner}/{room.kind}  {room.name} {room.sn}  {room.w}x{room.h}")
    for ov in deck.overlays:
        if ov.tx <= tx < ov.tx + ov.w and ov.ty <= ty < ov.ty + ov.h:
            lines.append(f"prop {ov.file}  role {ov.role}  at {ov.tx},{ov.ty}  {ov.w}x{ov.h}")
    hits = []
    for label in deck.labels + deck.banners:
        rooms = label_hits_room(deck, label)
        if rooms:
            hits.append(f"LABEL HIT {label.text!r} -> " + ",".join(r.id for r in rooms))
    if hits:
        lines.extend(hits)
    else:
        lines.append("labels clear of room interiors")
    return "\n".join(lines)


def run_window(initial: str) -> None:
    import tkinter as tk
    from PIL import ImageTk

    data = load_station_data()
    state = {"crew": screenshot_crew() if initial == "screenshot" else (["bed", "tor", "lymph", "recipe", "poe"], [])}

    def deck_now() -> Deck:
        rooms, crew = state["crew"]
        return build_deck(rooms or None, crew, data)

    root = tk.Tk()
    root.title("NAAN station inspector")
    root.configure(bg="#0b0b0c")
    scale = 2
    photo_holder = {"im": None}

    canvas = tk.Canvas(root, width=1100, height=620, bg="#0b0b0c", highlightthickness=0)
    canvas.pack(fill="both", expand=True)
    status = tk.Text(root, height=8, bg="#111113", fg="#d4d4d8", insertbackground="#fff", font=("monospace", 10))
    status.pack(fill="x")

    def paint(msg: str = ""):
        deck = deck_now()
        image = render_deck(deck, data, state["crew"][1], scale)
        photo = ImageTk.PhotoImage(image)
        photo_holder["im"] = photo
        canvas.delete("all")
        canvas.config(scrollregion=(0, 0, image.width, image.height), width=min(1100, image.width), height=min(620, image.height))
        canvas.create_image(0, 0, anchor="nw", image=photo)
        status.delete("1.0", "end")
        if msg:
            status.insert("end", msg + "\n\n")
        status.insert("end", describe(deck, data, 13, 2))

    def on_click(ev):
        deck = deck_now()
        tx, ty = _tile_at(deck, canvas.canvasx(ev.x), canvas.canvasy(ev.y), scale)
        status.delete("1.0", "end")
        status.insert("end", describe(deck, data, tx, ty))

    def on_key(ev):
        key = ev.keysym.lower()
        if key in ("q", "escape"):
            root.destroy()
            return
        if key == "1":
            state["crew"] = (["bed", "tor", "lymph", "recipe", "poe"], [])
            paint("primary hull only")
        elif key == "2":
            state["crew"] = screenshot_crew()
            paint("screenshot crew: Rick + Alien")
        elif key == "s":
            out = Path("/tmp/naan_station_inspect.png")
            render_deck(deck_now(), data, state["crew"][1], scale).save(out)
            paint(f"saved {out}")

    canvas.bind("<Button-1>", on_click)
    root.bind("<Key>", on_key)
    paint("click tiles to inspect props, rooms, and label hits")
    root.mainloop()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Inspect NAAN station rooms, props, and labels.")
    parser.add_argument("--png", "--shot", dest="shot", metavar="PNG", help="Write a PNG and exit.")
    parser.add_argument("--crew", choices=("none", "screenshot"), default="screenshot")
    parser.add_argument("--scale", type=int, default=2)
    args = parser.parse_args(argv)
    data = load_station_data()
    primary, crew = screenshot_crew() if args.crew == "screenshot" else (["bed", "tor", "lymph", "recipe", "poe"], [])
    if args.crew == "none":
        crew = []
    deck = build_deck(primary, crew, data)
    image = render_deck(deck, data, crew, args.scale)
    if args.shot:
        Path(args.shot).parent.mkdir(parents=True, exist_ok=True)
        image.save(args.shot)
        print(f"wrote {args.shot}  {image.size[0]}x{image.size[1]}")
        print(describe(deck, data, 13, 2))
        return 0
    run_window(args.crew)
    return 0


if __name__ == "__main__":
    sys.exit(main())
