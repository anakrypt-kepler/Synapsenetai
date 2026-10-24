#!/usr/bin/env python3
"""Hostname gate for the mining runner. Does not fetch anything."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from naan_mining_runner import TOPIC_URLS, http_hostname, is_onion_http_url

V2 = "abcdefghijklmnop"
V3 = "a" * 56


class MiningHostTests(unittest.TestCase):
    def test_listed_onion_hosts_keep_their_scheme(self):
        saw_http = False
        for urls in TOPIC_URLS.values():
            for url in urls:
                host = http_hostname(url)
                if host.endswith(".onion"):
                    self.assertTrue(is_onion_http_url(url))
                    self.assertTrue(url.startswith("http://") or url.startswith("https://"))
                    if url.startswith("http://"):
                        saw_http = True
                        self.assertFalse(url.startswith("https://"))
                else:
                    self.assertFalse(is_onion_http_url(url))
        self.assertTrue(saw_http)

    def test_substring_is_not_the_hostname(self):
        self.assertTrue(is_onion_http_url(f"http://{V2}.onion/path"))
        self.assertTrue(is_onion_http_url(f"https://www.{V3}.onion/path"))
        self.assertEqual(http_hostname(f"http://User:secret@{V2}.onion/x"), f"{V2}.onion")
        self.assertFalse(is_onion_http_url(f"https://{V3}.onion.example/path"))
        self.assertFalse(is_onion_http_url(f"http://{V3}.onion@example/path"))
        self.assertFalse(is_onion_http_url(f"https://example/a/{V3}.onion"))
        self.assertFalse(is_onion_http_url(f"https://example/search?q={V3}.onion"))
        self.assertFalse(is_onion_http_url(f"http://notonion/{V2}"))
        self.assertFalse(is_onion_http_url(""))
        self.assertFalse(is_onion_http_url("not a url"))


if __name__ == "__main__":
    unittest.main()
