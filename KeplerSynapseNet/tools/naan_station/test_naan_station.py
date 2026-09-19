#!/usr/bin/env python3
# Layout tests for the NAAN harvest map. Run:
#   python3 -m unittest KeplerSynapseNet/tools/naan_station/test_naan_station.py
from __future__ import annotations

import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from deck import (
    CrewMember,
    build_deck,
    in_deck,
    label_hits_room,
    load_station_data,
    pack_wings,
    screenshot_crew,
)
from preview import render_deck


class NaanStationLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = load_station_data()
        cls.primary, cls.crew = screenshot_crew()
        cls.deck = build_deck(cls.primary, cls.crew, cls.data)

    def test_json_view_matches_the_live_map(self):
        self.assertEqual(self.data.view_cols, 64)
        self.assertEqual(self.data.view_rows, 36)
        self.assertEqual(self.data.tile, 16)
        self.assertEqual(self.data.origin_y, 14)
        self.assertLessEqual(self.deck.ox + self.deck.st_cols, self.data.view_cols)

    def test_screenshot_crew_fits_the_view(self):
        strides = pack_wings(self.crew, self.data.primary_cols, self.data.view_cols)
        self.assertEqual(len(strides), 2)
        self.assertLessEqual(self.data.primary_cols + sum(strides), self.data.view_cols - 2)
        self.assertLessEqual(self.deck.st_cols, self.data.view_cols - 1)

    def test_rooms_of_one_owner_do_not_overlap(self):
        by_owner: dict[str, list] = {}
        for room in self.deck.rooms:
            if room.kind == "hall":
                continue
            by_owner.setdefault(room.owner, []).append(room)
        for owner, rooms in by_owner.items():
            for i, a in enumerate(rooms):
                for b in rooms[i + 1 :]:
                    overlap = not (a.x2 < b.x1 or b.x2 < a.x1 or a.y2 < b.y1 or b.y2 < a.y1)
                    self.assertFalse(overlap, f"{owner} {a.id} overlaps {b.id}")

    def test_props_stay_inside_their_room(self):
        rooms = {r.id: r for r in self.deck.rooms}
        missing = 0
        for ov in self.deck.overlays:
            room = rooms.get(ov.room)
            self.assertIsNotNone(room, ov.room)
            self.assertGreaterEqual(ov.tx, room.x1, ov.file)
            self.assertGreaterEqual(ov.ty, room.y1, ov.file)
            self.assertLessEqual(ov.tx, room.x2, ov.file)
            self.assertLessEqual(ov.ty, room.y2, ov.file)
            prop = self.data.public / "props" / ov.file
            if not prop.is_file():
                missing += 1
        self.assertLess(missing, 3, "prop PNGs missing under public/station/props")

    def test_room_labels_sit_above_the_hull(self):
        hits = []
        for label in self.deck.labels:
            rooms = label_hits_room(self.deck, label)
            if rooms:
                hits.append((label.text, [r.id for r in rooms]))
        self.assertEqual(hits, [], f"room labels still cover furniture: {hits}")

    def test_room_labels_are_short_and_unprefixed(self):
        for label in self.deck.labels:
            self.assertNotIn("·", label.text)
            self.assertLessEqual(len(label.text), 8)
            self.assertEqual(label.text, label.text.upper())
        banners = {b.owner: b.text for b in self.deck.banners}
        self.assertEqual(banners.get("crew-rick"), "RICK")
        self.assertEqual(banners.get("crew-alien"), "ALIEN")
        self.assertNotIn("primary", banners)

    def test_nameplate_is_narrower_than_a_wing_room(self):
        # Compact chip is 64px. Extra-crew rooms are at least 5 tiles (80px).
        chip_px = 64
        wing = [r for r in self.deck.rooms if r.owner != "primary" and r.kind != "hall"]
        self.assertTrue(wing)
        smallest = min(r.w * self.deck.tile for r in wing)
        self.assertLessEqual(chip_px, smallest)

    def test_walkable_tiles_exist_for_every_body(self):
        self.assertIn("primary", self.deck.walk)
        for member in self.crew:
            cells = self.deck.walk[member.id]
            self.assertGreater(len(cells), 8, member.id)
            for kind in member.rooms:
                rooms = [r for r in self.deck.rooms if r.owner == member.id and r.kind == kind]
                if not rooms:
                    continue
                room = rooms[0]
                self.assertTrue(any(room.contains(*map(int, k.split(","))) for k in cells), member.id + kind)

    def test_inspector_can_paint_the_screenshot_deck(self):
        image = render_deck(self.deck, self.data, self.crew, scale=1)
        self.assertEqual(image.size, (self.deck.view_cols * self.deck.tile, self.deck.view_rows * self.deck.tile))
        # Void is dark; hull stamps should add brighter floor pixels.
        extrema = image.getextrema()
        self.assertTrue(any(channel[1] > 40 for channel in extrema))


    def test_primary_only_still_builds(self):
        deck = build_deck(["bed", "tor"], [], self.data)
        kinds = {r.kind for r in deck.rooms if r.owner == "primary" and r.kind != "hall"}
        self.assertEqual(kinds, {"bed", "tor"})
        self.assertTrue(in_deck(deck, deck.rooms[1].x1, deck.rooms[1].y1))


if __name__ == "__main__":
    unittest.main()
