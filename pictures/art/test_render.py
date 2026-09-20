#!/usr/bin/env python3
"""Safety checks for SynapseNet README graphics."""

from pathlib import Path
import unittest

from render import ART, ROOT, mesh_nodes, node_briefs, render

README = ROOT / "README.md"
NAMES = ("hero", "tiles", "map", "portrait", "footer")
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
        for name in NAMES:
            for theme in ("dark", "light"):
                path = ART / f"{name}-{theme}.svg"
                self.assertTrue(path.is_file(), path)
                body = path.read_text(encoding="utf-8")
                self.assertTrue(body.startswith("<svg "))
                self.assertIn("</svg>", body)
                self.assertNotIn("<script", body.lower())
                self.assertIn("JetBrains Mono", body)
                self.assertIn("#FF4D0D", body)

    def test_map_matches_profile_mesh(self):
        body = (ART / "map-dark.svg").read_text(encoding="utf-8")
        self.assertIn("SynapseNet", body)
        self.assertIn("@anakrypt-kepler", body)
        self.assertIn("the cell, the mesh, the ledger", body)
        self.assertIn("animateMotion", body)
        self.assertIn("mpath", body)
        self.assertIn('id="you"', body)
        for marker in MESH_MARKERS:
            self.assertIn(marker, body)
        self.assertEqual(set(node_briefs()), {name for name, *_ in mesh_nodes()})

    def test_readme_uses_profile_picture_tags(self):
        text = README.read_text(encoding="utf-8")
        for name in NAMES:
            self.assertIn(f"pictures/art/{name}-dark.svg", text)
            self.assertIn(f"pictures/art/{name}-light.svg", text)
        self.assertIn("<picture>", text)
        self.assertNotIn("pictures/cell-naan.png", text)
        self.assertNotIn("pictures/cell-main.png", text)
        self.assertNotIn("pictures/cell-station.png", text)
        self.assertNotIn("pictures/header.gif", text)
        self.assertNotIn("┌", text)
        self.assertNotIn("synapsenet-app Tauri", text)
