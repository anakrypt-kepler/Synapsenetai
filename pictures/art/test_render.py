#!/usr/bin/env python3
"""Safety checks for SynapseNet README graphics."""

import base64
from io import BytesIO
from pathlib import Path
import re
import unittest

from PIL import Image

from render import ART, HEADINGS, ROOT, mesh_nodes, node_briefs, render

README = ROOT / "README.md"
CORE = ("hero", "tiles", "map", "mesh", "portrait", "footer")
FLOWS = ("install", "lego", "naan", "know", "skin", "wizard", "tabs", "docker", "cards")
STRIPS = tuple(key for key, _title, _note in HEADINGS)
MESH_MARKERS = (
    "YOU",
    "Desktop",
    "libsynapsed",
    "Tor mesh",
    "PoE v1",
    "NAAN",
    "RECIPE",
    "POE_VOTE",
    "FINALIZED",
    "KNOW",
    "RingCT",
    "majority",
    "VPS mesh-peer",
    "HARVEST",
    "NGT",
)


class RenderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.written = render()

    def test_writes_dark_and_light_svgs(self):
        for name in CORE + FLOWS + STRIPS:
            for theme in ("dark", "light"):
                path = ART / f"{name}-{theme}.svg"
                self.assertTrue(path.is_file(), path)
                body = path.read_text(encoding="utf-8")
                self.assertTrue(body.startswith("<svg "))
                self.assertIn("</svg>", body)
                self.assertNotIn("<script", body.lower())
                self.assertIn("#FF4D0D", body)

    def test_core_and_flow_embed_jetbrains_mono(self):
        for name in CORE + FLOWS:
            body = (ART / f"{name}-dark.svg").read_text(encoding="utf-8")
            self.assertIn("JetBrains Mono", body)

    def test_map_matches_profile_mesh(self):
        for name in ("map", "mesh"):
            body = (ART / f"{name}-dark.svg").read_text(encoding="utf-8")
            self.assertIn("SynapseNet", body)
            self.assertIn("@anakrypt-kepler", body)
            self.assertIn("the cell, the mesh, the ledger", body)
            self.assertIn("animateMotion", body)
            self.assertIn("mpath", body)
            self.assertIn('id="you"', body)
            for marker in MESH_MARKERS:
                self.assertIn(marker, body)
        self.assertEqual(set(node_briefs()), {name for name, *_ in mesh_nodes()})

    def test_portrait_uses_kepler_red_face_photo(self):
        body = (ART / "portrait-dark.svg").read_text(encoding="utf-8")
        match = re.search(r"data:image/gif;base64,([A-Za-z0-9+/=]+)", body)
        self.assertIsNotNone(match)
        with Image.open(BytesIO(base64.b64decode(match.group(1)))) as image:
            frame = image.convert("RGB")
            left, top, right, bottom = (
                int(frame.width * 0.38),
                int(frame.height * 0.28),
                int(frame.width * 0.62),
                int(frame.height * 0.55),
            )
            crop = frame.crop((left, top, right, bottom))
            bright = [pixel for pixel in crop.getdata() if pixel[0] + pixel[1] + pixel[2] > 90]
            self.assertGreater(len(bright), 80)
            red = sum(pixel[0] for pixel in bright) / len(bright)
            green = sum(pixel[1] for pixel in bright) / len(bright)
            self.assertGreater(red, green + 20)
            self.assertGreater(getattr(image, "n_frames", 1), 20)

    def test_install_keeps_clone_path(self):
        body = (ART / "install-dark.svg").read_text(encoding="utf-8")
        for marker in ("git clone", "lego-linux.sh", "run-desktop.sh", "~/.synapsenet", "animateMotion"):
            self.assertIn(marker, body)

    def test_readme_uses_profile_picture_tags(self):
        text = README.read_text(encoding="utf-8")
        self.assertEqual(text.count("pictures/art/mesh-dark.svg"), 1)
        self.assertEqual(text.count("pictures/art/mesh-light.svg"), 1)
        self.assertGreater(text.find("pictures/art/mesh-dark.svg"), text.find("This is alpha."))
        shown = ("hero", "mesh", "portrait", "footer") + FLOWS + (
            "hd-what",
            "hd-not",
            "hd-why",
            "hd-mine",
            "hd-now",
            "hd-gates",
            "hd-skeptic",
            "hd-change",
            "hd-later",
            "hd-boot",
            "hd-set",
            "hd-support",
            "hd-license",
        )
        for name in shown:
            self.assertIn(f"pictures/art/{name}-dark.svg", text, name)
            self.assertIn(f"pictures/art/{name}-light.svg", text, name)
        self.assertNotIn("pictures/art/tiles-dark.svg", text)
        self.assertIsNone(re.search(r"^## ", text, flags=re.M))
        for name in ("hd-naan", "hd-skin", "hd-linux", "hd-wizard", "hd-tabs", "hd-docker"):
            self.assertNotIn(f"pictures/art/{name}-dark.svg", text, name)
        self.assertIn("<picture>", text)
        self.assertIn("git clone https://github.com/anakrypt-kepler/Synapsenetai.git", text)
        self.assertIn("./KeplerSynapseNet/scripts/lego-linux.sh", text)
        self.assertIn("~/.synapsenet/run-desktop.sh", text)
        self.assertIn("| 13 | Print the remaining human steps |", text)
        self.assertNotIn("pictures/cell-naan.png", text)
        self.assertNotIn("pictures/cell-main.png", text)
        self.assertNotIn("pictures/cell-station.png", text)
        self.assertNotIn("pictures/header.gif", text)
        self.assertIn("pictures/art/portrait-dark.svg", text)
        self.assertNotIn('<img src="pictures/kepler.gif"', text)
        self.assertNotIn("┌", text)
        self.assertNotIn("synapsenet-app Tauri", text)

    def test_kepler_clip_is_renamed_and_stripped(self):
        gif = ROOT / "pictures" / "kepler.gif"
        mp4 = ROOT / "pictures" / "kepler.mp4"
        self.assertTrue(gif.is_file(), gif)
        self.assertTrue(mp4.is_file(), mp4)
        with Image.open(gif) as image:
            self.assertEqual(image.format, "GIF")
            self.assertGreater(image.n_frames, 20)
            self.assertEqual(image.info.get("loop"), 0)
        blob = mp4.read_bytes()
        self.assertNotIn(b"creation_time", blob)
        self.assertNotIn(b"Core Media", blob)
        self.assertNotIn(b"iPhone", blob)
