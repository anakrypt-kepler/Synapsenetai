#!/usr/bin/env python3
# Layout tests for the NAAN harvest map. Run:
#   python3 -m unittest KeplerSynapseNet/tools/naan_station/test_naan_station.py
from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from deck import (
    HULL_RIM,
    SUITE_H,
    SUITE_W,
    build_deck,
    campus_origin,
    clip_prop_to_room,
    clip_rect,
    crop_offsets,
    dilated_hull_rects,
    in_deck,
    label_hits_room,
    load_station_data,
    prop_sprite_rect,
    room_clip_rect,
    screenshot_crew,
)
from preview import render_deck

CHIP = HERE.parents[1] / "tauri-app" / "src" / "app" / "components" / "sprites" / "AgentLevelChip.svelte"
STATION = HERE.parents[1] / "tauri-app" / "src" / "app" / "components" / "sprites" / "NaanStation.svelte"


class NaanStationLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = load_station_data()
        cls.primary, cls.crew = screenshot_crew()
        cls.deck = build_deck(cls.primary, cls.crew, cls.data)

    def test_json_view_matches_the_live_map(self):
        self.assertEqual(self.data.view_cols, 80)
        self.assertEqual(self.data.view_rows, 48)
        self.assertEqual(self.data.tile, 16)
        self.assertEqual(self.data.origin_y, 10)
        self.assertEqual(self.data.primary_cols, 25)
        self.assertEqual(self.data.suite_w, 25)
        self.assertEqual(self.data.suite_h, 12)
        self.assertEqual(self.data.spine, 3)
        self.assertLessEqual(self.deck.ox + self.deck.st_cols, self.data.view_cols)

    def test_screenshot_crew_wraps_down_not_across(self):
        rick_o = campus_origin(1, self.data.view_cols)
        alien_o = campus_origin(2, self.data.view_cols)
        self.assertEqual(rick_o, (28, 0))
        self.assertEqual(alien_o, (0, 15))
        self.assertGreaterEqual(rick_o[0], 26)
        self.assertGreaterEqual(alien_o[1], 13)
        rick = [r for r in self.deck.rooms if r.owner == "crew-rick" and r.kind != "hall"]
        alien = [r for r in self.deck.rooms if r.owner == "crew-alien" and r.kind != "hall"]
        self.assertTrue(rick)
        self.assertTrue(alien)
        self.assertGreaterEqual(min(r.x1 for r in rick), rick_o[0])
        self.assertLessEqual(max(r.x2 for r in rick), rick_o[0] + SUITE_W - 1)
        self.assertGreaterEqual(min(r.y1 for r in alien), alien_o[1])
        self.assertLessEqual(max(r.y2 for r in alien), alien_o[1] + SUITE_H - 1)
        north = {r.kind: r for r in alien if r.kind in ("tor", "recipe", "poe")}
        self.assertEqual(set(north), {"tor", "recipe", "poe"})
        self.assertLess(north["tor"].x1, north["recipe"].x1)
        self.assertLess(north["recipe"].x1, north["poe"].x1)
        self.assertEqual(north["tor"].y1, alien_o[1])

    def test_rick_keeps_a_full_bed(self):
        beds = [r for r in self.deck.rooms if r.owner == "crew-rick" and r.kind == "bed"]
        self.assertEqual(len(beds), 1)
        bed = beds[0]
        self.assertGreaterEqual(bed.w, 6)
        cells = self.deck.walk["crew-rick"]
        self.assertTrue(any(bed.contains(*map(int, k.split(","))) for k in cells))

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
        for label in self.deck.labels + self.deck.banners:
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

    def test_clip_helper_intersects_and_crops(self):
        self.assertEqual(clip_rect((0, 0, 10, 10), (5, 5, 10, 10)), (5, 5, 5, 5))
        self.assertIsNone(clip_rect((0, 0, 2, 2), (8, 8, 2, 2)))
        self.assertEqual(clip_rect((2, 3, 4, 5), (0, 0, 20, 20)), (2, 3, 4, 5))
        dest = (0.0, -8.0, 16.0, 24.0)
        room = (0.0, 0.0, 32.0, 32.0)
        clipped = clip_rect(dest, room)
        self.assertEqual(clipped, (0.0, 0.0, 16.0, 16.0))
        self.assertEqual(crop_offsets(dest, clipped), (0.0, 8.0, 16.0, 16.0))

    def test_props_clipped_stay_within_one_tile_of_room(self):
        rooms = {r.id: r for r in self.deck.rooms}
        tile = self.deck.tile
        overhangs = 0
        for ov in self.deck.overlays:
            room = rooms[ov.room]
            raw = prop_sprite_rect(ov, self.deck.ox, self.deck.oy, tile, self.deck.dump_t)
            rx, ry, rw, rh = room.pixels(self.deck.ox, self.deck.oy, tile)
            sx, sy, sw, sh = raw
            if sx < rx - 0.01 or sy < ry - 0.01 or sx + sw > rx + rw + 0.01 or sy + sh > ry + rh + 0.01:
                overhangs += 1
            clipped = clip_prop_to_room(ov, room, self.deck.ox, self.deck.oy, tile, self.deck.dump_t)
            self.assertIsNotNone(clipped, ov.file)
            cx, cy, cw, ch = clipped
            self.assertGreaterEqual(cx, rx - 0.01, ov.file)
            self.assertGreaterEqual(cy, ry - 0.01, ov.file)
            self.assertLessEqual(cx + cw, rx + rw + 0.01, ov.file)
            self.assertLessEqual(cy + ch, ry + rh + 0.01, ov.file)
        self.assertGreater(overhangs, 0, "dump bx/by/bw/bh should hang past at least one room before clip")

    def test_dilated_hull_keeps_the_shell_rim(self):
        rects = dilated_hull_rects(self.deck, 1)
        self.assertTrue(rects)
        tile = self.deck.tile
        # A north-edge hull stamp starts rim pixels above the tile.
        ox, oy = self.deck.ox, self.deck.oy
        sample = None
        for y in range(self.deck.st_rows):
            for x in range(self.deck.st_cols):
                if in_deck(self.deck, x, y) and not in_deck(self.deck, x, y - 1):
                    sample = (x, y)
                    break
            if sample:
                break
        self.assertIsNotNone(sample)
        x, y = sample
        dx = (ox + x) * tile
        dy = (oy + y) * tile
        expected = (dx - HULL_RIM, dy - HULL_RIM, dx + tile + HULL_RIM, dy + tile + 10)
        self.assertIn(expected, rects)

    def test_chip_path_is_here_parents_tauri(self):
        self.assertEqual(
            CHIP,
            HERE.parents[1] / "tauri-app" / "src" / "app" / "components" / "sprites" / "AgentLevelChip.svelte",
        )
        self.assertTrue(CHIP.is_file(), CHIP)
        self.assertTrue(STATION.is_file(), STATION)

    def test_chip_contract_paints_lv_unconditionally(self):
        src = CHIP.read_text(encoding="utf-8")
        self.assertIn("Always paint \"Lv N\"", src)
        self.assertIn("<span class=\"ag-n\">Lv {lv}</span>", src)
        stripped = re.sub(r"\{#if label\}.*?\{/if\}", "", src, flags=re.S)
        self.assertIn("Lv {lv}", stripped)
        self.assertIn("max-width: 64px", src)

    def test_primary_only_still_builds(self):
        deck = build_deck(["bed", "tor"], [], self.data)
        kinds = {r.kind for r in deck.rooms if r.owner == "primary" and r.kind != "hall"}
        self.assertEqual(kinds, {"bed", "tor"})
        self.assertTrue(in_deck(deck, deck.rooms[1].x1, deck.rooms[1].y1))
        self.assertGreater(deck.ox, 1)
        self.assertGreaterEqual(deck.st_cols, 24)
        self.assertLessEqual(deck.st_cols, 28)
        self.assertFalse(any(r.kind == "hold" for r in deck.rooms))

    def test_clipped_props_stay_inside_the_owning_room(self):
        rooms = {r.id: r for r in self.deck.rooms}
        for ov in self.deck.overlays:
            room = rooms.get(ov.room)
            self.assertIsNotNone(room, ov.room)
            clipped = clip_prop_to_room(ov, room, self.deck.ox, self.deck.oy, self.deck.tile, self.deck.dump_t)
            if clipped is None:
                continue
            rx, ry, rw, rh = room.pixels(self.deck.ox, self.deck.oy, self.deck.tile)
            cx, cy, cw, ch = clipped
            self.assertGreaterEqual(cx, rx - 0.01)
            self.assertGreaterEqual(cy, ry - 0.01)
            self.assertLessEqual(cx + cw, rx + rw + 0.01)
            self.assertLessEqual(cy + ch, ry + rh + 0.01)

    def test_level_chip_always_paints_lv(self):
        chip = CHIP.read_text(encoding="utf-8")
        self.assertIn("Lv {lv}", chip)
        named_block = chip.split("{#if label}")[1].split("{/if}")[0]
        self.assertNotIn("Lv {lv}", named_block)
        after_name = chip.split("{/if}", 1)[1]
        self.assertIn("Lv {lv}", after_name)

    def test_chip_sits_outside_the_transformed_sprite(self):
        src = STATION.read_text(encoding="utf-8")
        self.assertGreaterEqual(src.count('class="walker-slot"'), 2)
        self.assertEqual(src.count("<AgentLevelChip"), 2)
        self.assertIn(".walker-slot :global(.ag-lv)", src)
        slot_css = src.split(".walker-slot :global(.ag-lv)")[1].split(".walker-slot :global(.ag-n)")[0]
        self.assertIn("bottom: 100%", slot_css)
        self.assertIn("overflow: visible", src.split(".walker-slot {")[1].split(".walker-slot.sit")[0])
        walker_inner = src.split('class="walker"')[1].split("</div>")[0]
        self.assertNotIn("AgentLevelChip", walker_inner)
        for chunk in src.split('class="walker-slot"')[1:]:
            chip_at = chunk.find("<AgentLevelChip")
            walker_at = chunk.find('class="walker"')
            self.assertNotEqual(chip_at, -1)
            self.assertNotEqual(walker_at, -1)
            self.assertLess(chip_at, walker_at, "chip must be a walker-slot sibling, not inside .walker")
        chip = CHIP.read_text(encoding="utf-8")
        self.assertIn("overflow: visible", chip)

    def test_svelte_clips_props_to_the_owning_room(self):
        src = STATION.read_text(encoding="utf-8")
        self.assertIn('class="prop-clip"', src)
        self.assertIn("function pctPropClip", src)
        self.assertIn("function pctPropInRoom", src)
        clip_css = src.split(".prop-clip {")[1].split(".prop {")[0]
        self.assertTrue("overflow: hidden" in clip_css or "overflow: clip" in clip_css)
        self.assertIn("clip-path: inset(0)", clip_css)
        prop_css = src.split("\n  .prop {")[1].split("}")[0]
        self.assertIn("object-fit: fill", prop_css)
        self.assertNotIn("object-fit: contain", prop_css)
        self.assertNotIn("object-position: bottom", prop_css)

    def test_isometric_negative_by_clips_at_room_north(self):
        rooms = {r.id: r for r in self.deck.rooms}
        hanging = 0
        for ov in self.deck.overlays:
            if ov.by >= -0.01:
                continue
            room = rooms[ov.room]
            rx, ry, rw, rh = room_clip_rect(room, self.deck.ox, self.deck.oy, self.deck.tile)
            raw = prop_sprite_rect(ov, self.deck.ox, self.deck.oy, self.deck.tile, self.deck.dump_t)
            if raw[1] >= ry:
                continue
            hanging += 1
            clipped = clip_prop_to_room(ov, room, self.deck.ox, self.deck.oy, self.deck.tile, self.deck.dump_t)
            self.assertIsNotNone(clipped, ov.file)
            cx, cy, cw, ch = clipped
            self.assertGreaterEqual(cy, ry - 0.01, ov.file)
            self.assertLessEqual(cx + cw, rx + rw + 0.01, ov.file)
            self.assertLessEqual(cy + ch, ry + rh + 0.01, ov.file)
        self.assertGreater(hanging, 0)

    def test_primary_xp_does_not_borrow_focused_crew(self):
        src = STATION.read_text(encoding="utf-8")
        self.assertNotIn("isFocusedOwner(owner) ? submissions", src)
        self.assertIn("crewSubmissions[PRIMARY_NAAN_ID]", src)
        body = src.split("function bodyXp(", 1)[1].split("let liveRooms", 1)[0]
        self.assertNotIn("isFocusedOwner", body)
        self.assertNotIn("focusedId", body)
        self.assertIn("crewSubmissions[owner]", body)
        self.assertIn("?? submissions", body)
        self.assertIn('bodyXp("primary")', src)
        self.assertIn("bodyXp(w.owner)", src)

    def test_naan_agent_passes_per_body_submissions(self):
        page = HERE.parents[1] / "tauri-app" / "src" / "app" / "routes" / "NaanAgent.svelte"
        src = page.read_text(encoding="utf-8")
        self.assertIn("crewSubmissions={crewSubmissions}", src)
        self.assertIn("engineAgents[c.id]?.submissions", src)

    def test_extra_crew_suites_are_solid(self):
        for i, member in enumerate(self.crew):
            bx, by = campus_origin(i + 1, self.data.view_cols)
            holes = []
            for y in range(by, by + SUITE_H):
                for x in range(bx, bx + SUITE_W):
                    if not in_deck(self.deck, x, y):
                        holes.append((x, y))
            self.assertEqual(holes, [], f"{member.id} swiss cheese: {holes[:12]}")

    def test_no_void_bridge_from_primary_hall(self):
        for x1, y1, x2, y2 in self.deck.halls:
            width = x2 - x1 + 1
            height = y2 - y1 + 1
            if width > 8 and height == 1 and x1 <= 16 and x2 >= 26:
                self.fail(f"void bridge {(x1, y1, x2, y2)}")

    def test_hold_room_for_each_extra_crew(self):
        hold_labels = [l for l in self.deck.labels if l.text == "HOLD"]
        self.assertEqual(len(hold_labels), len(self.crew))
        for member in self.crew:
            holds = [r for r in self.deck.rooms if r.owner == member.id and r.kind == "hold"]
            self.assertEqual(len(holds), 1, member.id)
            hold = holds[0]
            self.assertEqual(hold.name, "HOLD")
            self.assertNotEqual(hold.kind, "hall")
            self.assertGreaterEqual(hold.w, 6)
            cells = self.deck.walk[member.id]
            self.assertTrue(any(hold.contains(*map(int, k.split(","))) for k in cells), member.id)
            # Alien south col1 is HOLD.
            if member.id == "crew-alien":
                bx, by = campus_origin(2, self.data.view_cols)
                self.assertEqual((hold.x1, hold.y1), (bx + 10, by + 7))

    def test_computed_st_rows_grows_for_campus(self):
        self.assertGreater(self.deck.st_rows, 12)
        _, alien_y = campus_origin(2, self.data.view_cols)
        self.assertGreaterEqual(self.deck.st_rows, alien_y + SUITE_H)


if __name__ == "__main__":
    unittest.main()
