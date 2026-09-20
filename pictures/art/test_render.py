#!/usr/bin/env python3
"""Safety checks for SynapseNet README graphics."""

from pathlib import Path
import unittest

from render import ART, ROOT, render

README = ROOT / "README.md"
NAMES = ("hero", "tiles", "map", "portrait", "footer")


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

    def test_map_is_a_live_mesh(self):
        body = (ART / "map-dark.svg").read_text(encoding="utf-8")
        self.assertIn(">YOU<", body)
        self.assertIn("NAAN", body)
        self.assertIn("animateMotion", body)
        self.assertIn("The map", body)

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


if __name__ == "__main__":
    unittest.main()
