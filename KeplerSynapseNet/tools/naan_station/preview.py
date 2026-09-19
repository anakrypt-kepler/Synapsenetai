#!/usr/bin/env python3
# Clickable NAAN station inspector.
#   python3 KeplerSynapseNet/tools/naan_station/preview.py
# Keys: click a tile. 1 primary-only, 2 screenshot crew, s save PNG, q quit.
from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from PIL import Image, ImageDraw, ImageFont

from deck import (
    CrewMember,
    Deck,
    StationData,
    build_deck,
    in_deck,
    label_hits_room,
    load_station_data,
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


def _stamp_floor(canvas: Image.Image, atlas: Image.Image | None, dx: int, dy: int, tx: int, ty: int, tile: int, scale: int):
    box = (dx, dy, dx + tile * scale, dy + tile * scale)
    if atlas is None:
        color = (46, 49, 54) if (tx + ty) % 2 == 0 else (40, 43, 48)
        ImageDraw.Draw(canvas).rectangle(box, fill=color)
        return
    period = 8
    cell_w = atlas.width // period
    cell_h = atlas.height // period
    sx = (tx % period) * cell_w
    sy = (ty % period) * cell_h
    crop = atlas.crop((sx, sy, sx + cell_w, sy + cell_h)).resize((tile * scale, tile * scale), Image.NEAREST)
    canvas.paste(crop, (dx, dy))


def render_deck(deck: Deck, data: StationData, crew: list[CrewMember], scale: int = 2) -> Image.Image:
    tw = deck.view_cols * deck.tile * scale
    th = deck.view_rows * deck.tile * scale
    canvas = Image.new("RGBA", (tw, th), VOID + (255,))
    draw = ImageDraw.Draw(canvas)
    floor_id = data.catalog.get("defaults", {}).get("floor", "grate")
    floor_file = next((t["file"] for t in data.catalog.get("floors", []) if t["id"] == floor_id), "grate.png")
    atlas = _load(data.public / "floors" / floor_file)
    tile = deck.tile
    ox, oy = deck.ox, deck.oy

    for y in range(deck.st_rows):
        for x in range(deck.st_cols):
            if not in_deck(deck, x, y):
                continue
            dx = (ox + x) * tile * scale
            dy = (oy + y) * tile * scale
            _stamp_floor(canvas, atlas, dx, dy, x, y, tile, scale)
            draw.rectangle((dx, dy, dx + tile * scale, dy + tile * scale), outline=(20, 20, 24))

    for ov in deck.overlays:
        src = _load(data.public / "props" / ov.file)
        u = tile / deck.dump_t
        left = int(((ox + ov.tx) * tile + ov.bx * u) * scale)
        top = int(((oy + ov.ty) * tile + ov.by * u) * scale)
        w = max(1, int(ov.bw * u * scale))
        h = max(1, int(ov.bh * u * scale))
        if src is None:
            draw.rectangle((left, top, left + w, top + h), outline=(0, 229, 255))
            continue
        sprite = src.resize((w, h), Image.NEAREST)
        canvas.alpha_composite(sprite, (max(0, left), max(0, top)))

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
    parser.add_argument("--shot", metavar="PNG", help="Write a PNG and exit.")
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
