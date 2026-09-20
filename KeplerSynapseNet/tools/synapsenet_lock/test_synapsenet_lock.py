#!/usr/bin/env python3
# Grep-locks for C++/Svelte contracts outside harvest/station/pqc packages.
#   python3 -m unittest KeplerSynapseNet/tools/synapsenet_lock/test_synapsenet_lock.py -v
from __future__ import annotations

import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from synapsenet_lock import KEPLER, read_src  # noqa: E402


class CurlFetchRequireSocksTests(unittest.TestCase):
    def test_header_declares_require_socks(self):
        header = read_src("src", "web", "curl_fetch.h")
        self.assertIn("bool requireSocks = false;", header)
        self.assertIn("Fail closed instead of a clearnet curl", header)
        self.assertIn("when Tor is the required path", header)

    def test_impl_refuses_empty_socks_when_required(self):
        impl = read_src("src", "web", "curl_fetch.cpp")
        fn = impl.split("CurlFetchResult curlFetch(")[1].split("FILE* fp")[0]
        self.assertIn("options.requireSocks && options.socksProxyHostPort.empty()", fn)
        self.assertIn('result.error = "socks required"', fn)
        self.assertIn("return result;", fn)


class DefaultSearchConfigBraveTests(unittest.TestCase):
    def test_default_search_config_includes_brave(self):
        src = read_src("src", "web", "search_config.cpp")
        fn = src.split("SearchConfig defaultSearchConfig()")[1].split("bool loadSearchConfig")[0]
        self.assertIn(
            "cfg.clearnetEngines = {SearchEngine::DUCKDUCKGO, SearchEngine::BRAVE};",
            fn,
        )
        self.assertIn("SearchEngine::BRAVE", fn)
        enum = read_src("include", "web", "web.h").split("enum class SearchEngine")[1].split("}")[0]
        self.assertIn("BRAVE", enum)

    def test_sanitize_fallback_also_includes_brave(self):
        src = read_src("src", "web", "search_config.cpp")
        sanit = src.split("void sanitizeSearchConfig")[1].split("loadConfigFromFile")[0]
        self.assertIn(
            "cfg.clearnetEngines = {SearchEngine::DUCKDUCKGO, SearchEngine::BRAVE};",
            sanit,
        )


class NaanStationBodyXpTests(unittest.TestCase):
    def test_body_xp_does_not_use_focused_submissions_for_other_bodies(self):
        station = (
            KEPLER
            / "tauri-app"
            / "src"
            / "app"
            / "components"
            / "sprites"
            / "NaanStation.svelte"
        )
        src = station.read_text(encoding="utf-8")
        body = src.split("function bodyXp(", 1)[1].split("let liveRooms", 1)[0]
        self.assertNotIn("isFocusedOwner", body)
        self.assertNotIn("focusedId", body)
        self.assertNotIn("isFocusedOwner(owner) ? submissions", src)
        self.assertIn("crewSubmissions[PRIMARY_NAAN_ID]", body)
        self.assertIn("crewSubmissions[owner]", body)
        self.assertIn('heroXp = poseClock >= 0 ? bodyXp("primary") : bodyXp("primary")', src)
        self.assertIn("const lv = bodyXp(w.owner);", src)

    def test_naan_agent_feeds_crew_submissions_into_the_station(self):
        src = read_src("tauri-app", "src", "app", "routes", "NaanAgent.svelte")
        self.assertIn("crewSubmissions={crewSubmissions}", src)
        self.assertIn("engineAgents[c.id]?.submissions", src)


class NaanHoldStashTests(unittest.TestCase):
    def test_hold_stash_component_exists(self):
        src = read_src("tauri-app", "src", "app", "components", "naan", "NaanHoldStash.svelte")
        self.assertIn("HOLD", src)
        self.assertIn("STASH", src)
        self.assertIn('aria-label="HOLD stash"', src)
        self.assertIn("Math.min", src)
        self.assertIn("+{", src)
        self.assertIn("/station/props/crate.png", src)
        self.assertIn("/station/props/industrial_locker.png", src)

    def test_naan_agent_renders_hold_stash(self):
        src = read_src("tauri-app", "src", "app", "routes", "NaanAgent.svelte")
        self.assertIn("NaanHoldStash", src)
        self.assertIn("import NaanHoldStash from", src)
        self.assertIn("<NaanHoldStash", src)


if __name__ == "__main__":
    unittest.main()
