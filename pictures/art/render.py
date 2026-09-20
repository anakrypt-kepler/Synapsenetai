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
import re

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[2]
ART = Path(__file__).resolve().parent
PORTRAIT = ROOT / "pictures" / "kepler.jpg"
LOGIN = "anakrypt-kepler"
ACCENT = "#FF4D0D"
MAP_WIDTH = 840
MAP_HEIGHT = 640
THEMES = {
    "dark": {"text": "#F4F4F5", "muted": "#A1A1AA", "dim": "#71717A", "edge": "#24242A", "card": "#101013", "grid": "#151518"},
    "light": {"text": "#18181B", "muted": "#52525B", "dim": "#71717A", "edge": "#E4E4E7", "card": "#FAFAFA", "grid": "#F4F4F5"},
}


class SVG:
    def __init__(self, height, theme, title, width=840, extra_style=""):
        self.height, self.width, self.title = height, width, title
        self.colors = THEMES[theme]
        self.extra_style = extra_style
        self.parts = []

    def raw(self, value):
        self.parts.append(value)

    def rect(self, x, y, width, height, fill, **attrs):
        extra = " ".join(f'{key.rstrip("_").replace("_", "-")}="{escape(str(value), quote=True)}"' for key, value in attrs.items())
        self.raw(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" fill="{fill}" {extra}/>')

    def text(self, x, y, value, size=12, color="muted", **attrs):
        color = self.colors.get(color, color)
        extra = " ".join(f'{key.rstrip("_").replace("_", "-")}="{escape(str(value), quote=True)}"' for key, value in attrs.items())
        self.raw(f'<text x="{x}" y="{y}" fill="{color}" font-size="{size}" {extra}>{escape(str(value))}</text>')

    def section(self, title, note=""):
        self.text(22, 28, title.upper(), 16, "text", font_weight=600, letter_spacing=1.5)
        self.text(self.width - 22, 28, note, 11, "dim", text_anchor="end")
        self.raw(f'<defs><linearGradient id="rule"><stop stop-color="{ACCENT}" stop-opacity=".9"/><stop offset="1" stop-color="{ACCENT}" stop-opacity="0"/></linearGradient></defs>')
        self.rect(0, 40, self.width, 1, "url(#rule)")

    def finish(self):
        title = escape(self.title, quote=True)
        style = font_style() + "text{font-family:'JetBrains Mono',ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;white-space:pre}.reveal{animation:reveal .8s ease both}@keyframes reveal{from{opacity:0}to{opacity:1}}@media(prefers-reduced-motion:reduce){.motion{display:none}.reveal{animation:none!important}}" + self.extra_style
        return (
            f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{self.width}" height="{self.height}" viewBox="0 0 {self.width} {self.height}" role="img" aria-label="{title}">\n'
            f"<title>{title}</title>\n<style>{style}</style>\n" + "\n".join(self.parts) + "\n</svg>\n"
        )


@lru_cache(maxsize=1)
def font_style():
    encoded = base64.b64encode((ART / "JetBrainsMono.woff2").read_bytes()).decode()
    return "@font-face{font-family:'JetBrains Mono';font-style:normal;font-weight:100 800;src:url(data:font/woff2;base64," + encoded + ") format('woff2')}"


def portrait_data():
    with Image.open(PORTRAIT) as original:
        portrait = ImageOps.exif_transpose(original).convert("RGB")
        portrait.thumbnail((600, 600), Image.Resampling.LANCZOS)
        buffer = BytesIO()
        portrait.save(buffer, format="JPEG", quality=95)
    return base64.b64encode(buffer.getvalue()).decode()


def node_slug(name):
    slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    if not re.fullmatch(r"[a-z][a-z0-9-]*", slug):
        raise ValueError("Unsafe mesh node name: " + name)
    return slug


def ticker_animation(index, count, period=3.2):
    if count < 1:
        raise ValueError("Ticker needs at least one node")
    cycle = round(count * period, 1)
    fade = 0.012
    start = index / count
    end = (index + 1) / count
    if index == 0:
        times = [0.0, round(end - fade, 4), round(end, 4), 1.0]
        values = ["1", "1", "0", "0"]
    elif index == count - 1:
        times = [0.0, round(start, 4), round(min(1.0, start + fade), 4), round(1.0 - fade, 4), 1.0]
        values = ["0", "0", "1", "1", "0"]
    else:
        times = [0.0, round(start, 4), round(min(1.0, start + fade), 4), round(max(start + fade, end - fade), 4), round(end, 4), 1.0]
        values = ["0", "0", "1", "1", "0", "0"]
    cleaned_times = []
    cleaned_values = []
    for time, value in zip(times, values):
        time = min(max(time, 0.0), 1.0)
        if cleaned_times and time <= cleaned_times[-1]:
            time = min(1.0, round(cleaned_times[-1] + 0.0001, 4))
        cleaned_times.append(time)
        cleaned_values.append(value)
    cleaned_times[-1] = 1.0
    return cycle, cleaned_times, cleaned_values


def mesh_hover_style(slugs):
    rules = [".card{opacity:0}", ".static-legend{display:none}"]
    for slug in slugs:
        rules.append(f"#{slug}:hover~#card-{slug},#{slug}:focus-within~#card-{slug}{{opacity:1}}")
    rules.append("@media(prefers-reduced-motion:reduce){.static-legend{display:block}}")
    return "".join(rules)


def card_box(x, y, width=268, height=102):
    left = x + 18
    top = y - height / 2
    if left + width > MAP_WIDTH - 16:
        left = x - width - 18
    left = min(max(16.0, left), float(MAP_WIDTH - width - 16))
    top = min(max(82.0, top), float(MAP_HEIGHT - 48 - height))
    return (round(left, 1), round(top, 1), width, height)


def ticker_line(info):
    return info["title"] + "   " + "   ".join(info["files"][:2])


def mesh_nodes():
    # name, x, y, hub  — constellation of the live cell, not a box diagram
    return (
        ("YOU", 420, 328, True),
        ("Desktop", 650, 214, False),
        ("libsynapsed", 530, 404, False),
        ("session Tor", 268, 176, False),
        ("Tor mesh", 420, 138, False),
        ("PoE v1", 292, 262, False),
        ("Wallet", 158, 392, False),
        ("NAAN", 198, 236, False),
        ("Local GGUF", 108, 156, False),
        ("MSG", 548, 246, False),
        ("desktop cell", 248, 492, False),
        ("VPS mesh-peer", 628, 492, False),
        ("PEX", 420, 448, False),
        ("majority", 420, 556, False),
        ("IDE CODE", 668, 128, False),
        ("RECIPE", 118, 318, False),
        ("POE_VOTE", 708, 332, False),
        ("two clocks", 742, 412, False),
        ("FINALIZED", 612, 556, False),
        ("KNOW", 752, 556, False),
        ("RingCT", 178, 556, False),
        ("scar", 300, 576, False),
        ("NGT", 88, 500, False),
        ("HARVEST", 72, 236, False),
        ("NET", 762, 256, False),
        ("SET", 780, 176, False),
    )


def node_briefs():
    return {
        "YOU": {"title": "YOU", "blurb": "The operator in the cell", "files": ("~/.synapsenet/synapsenet.conf", "wallet.dat + wallet.key", "poe/poe.db  lib  models/*.gguf")},
        "Desktop": {"title": "Desktop", "blurb": "synapsenet-app Tauri, JSON RPC same process", "files": ("MAIN WALLET SEND BLOCKS KNOW NAAN", "HARVEST INTEL MSG IDE NET RENT SET", "tauri-app/src/app/App.svelte")},
        "libsynapsed": {"title": "libsynapsed.so", "blurb": "C++ engine in-process with the desktop", "files": ("session Tor  PoE v1  Transfer", "NAAN harvest  GGUF mouth  MSG sealed", "src/ide/synapsed_engine.cpp")},
        "session Tor": {"title": "session Tor", "blurb": "App-owned daemon, not Tor Browser", "files": ("ADD_ONION NEW  DiscardPK", "ephemeral v3 onion", "src/core/tor_process_guard.cpp")},
        "Tor mesh": {"title": "Tor mesh", "blurb": "Fail-closed peer transport", "files": ("onion :8333  no clearnet fallback", "seed only from synapsenet.conf", "SOCKS  POE_*  stealth tx")},
        "PoE v1": {"title": "PoE v1", "blurb": "Peer knowledge consensus, not an LLM", "files": ("poe.db  entries  votes  finalize", "POE_ENTRY  POE_VOTE", "POE_RECIPE  POE_RECIPE_REPLAY")},
        "Wallet": {"title": "Wallet", "blurb": "Local wallet and claims", "files": ("wallet.dat  stealth SEND", "RingCT coinbase  key image", "src/core/wallet.cpp")},
        "NAAN": {"title": "NAAN", "blurb": "The miner: lymph then recipe", "files": ("Tor first public pages", "gate solver / OCR / GGUF", "scar of a door  no faucet mint")},
        "Local GGUF": {"title": "Local GGUF", "blurb": "Mouth isolation: talks, never judges", "files": ("prompt / score from the model", "cannot vote or finalize", "weights stay on disk")},
        "MSG": {"title": "MSG", "blurb": "Sealed messages to a peer onion", "files": ("ML-KEM + X25519 if kem_pk", "else crypto_box_seal", "NET map still shows YOU")},
        "desktop cell": {"title": "desktop cell", "blurb": "Full cell: peer + miner + validator", "files": ("poe_pk  NAAN  stealth wallet", "PEX onions to the VPS", "2 of 2 / majority")},
        "VPS mesh-peer": {"title": "VPS mesh-peer", "blurb": "Always-on mailbox, not a desktop", "files": ("mesh-peer.py  stable onion", "synapsed-poe-mesh  votes if poe_pk", "no stealth NGT on the mailbox")},
        "PEX": {"title": "PEX", "blurb": "Onions learned over Tor, not baked in", "files": ("desktop cell <-> VPS", "seed_nodes in synapsenet.conf", "rotate the example, not the .so")},
        "majority": {"title": "majority", "blurb": "PoE is votes, not a roll call of onions", "files": ("two full cells: both must vote", "then 2 of 3, 3 of 5, ...", "thin mailbox without poe_pk stays mail")},
        "IDE CODE": {"title": "IDE CODE / TEXT", "blurb": "Human knowledge entry from the IDE", "files": ("title + body  PoW 12  <= 64 KiB", "POE_ENTRY to VPS + other cells", "snippet.rs in IDE")},
        "RECIPE": {"title": "RECIPE", "blurb": "Hash of a public page, never the bytes", "files": ("lymph on the same machine", "hash mismatch dies here", "locator + selector + body hash")},
        "POE_VOTE": {"title": "POE_VOTE", "blurb": "Each full cell casts its own vote", "files": ("after mouth isolation", "GGUF cannot vote", "src/core/poe_v1.cpp")},
        "two clocks": {"title": "two clocks", "blurb": "Cite DAG orders recipes", "files": ("Tor arrival time does not vote", "quorum of absence = not seen", "never as false")},
        "FINALIZED": {"title": "FINALIZED", "blurb": "Accepted knowledge, three exits", "files": ("KNOW card stays public", "RingCT coinbase 0.10 stealth", "scar: class, UTC day, method")},
        "KNOW": {"title": "KNOW", "blurb": "Author, amount, rewardId are public", "files": ("ACTIVE / SLEEPING / RETRACTED", "seniority ring: prove N, not which N", "KNOW card stays public")},
        "RingCT": {"title": "RingCT", "blurb": "Mint is coinbase; spend is stealth", "files": ("CODE/TEXT or RECIPE after 2 of 2", "SEND: SN + 128 hex  MLSAG-2", "Pedersen  range64  key image  Tor")},
        "scar": {"title": "scar", "blurb": "The organism remembers a public door", "files": ("gate class  UTC day  method class", "no cookie  no session", "INTEL may show the class")},
        "NGT": {"title": "NGT", "blurb": "Earn-or-transfer. No in-protocol buy.", "files": ("size penalty after 8 KiB", "first reward sweep secp, later ring", "Dilithium when liboqs is real")},
        "HARVEST": {"title": "HARVEST", "blurb": "What the agent pulled, Tor SOCKS only", "files": ("tick -> gate -> lymph -> RECIPE", "HARVEST log  no NGT from a faucet", "NGT only after finalize")},
        "NET": {"title": "NET", "blurb": "YOU sit in the center of the map", "files": ("wheel zooms  drag pans", "share the session onion", "it is new each launch")},
        "SET": {"title": "SET", "blurb": "Tor only. No clearnet button.", "files": ("Tor or Tor + Bridges", "model  resources  NAAN  profile", "quantum status is read-only")},
    }


def mesh_edges(names):
    edges = set()
    for i, (_, x, y, _) in enumerate(names):
        nearest = sorted((j for j in range(len(names)) if j != i), key=lambda j: math.hypot(names[j][1] - x, names[j][2] - y))[:2]
        edges.update(tuple(sorted((i, j))) for j in nearest)
    by_name = {name: index for index, (name, *_rest) in enumerate(names)}

    def link(*pair):
        edges.add(tuple(sorted((by_name[pair[0]], by_name[pair[1]]))))

    for left, right in [
        ("YOU", "Desktop"),
        ("YOU", "libsynapsed"),
        ("YOU", "Tor mesh"),
        ("YOU", "NET"),
        ("Desktop", "libsynapsed"),
        ("Desktop", "SET"),
        ("libsynapsed", "session Tor"),
        ("libsynapsed", "PoE v1"),
        ("libsynapsed", "Wallet"),
        ("libsynapsed", "NAAN"),
        ("libsynapsed", "Local GGUF"),
        ("libsynapsed", "MSG"),
        ("session Tor", "Tor mesh"),
        ("Tor mesh", "desktop cell"),
        ("Tor mesh", "VPS mesh-peer"),
        ("desktop cell", "PEX"),
        ("VPS mesh-peer", "PEX"),
        ("desktop cell", "majority"),
        ("VPS mesh-peer", "majority"),
        ("IDE CODE", "PoE v1"),
        ("HARVEST", "NAAN"),
        ("NAAN", "RECIPE"),
        ("RECIPE", "PoE v1"),
        ("Local GGUF", "POE_VOTE"),
        ("PoE v1", "POE_VOTE"),
        ("POE_VOTE", "two clocks"),
        ("two clocks", "FINALIZED"),
        ("majority", "FINALIZED"),
        ("FINALIZED", "KNOW"),
        ("FINALIZED", "RingCT"),
        ("FINALIZED", "scar"),
        ("Wallet", "RingCT"),
        ("RingCT", "NGT"),
        ("NAAN", "scar"),
    ]:
        link(left, right)
    return edges


def net(theme):
    names = mesh_nodes()
    briefs = node_briefs()
    if set(briefs) != {name for name, *_ in names}:
        raise ValueError("Mesh node briefs must cover every node and only those nodes")
    slugs = [node_slug(name) for name, *_ in names]
    svg = SVG(MAP_HEIGHT, theme, "SynapseNet map — the cell, the mesh, the ledger", extra_style=mesh_hover_style(slugs))
    svg.raw(f'<defs><pattern id="grid" width="40" height="40" patternUnits="userSpaceOnUse"><path d="M40 0H0V40" fill="none" stroke="{svg.colors["grid"]}"/></pattern></defs>')
    svg.rect(0, 0, MAP_WIDTH, MAP_HEIGHT, "url(#grid)")
    bottom = MAP_HEIGHT - 30
    for path in [f"M14 30V14H30", f"M826 30V14h-16", f"M14 {bottom}v16h16", f"M826 {bottom}v16h-16"]:
        svg.raw(f'<path d="{path}" fill="none" stroke="{ACCENT}" stroke-width="2"/>')
    svg.text(22, 40, "SynapseNet", 19, "text", font_weight=600)
    svg.text(178, 40, "@" + LOGIN, 13, ACCENT)
    svg.text(22, 62, "the cell, the mesh, the ledger", 12)
    svg.rect(0, 76, MAP_WIDTH, 1, svg.colors["edge"])
    for index, (a, b) in enumerate(sorted(mesh_edges(names))):
        _, x1, y1, _ = names[a]
        _, x2, y2, _ = names[b]
        svg.raw(f'<path id="edge{index}" d="M{x1},{y1}L{x2},{y2}" fill="none" stroke="{svg.colors["edge"]}"/>')
        duration, begin = 3.5 + index % 7 * 0.19, -(index * 0.37 % 4)
        svg.raw(
            f'<circle class="motion" r="2" fill="{ACCENT}" opacity="0">'
            f'<animate attributeName="opacity" values="0;1;1;0" keyTimes="0;0.12;0.88;1" dur="{duration:.2f}s" begin="{begin:.2f}s" repeatCount="indefinite"/>'
            f'<animateMotion dur="{duration:.2f}s" begin="{begin:.2f}s" repeatCount="indefinite"><mpath href="#edge{index}" xlink:href="#edge{index}"/></animateMotion></circle>'
        )
    for name, x, y, hub in names:
        radius = 16 if hub else 6
        stroke = ACCENT if hub else svg.colors["dim"]
        fill_node = ACCENT if hub else svg.colors["card"]
        slug = node_slug(name)
        info = briefs[name]
        hit = max(radius + 10, 20)
        svg.raw(f'<g id="{slug}" class="node" tabindex="0">')
        svg.raw(f'<title>{escape(info["title"])}: {escape(info["blurb"])}. {escape(", ".join(info["files"]))}</title>')
        svg.raw(f'<circle class="hit" cx="{x}" cy="{y}" r="{hit}" fill="#000" fill-opacity="0"/>')
        if hub:
            svg.raw(
                f'<circle class="motion" cx="{x}" cy="{y}" r="{radius}" fill="none" stroke="{ACCENT}">'
                f'<animate attributeName="r" values="{radius};{radius + 12}" dur="2.8s" repeatCount="indefinite"/>'
                f'<animate attributeName="opacity" values=".6;0" dur="2.8s" repeatCount="indefinite"/></circle>'
            )
        svg.raw(f'<circle class="core" cx="{x}" cy="{y}" r="{radius}" fill="{fill_node}" stroke="{stroke}" stroke-width="1.5"/>')
        if name == "YOU":
            svg.text(x - radius - 12, y + 4, "YOU", 12, "text", text_anchor="end")
        else:
            svg.text(x + radius + 10 if x < 500 else x - radius - 10, y + 4, name, 12, "muted", text_anchor="start" if x < 500 else "end")
        svg.raw("</g>")
    legend_y = MAP_HEIGHT - 18
    for index, (name, *_) in enumerate(names):
        line = ticker_line(briefs[name])
        cycle, key_times, values = ticker_animation(index, len(names))
        svg.raw(
            f'<g class="motion" opacity="0"><text x="22" y="{legend_y}" fill="{svg.colors["dim"]}" font-size="10">{escape(line)}</text>'
            f'<animate attributeName="opacity" values="{",".join(values)}" keyTimes="{";".join(f"{time:.4f}" for time in key_times)}" dur="{cycle}s" begin="0s" repeatCount="indefinite"/></g>'
        )
    svg.raw(f'<text class="static-legend" x="22" y="{legend_y}" fill="{svg.colors["dim"]}" font-size="10">filled: YOU / outlined: the cell, the mesh, the ledger</text>')
    svg.text(MAP_WIDTH - 22, legend_y, "hover a node / 2 of 2 then majority", 10, "dim", text_anchor="end")
    for name, x, y, _hub in names:
        info = briefs[name]
        slug = node_slug(name)
        left, top, width, height = card_box(x, y)
        svg.raw(f'<g id="card-{slug}" class="card">')
        svg.rect(left, top, width, height, svg.colors["card"], rx=6, stroke=ACCENT)
        svg.rect(left, top, 3, height, ACCENT)
        svg.text(left + 14, top + 22, info["title"], 12, "text", font_weight=600)
        svg.text(left + 14, top + 40, info["blurb"], 10, "muted")
        for index, path in enumerate(info["files"][:3]):
            svg.text(left + 14, top + 62 + index * 14, path, 10, "dim")
        svg.raw("</g>")
    return svg


def hero(theme):
    svg = SVG(170, theme, "SynapseNet — intelligence belongs to everyone")
    svg.section("SynapseNet", "public cell / alpha")
    svg.text(48, 92, "SynapseNet", 34, "text", font_weight=700)
    svg.text(48, 122, "Intelligence belongs to everyone.", 14, ACCENT)
    svg.text(48, 148, "Local GGUF models. Tor peers. Proof of Emergence. NAAN harvest on a station campus.", 12)
    return svg


def tiles(theme):
    svg = SVG(92, theme, "SynapseNet status tiles")
    items = [("ALPHA", "STATUS"), ("TOR ONLY", "MESH"), ("MIT", "LICENSE"), ("C++ / RUST", "ENGINE"), ("TAURI", "SHELL")]
    for index, (value, name) in enumerate(items):
        x = round(index * 169.6, 1)
        color = ACCENT if index in (1, 3) else svg.colors["edge"]
        svg.rect(x, 0.5, 161.6, 91, svg.colors["card"], rx=6, stroke=svg.colors["edge"])
        svg.rect(x, 0.5, 3, 91, color)
        svg.text(x + 80.8, 46, value, 18 if len(str(value)) > 8 else 24, ACCENT if index in (1, 3) else "text", text_anchor="middle", font_weight=600)
        svg.text(x + 80.8, 68, name, 9, "dim", text_anchor="middle", letter_spacing=0.7)
    return svg


def portrait(theme):
    svg = SVG(325, theme, "Kepler — independent builder of SynapseNet")
    svg.section("Kepler / SynapseNet", "independent builder")
    svg.rect(22, 58, 206, 250, svg.colors["card"], rx=6, stroke=svg.colors["edge"])
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
    svg.section("Elsewhere", "@" + LOGIN)
    for x, width, value in [(140, 160, "SynapseNet"), (314, 196, "GitHub profile"), (524, 176, "Issues + ideas")]:
        svg.rect(x, 59, width, 30, svg.colors["card"], rx=15, stroke=svg.colors["edge"])
        svg.text(x + width / 2, 79, value, 11, ACCENT, text_anchor="middle")
    svg.text(420, 116, "Local models. Shared knowledge. Open infrastructure.", 11, "dim", text_anchor="middle")
    svg.text(420, 138, "Desktop screenshots are coming next. The map stays here.", 10, "dim", text_anchor="middle")
    return svg


def render():
    if not (ART / "JetBrainsMono.woff2").is_file():
        raise FileNotFoundError("pictures/art/JetBrainsMono.woff2 is required")
    if not PORTRAIT.is_file():
        raise FileNotFoundError("pictures/kepler.jpg is required")
    graphics = {"hero": hero, "tiles": tiles, "map": net, "portrait": portrait, "footer": footer}
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
