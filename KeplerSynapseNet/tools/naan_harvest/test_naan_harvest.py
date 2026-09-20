#!/usr/bin/env python3
# Harvest planner tests. Run:
#   python3 -m unittest KeplerSynapseNet/tools/naan_harvest/test_naan_harvest.py
from __future__ import annotations

import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from plan import (
    CLEARNET_ENGINES,
    DARKNET_ENGINES,
    JUNK_NEEDLES,
    LoopAction,
    decide_loop,
    extract_hrefs,
    is_junk_html,
    is_search_surface,
    pick_target,
)

KEPLER = HERE.parents[1]


class NaanHarvestPlanTests(unittest.TestCase):
    def test_user_stop_is_the_only_hard_exit(self):
        self.assertEqual(decide_loop(True, False, 0, 100), LoopAction.STOP_USER)
        self.assertEqual(decide_loop(False, True, 0, 100), LoopAction.WAIT_TOPIC)
        self.assertEqual(decide_loop(False, False, 100, 100), LoopAction.WAIT_BUDGET)
        self.assertEqual(decide_loop(False, False, 0, 100), LoopAction.FETCH)

    def test_ai_clearnet_is_not_forced_onto_ahmia(self):
        ahmia = "juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion"
        urls = [pick_target("AI", tick).url for tick in range(12)]
        self.assertTrue(any("arxiv.org" in u or "duckduckgo.com" in u or "brave.com" in u or "wikipedia.org" in u for u in urls))
        self.assertFalse(any(ahmia in u for u in urls))

    def test_crypto_direct_page_stays_https(self):
        t = pick_target("crypto", 0)
        self.assertTrue(t.url.startswith("https://arxiv.org"))
        self.assertFalse(t.onion)

    def test_onion_topics_rotate_more_than_one_engine(self):
        engines = {pick_target("darknet search", tick).engine for tick in range(12)}
        self.assertGreaterEqual(len(engines), 4)
        urls = [pick_target("tor hidden services", tick).url for tick in range(len(DARKNET_ENGINES))]
        self.assertEqual(len(set(urls)), len(DARKNET_ENGINES))

    def test_clearnet_roster_has_duckduckgo_and_brave(self):
        blob = " ".join(CLEARNET_ENGINES)
        self.assertIn("duckduckgo.com", blob)
        self.assertIn("search.brave.com", blob)
        self.assertIn("wikipedia.org", blob)
        self.assertIn("arxiv.org", blob)
        self.assertEqual(len(CLEARNET_ENGINES), 4)
        self.assertEqual(
            CLEARNET_ENGINES,
            (
                "https://html.duckduckgo.com/html/?q=",
                "https://search.brave.com/search?q=",
                "https://en.wikipedia.org/w/index.php?search=",
                "https://arxiv.org/search/?query=",
            ),
        )

    def test_dread_queue_is_junk(self):
        html = (
            "<html><body>dread Access QueuereadYou have been placed in a queue, "
            "awaiting forwarding to the platform.Your estimated entry time is soon"
            "</body></html>"
        )
        self.assertTrue(is_junk_html(html))
        self.assertTrue(is_junk_html("tiny"))
        self.assertFalse(is_junk_html("<html>" + ("arxiv paper title " * 20) + "</html>"))

    def test_engine_cpp_no_longer_funnels_https_to_ahmia(self):
        text = (KEPLER / "src" / "ide" / "synapsed_engine.cpp").read_text(encoding="utf-8")
        self.assertNotIn('sources == "tor" && url.find(".onion")', text)
        self.assertIn("pickNaanHarvestTarget", text)
        self.assertIn("decideNaanHarvestLoop", text)
        self.assertIn("isJunkHarvestHtml", text)
        header = (KEPLER / "include" / "core" / "naan_harvest_plan.h").read_text(encoding="utf-8")
        self.assertIn("not a reason to collapse every", header)
        plan = (KEPLER / "src" / "core" / "naan_harvest_plan.cpp").read_text(encoding="utf-8")
        pick = plan.split("NaanHarvestTarget pickNaanHarvestTarget")[1].split("bool isJunkHarvestHtml")[0]
        self.assertNotIn("juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion", pick)
        self.assertIn("naanClearnetSearchEngines()", pick)

    def test_search_page_hrefs_skip_the_engine_ui(self):
        html = '''<a href="https://html.duckduckgo.com/html/?q=x">ddg</a>
        <a href="https://arxiv.org/abs/2401.00001">paper</a>
        <a href="/relative">no</a>
        <a href="https://search.brave.com/search?q=x">brave</a>
        <a href="https://en.wikipedia.org/wiki/Tor">wiki</a>'''
        hrefs = extract_hrefs(html)
        self.assertIn("https://arxiv.org/abs/2401.00001", hrefs)
        self.assertIn("https://en.wikipedia.org/wiki/Tor", hrefs)
        self.assertFalse(any("duckduckgo.com" in u for u in hrefs))
        self.assertFalse(any("brave.com/search" in u for u in hrefs))

    def test_ahmia_javascript_stub_is_junk(self):
        html = (
            "<html><body>" + ("x" * 80)
            + "This site requires Javascript. We have not deployd non-javascript "
            + "version of this site. Please enable javascript to use Ahmia."
            "</body></html>"
        )
        self.assertTrue(is_junk_html(html))
        self.assertIn("please enable javascript to use ahmia", JUNK_NEEDLES)
        self.assertIn("have not deployd non-javascript", JUNK_NEEDLES)
        self.assertIn("this site requires javascript", JUNK_NEEDLES)
        plan = (KEPLER / "src" / "core" / "naan_harvest_plan.cpp").read_text(encoding="utf-8")
        junk = plan.split("bool isJunkHarvestHtml")[1].split("extractHarvestHrefs")[0]
        self.assertIn("please enable javascript to use ahmia", junk)
        self.assertIn("have not deployd non-javascript", junk)
        self.assertIn("this site requires javascript", junk)

    def test_engine_does_not_file_cve_stub_as_harvest(self):
        text = (KEPLER / "src" / "ide" / "synapsed_engine.cpp").read_text(encoding="utf-8")
        self.assertNotIn("return confusionResult;", text)
        self.assertNotIn("return powReplay;", text)
        self.assertIn("isJunkHarvestHtml(html)", text)
        start = text.find(
            "std::string SynapsedEngine::fetchViaTor(const std::string& url, const std::string& cookieTag)"
        )
        self.assertNotEqual(start, -1)
        fetch = text[start : text.find("SynapsedEngine::CaptchaResult", start)]
        self.assertIn("if (socksPort <= 0) return \"\";", fetch)
        self.assertIn("--socks5-hostname", fetch)

    def test_engine_does_not_rewrite_search_mode_to_tor_string(self):
        text = (KEPLER / "src" / "ide" / "synapsed_engine.cpp").read_text(encoding="utf-8")
        self.assertIn("Intentionally empty", text)
        self.assertNotIn('lines.push_back("naan_auto_search_mode=" + cfgSources_)', text)

    def test_example_conf_uses_both_and_brave(self):
        text = (KEPLER / "data" / "naan_agent_web.conf.example").read_text(encoding="utf-8")
        self.assertIn("clearnet_engines=duckduckgo,brave", text)
        self.assertIn("naan_auto_search_mode=both", text)
        self.assertNotIn("naan_auto_search_mode=tor", text)

    def test_shared_topics_do_not_serialize_crew(self):
        text = (KEPLER / "src" / "core" / "naan_task_share.cpp").read_text(encoding="utf-8")
        start = text.find("bool NaanTaskShare::claimTopic")
        end = text.find("void NaanTaskShare::releaseTopic")
        body = text[start:end]
        self.assertIn("Topics are a shared queue", body)
        self.assertIn("return true;", body)
        self.assertNotIn("topicOwner_", body)
        header = (KEPLER / "include" / "core" / "naan_task_share.h").read_text(encoding="utf-8")
        self.assertIn("Topics are shared work", header)

    def test_cpp_clearnet_engines_lock_ddg_brave_wiki_arxiv(self):
        plan = (KEPLER / "src" / "core" / "naan_harvest_plan.cpp").read_text(encoding="utf-8")
        roster = plan.split("naanClearnetSearchEngines()")[1].split("naanDarknetSearchEngines()")[0]
        for url in CLEARNET_ENGINES:
            self.assertIn(url, roster)
        self.assertIn("not a single Ahmia funnel", roster)

    def test_search_surfaces_are_not_knowledge_pages(self):
        self.assertTrue(is_search_surface(pick_target("AI", 0).url))
        self.assertTrue(is_search_surface("https://html.duckduckgo.com/html/?q=ai"))
        self.assertTrue(is_search_surface("https://search.brave.com/search?q=ai"))
        self.assertTrue(is_search_surface(
            "http://duckduckgogg42xjoc72x3sjasowoarfbgcmvfimaftt6twagswzczad.onion/?q=ai"
        ))
        self.assertFalse(is_search_surface("https://arxiv.org/abs/2401.00001"))
        self.assertFalse(is_search_surface("https://en.wikipedia.org/wiki/Tor"))

    def test_relative_result_hrefs_resolve_off_search_pages(self):
        html = (
            '<a href="/abs/2401.00001">paper</a>'
            '<a href="https://html.duckduckgo.com/html/?q=x">ddg</a>'
            '<a href="/wiki/Tor">wiki</a>'
        )
        arxiv = extract_hrefs(html, 8, "https://arxiv.org/list/cs.AI/recent")
        self.assertIn("https://arxiv.org/abs/2401.00001", arxiv)
        self.assertFalse(any("duckduckgo.com" in u for u in arxiv))
        wiki = extract_hrefs(
            '<a href="/wiki/Tor">t</a>', 8, "https://en.wikipedia.org/w/index.php?search=tor"
        )
        self.assertIn("https://en.wikipedia.org/wiki/Tor", wiki)

    def test_ddg_only_conf_gets_brave_without_wiping_queries(self):
        cfg = (KEPLER / "src" / "web" / "search_config.cpp").read_text(encoding="utf-8")
        self.assertIn("cfg.clearnetEngines = {SearchEngine::DUCKDUCKGO, SearchEngine::BRAVE}", cfg)
        self.assertIn("Leftover duckduckgo-only user confs", cfg)
        self.assertIn("Do not rewrite naan_auto_search_queries", cfg)
        self.assertIn("if (cfg.naanAutoSearchQueries.empty())", cfg)
        engine = (KEPLER / "src" / "ide" / "synapsed_engine.cpp").read_text(encoding="utf-8")
        self.assertIn("Queries and topics stay put", engine)
        self.assertIn("clearnet_engines=", engine)
        self.assertNotIn('lines.push_back("naan_auto_search_mode=" + cfgSources_)', engine)

    def test_engine_keeps_walking_after_duplicate_or_poe_similar(self):
        text = (KEPLER / "src" / "ide" / "synapsed_engine.cpp").read_text(encoding="utf-8")
        self.assertIn("isHarvestSearchSurface", text)
        self.assertIn("too_similar / near_duplicate", text)
        self.assertIn("Same catalog title is a skip, not a freeze", text)
        self.assertIn("extractHarvestHrefs(html, 8, url)", text)
        self.assertNotIn("return confusionResult;", text)
        self.assertNotIn("return powReplay;", text)


if __name__ == "__main__":
    unittest.main()
