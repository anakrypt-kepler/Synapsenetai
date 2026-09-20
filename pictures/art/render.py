#!/usr/bin/env python3
"""Render SynapseNet README graphics in the same language as Kepler's GitHub profile."""

from __future__ import annotations

import argparse
import base64
from functools import lru_cache
from html import escape
from io import BytesIO
import math
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[2]
ART = Path(__file__).resolve().parent
PORTRAIT = ROOT / "pictures" / "kepler.jpg"
ACCENT = "#FF4D0D"
THEMES = {
    "dark": {"text": "#F4F4F5", "muted": "#A1A1AA", "dim": "#71717A", "edge": "#24242A", "card": "#101013", "grid": "#151518"},
    "light": {"text": "#18181B", "muted": "#52525B", "dim": "#71717A", "edge": "#E4E4E7", "card": "#FAFAFA", "grid": "#F4F4F5"},
}


class SVG:
    def __init__(self, height, theme, title):
        self.height = height
        self.theme = theme
        self.colors = THEMES[theme]
        self.parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="840" height="{height}" viewBox="0 0 840 {height}" role="img" aria-label="{escape(title)}">',
            f'<title>{escape(title)}</title>',
            "<desc>SynapseNet public graphics. Dark and light variants follow GitHub color-scheme.</desc>",
            "<defs>",
            font_face(),
            f'<clipPath id="frame"><rect width="840" height="{height}" rx="16"/></clipPath>',
            '<filter id="glow"><feGaussianBlur stdDeviation="6" result="blur"/><feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge></filter>',
            "</defs>",
            '<g clip-path="url(#frame)">',
            f'<rect width="840" height="{height}" fill="#0A0A0B"/>' if theme == "dark" else f'<rect width="840" height="{height}" fill="#FFFFFF"/>',
            f'<rect x="1" y="1" width="838" height="{height - 2}" rx="15" fill="none" stroke="{self.colors["edge"]}"/>',
        ]

    def __getattr__(self, name):
        return self.colors[name]

    def raw(self, markup):
        self.parts.append(markup)

    def rect(self, x, y, width, height, fill, rx=0, opacity=None, stroke=None):
        extra = ""
        if opacity is not None:
            extra += f' opacity="{opacity}"'
        if stroke:
            extra += f' stroke="{stroke}"'
        self.raw(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="{rx}" fill="{fill}"{extra}/>')

    def text(self, x, y, value, size, fill="text", font_weight=500, text_anchor="start", opacity=None):
        extra = "" if opacity is None else f' opacity="{opacity}"'
        self.raw(
            f'<text x="{x}" y="{y}" fill="{self.colors.get(fill, fill)}" font-family="JetBrains Mono, ui-monospace, monospace" '
            f'font-size="{size}" font-weight="{font_weight}" text-anchor="{text_anchor}"{extra}>{escape(value)}</text>'
        )

    def section(self, title, subtitle):
        self.rect(0, 0, 840, 44, self.card)
        self.rect(0, 44, 840, 1, self.edge)
        self.rect(0, 0, 6, 44, ACCENT)
        self.text(22, 28, title, 13, "text", font_weight=600)
        self.text(818, 28, subtitle, 11, "muted", text_anchor="end")

    def finish(self):
        self.parts.append("</g></svg>")
        return "\n".join(self.parts) + "\n"


@lru_cache(maxsize=1)
def font_face():
    font = ART / "JetBrainsMono.woff2"
    payload = base64.b64encode(font.read_bytes()).decode()
    return (
        '<style>@font-face { font-family: "JetBrains Mono"; src: url("data:font/woff2;base64,'
        + payload
        + '") format("woff2"); font-weight: 400 700; font-style: normal; font-display: swap; }</style>'
    )


def hexagon(cx, cy, radius):
    return " ".join(
        f"{cx + radius * math.cos(math.radians(angle)):.1f},{cy + radius * math.sin(math.radians(angle)):.1f}"
        for angle in range(0, 360, 60)
    )


def portrait_data():
    with Image.open(PORTRAIT) as original:
        portrait = ImageOps.exif_transpose(original).convert("RGB")
        portrait.thumbnail((600, 600), Image.Resampling.LANCZOS)
        buffer = BytesIO()
        portrait.save(buffer, format="JPEG", quality=95)
    return base64.b64encode(buffer.getvalue()).decode()


def hero(theme):
    svg = SVG(170, theme, "SynapseNet — intelligence belongs to everyone")
    svg.section("SynapseNet", "public cell / alpha")
    svg.text(48, 92, "SynapseNet", 34, "text", font_weight=700)
    svg.text(48, 122, "Intelligence belongs to everyone.", 14, ACCENT)
    svg.text(48, 148, "Local GGUF models. Tor peers. Proof of Emergence. NAAN harvest on a station campus.", 12, "muted")
    svg.raw(f'<g fill="none" stroke="{ACCENT}" stroke-width="1.6" opacity=".55" transform="translate(612 78)">'
            f'<polygon points="{hexagon(78, 42, 36)}"/><polygon points="{hexagon(78, 42, 22)}"/>'
            f'<circle cx="78" cy="42" r="5" fill="{ACCENT}" stroke="none"/>'
            '<animateTransform attributeName="transform" type="rotate" from="0 78 42" to="360 78 42" dur="28s" repeatCount="indefinite"/>'
            "</g>")
    return svg


def tiles(theme):
    svg = SVG(104, theme, "SynapseNet status tiles")
    items = [("STATUS", "ALPHA"), ("MESH", "TOR ONLY"), ("LICENSE", "MIT"), ("ENGINE", "C++ / RUST"), ("SHELL", "TAURI")]
    for index, (label, value) in enumerate(items):
        x = 18 + index * 164
        svg.rect(x, 18, 152, 68, svg.card, rx=10, stroke=svg.edge)
        svg.text(x + 16, 44, label, 10, "dim")
        svg.text(x + 16, 68, value, 16, ACCENT, font_weight=600)
    return svg


def net(theme):
    svg = SVG(430, theme, "SynapseNet map — the cell around YOU")
    svg.section("The map", "the cell / YOU in the center")
    nodes = [
        {"id": "you", "label": "YOU", "x": 420, "y": 228, "kind": "core"},
        {"id": "desktop", "label": "Desktop", "x": 168, "y": 104},
        {"id": "lib", "label": "libsynapsed", "x": 168, "y": 228},
        {"id": "tor", "label": "Tor", "x": 168, "y": 352},
        {"id": "naan", "label": "NAAN", "x": 672, "y": 104},
        {"id": "poe", "label": "PoE", "x": 672, "y": 228},
        {"id": "wallet", "label": "Wallet", "x": 672, "y": 352},
        {"id": "hold", "label": "HOLD", "x": 420, "y": 352},
        {"id": "know", "label": "KNOW", "x": 420, "y": 104},
    ]
    lookup = {node["id"]: node for node in nodes}
    edges = [
        ("you", "desktop"),
        ("you", "lib"),
        ("you", "tor"),
        ("you", "naan"),
        ("you", "poe"),
        ("you", "wallet"),
        ("you", "hold"),
        ("you", "know"),
        ("desktop", "lib"),
        ("lib", "tor"),
        ("naan", "poe"),
        ("poe", "wallet"),
        ("know", "naan"),
        ("hold", "wallet"),
    ]
    for source, target in edges:
        a, b = lookup[source], lookup[target]
        svg.raw(
            f'<line x1="{a["x"]}" y1="{a["y"]}" x2="{b["x"]}" y2="{b["y"]}" stroke="{svg.edge}" stroke-width="1.4"/>'
        )
        svg.raw(
            f'<circle r="3.2" fill="{ACCENT}"><animateMotion dur="7s" repeatCount="indefinite" '
            f'path="M{a["x"]},{a["y"]} L{b["x"]},{b["y"]}"/></circle>'
        )
    for node in nodes:
        if node.get("kind") == "core":
            svg.raw(f'<polygon points="{hexagon(node["x"], node["y"], 34)}" fill="{svg.card}" stroke="{ACCENT}" stroke-width="2" filter="url(#glow)"/>')
            svg.text(node["x"], node["y"] + 5, node["label"], 13, ACCENT, font_weight=700, text_anchor="middle")
            continue
        svg.rect(node["x"] - 54, node["y"] - 16, 108, 32, svg.card, rx=8, stroke=svg.edge)
        svg.text(node["x"], node["y"] + 5, node["label"], 12, "text", font_weight=600, text_anchor="middle")
    svg.text(420, 404, "Desktop talks to libsynapsed. The mesh is Tor. Harvest walks NAAN. Value settles as PoE.", 11, "dim", text_anchor="middle")
    return svg


def portrait(theme):
    svg = SVG(325, theme, "Kepler — independent builder of SynapseNet")
    svg.section("Kepler / SynapseNet", "independent builder")
    svg.rect(22, 58, 206, 250, svg.card, rx=6, stroke=svg.edge)
    svg.raw(f'<image x="25" y="72" width="200" height="220" href="data:image/jpeg;base64,{portrait_data()}" preserveAspectRatio="xMidYMid meet"/>')
    rows = [
        ("SynapseNet", "Decentralized intelligence, built in the open."),
        ("Local AI + NAAN", "Local models and knowledge contributions."),
        ("Tor + Proof of Emergence", "Peer communication and knowledge validation."),
        ("C++ / Rust / Tauri / Svelte", "From the native engine to the desktop cell."),
    ]
    for index, (title, subtitle) in enumerate(rows):
        y = 88 + index * 51
        svg.rect(246, y - 12, 2, 14, ACCENT)
        svg.text(260, y, title, 13, "text", font_weight=600)
        svg.text(260, y + 19, subtitle, 11.5)
    svg.text(260, 298, "Intelligence belongs to everyone. / Alpha development.", 11, "dim")
    return svg


def footer(theme):
    svg = SVG(150, theme, "SynapseNet — local models, shared knowledge, open infrastructure")
    svg.section("Elsewhere", "@anakrypt-kepler")
    for x, width, value in [(140, 160, "SynapseNet"), (314, 196, "GitHub profile"), (524, 176, "Issues + ideas")]:
        svg.rect(x, 59, width, 30, svg.card, rx=15, stroke=svg.edge)
        svg.text(x + width / 2, 79, value, 11, ACCENT, text_anchor="middle")
    svg.text(420, 116, "Local models. Shared knowledge. Open infrastructure.", 11, "dim", text_anchor="middle")
    svg.text(420, 138, "Desktop screenshots are coming next. The map stays here.", 10, "dim", text_anchor="middle")
    return svg


def render():
    if not (ART / "JetBrainsMono.woff2").is_file():
        raise FileNotFoundError("pictures/art/JetBrainsMono.woff2 is required")
    if not PORTRAIT.is_file():
        raise FileNotFoundError("pictures/kepler.jpg is required")
    graphics = {
        "hero": hero,
        "tiles": tiles,
        "map": net,
        "portrait": portrait,
        "footer": footer,
    }
    written = []
    for name, builder in graphics.items():
        for theme in THEMES:
            path = ART / f"{name}-{theme}.svg"
            path.write_text(builder(theme).finish(), encoding="utf-8")
            written.append(path)
    return written


def main():
    parser = argparse.ArgumentParser(description="Generate SynapseNet README SVGs.")
    parser.parse_args()
    for path in render():
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
