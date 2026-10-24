#!/usr/bin/env python3
"""Hostname gate for captcha harvest targets. Does not fetch anything."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from harvest import (
    AHMIA_SEARCH,
    ONION_CAPTCHA_SOURCES,
    TORCH_SEARCH,
    extract_img_src_captcha,
    http_hostname,
    is_onion_http_url,
    onion_urls_from_html,
)

V2 = "abcdefghijklmnop"
V3 = "a" * 56


class HarvestHostTests(unittest.TestCase):
    def test_builtin_sources_are_onion_hosts(self):
        for source in ONION_CAPTCHA_SOURCES:
            self.assertTrue(is_onion_http_url(source["url"]))
        self.assertTrue(is_onion_http_url(AHMIA_SEARCH))
        self.assertTrue(is_onion_http_url(TORCH_SEARCH))

    def test_subdomain_and_port_stay_on_the_onion_host(self):
        self.assertTrue(is_onion_http_url(f"https://www.{V3}.onion/path"))
        self.assertTrue(is_onion_http_url(f"http://{V2}.onion:80/captcha"))
        self.assertEqual(http_hostname(f"http://User:secret@{V2}.onion/x"), f"{V2}.onion")

    def test_substring_is_not_the_hostname(self):
        self.assertFalse(is_onion_http_url(f"https://{V3}.onion.example/path"))
        self.assertFalse(is_onion_http_url(f"http://{V3}.onion@example/path"))
        self.assertFalse(is_onion_http_url(f"https://example/a/{V3}.onion"))
        self.assertFalse(is_onion_http_url(f"https://example/search?q={V3}.onion"))
        self.assertFalse(is_onion_http_url(f"http://notonion/{V2}"))
        self.assertFalse(is_onion_http_url(""))
        self.assertFalse(is_onion_http_url("not a url"))

    def test_html_keeps_only_onion_hostnames(self):
        page = (
            f'<a href="http://{V2}.onion/captcha">ok</a>'
            f'<a href="https://{V3}.onion.example/x">no</a>'
            f'<a href="http://{V2}.onion@example/y">no</a>'
            f'<a href="https://example/q?u=http://{V3}.onion/z">no</a>'
        )
        found = onion_urls_from_html(page)
        self.assertEqual(found, [f"http://{V2}.onion/captcha"])

    def test_captcha_images_stay_on_the_page_host(self):
        base = f"http://user:secret@{V2}.onion/login"
        html = (
            "<html><body>captcha form"
            f'<img src="/captcha.png">'
            f'<img src="//{V2}.onion/other.png">'
            f'<img src="http://{V2}.onion/abs.png">'
            f'<img src="http://{V2}.onion@example/abs.png">'
            f'<img src="https://example/captcha.png">'
            "</body></html>"
        ).encode()
        urls = extract_img_src_captcha(html, base)
        self.assertEqual(
            urls,
            [
                f"http://{V2}.onion/captcha.png",
                f"http://{V2}.onion/other.png",
                f"http://{V2}.onion/abs.png",
            ],
        )


if __name__ == "__main__":
    unittest.main()
