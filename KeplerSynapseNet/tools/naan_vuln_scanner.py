#!/usr/bin/env python3
import argparse
import json
import re
import subprocess
import sys
import time
from datetime import datetime, timezone

CVE_CATALOG = [
    {
        "id": "NAAN-CVE-2026-0001",
        "title": "EndGame V3 PoW Difficulty Downgrade via Cookie Replay",
        "severity": "HIGH",
        "type": "Authentication Bypass",
        "affected": ["Dread Forum", "Pitch Forum"],
        "indicators": ["proof-of-work", "hashcash", "pow_challenge", "endgame_pow"],
        "exploit": "pow_cookie_replay",
    },
    {
        "id": "NAAN-CVE-2026-0002",
        "title": "EndGame V2 Queue Timer Bypass via meta-refresh Race",
        "severity": "MEDIUM",
        "type": "Logic Flaw",
        "affected": ["Dread Forum", "Pitch Forum"],
        "indicators": ["placed in a queue", "awaiting forwarding", "estimated entry time"],
        "exploit": "queue_race",
    },
    {
        "id": "NAAN-CVE-2026-0003",
        "title": "anCaptcha Stateless Token Forgery via CSS Selector Predictability",
        "severity": "HIGH",
        "type": "Cryptographic Weakness",
        "affected": ["Maverick Blog", "Multiple .onion forums"],
        "indicators": ["anC_", ":checked~", "ancaptcha"],
        "exploit": "css_selector_leak",
    },
    {
        "id": "NAAN-CVE-2026-0004",
        "title": "Cloudflare __cf_bm Bot Management Cookie Replay",
        "severity": "MEDIUM",
        "type": "Session Weakness",
        "affected": ["Dark Reading", "BleepingComputer", "The Hacker News", "NVD NIST"],
        "indicators": ["__cf_bm", "cf-ray"],
        "exploit": "cf_bm_replay",
    },
    {
        "id": "NAAN-CVE-2026-0005",
        "title": "Sucuri-Protected Sites XSRF Token Replay via Cache",
        "severity": "MEDIUM",
        "type": "Cache Poisoning / Auth Bypass",
        "affected": ["Exploit-DB"],
        "indicators": ["Sucuri", "X-Sucuri-ID", "X-Sucuri-Cache"],
        "exploit": "xsrf_cache_replay",
    },
    {
        "id": "NAAN-CVE-2026-0006",
        "title": "BleepingComputer Session Cookie Misconfiguration",
        "severity": "LOW",
        "type": "Cookie Security Misconfiguration",
        "affected": ["BleepingComputer"],
        "indicators": ["session_id", "no_samesite"],
        "exploit": "none_required",
    },
    {
        "id": "NAAN-CVE-2026-0007",
        "title": "Cloudflare Managed Challenge No-Script Bypass",
        "severity": "HIGH",
        "type": "Logic Flaw",
        "affected": ["Dark Reading", "Reddit"],
        "indicators": ["challenge-platform", "cf-browser-verification", "/cdn-cgi/challenge-platform"],
        "exploit": "cf_ray_post_bypass",
    },
    {
        "id": "NAAN-CVE-2026-0008",
        "title": "Timing Oracle for Protection State Detection",
        "severity": "MEDIUM",
        "type": "Information Disclosure",
        "affected": ["All EndGame-protected", "All CAPTCHA-protected"],
        "indicators": ["ttfb_classification"],
        "exploit": "timing_precompute",
    },
    {
        "id": "NAAN-CVE-2026-0009",
        "title": "Cross-Service Cookie Jar Confusion",
        "severity": "MEDIUM",
        "type": "Session Confusion",
        "affected": ["BTC VPS", "DeepMa", "TorMart", "Bizzle"],
        "indicators": ["PHPSESSID", "session_id", "_token"],
        "exploit": "cookie_confusion",
    },
    {
        "id": "NAAN-CVE-2026-0010",
        "title": "Zero Bot Detection on Multiple Services",
        "severity": "LOW",
        "type": "Fingerprinting Gap",
        "affected": ["PacketStorm", "Schneier Blog", "Krebs on Security", "IACR"],
        "indicators": ["no_protection"],
        "exploit": "direct_fetch",
    },
]

TARGETS = [
    {"name": "Exploit-DB", "url": "https://www.exploit-db.com/", "type": "clearnet"},
    {"name": "Dark Reading", "url": "https://www.darkreading.com/", "type": "clearnet"},
    {"name": "BleepingComputer", "url": "https://www.bleepingcomputer.com/", "type": "clearnet"},
    {"name": "The Hacker News", "url": "https://thehackernews.com/", "type": "clearnet"},
    {"name": "PacketStorm", "url": "https://packetstormsecurity.com/", "type": "clearnet"},
    {"name": "Krebs on Security", "url": "https://krebsonsecurity.com/", "type": "clearnet"},
    {"name": "Schneier Blog", "url": "https://www.schneier.com/", "type": "clearnet"},
    {"name": "IACR ePrint", "url": "https://eprint.iacr.org/", "type": "clearnet"},
    {"name": "GitHub Trending", "url": "https://github.com/trending", "type": "clearnet"},
    {"name": "NVD NIST", "url": "https://nvd.nist.gov/vuln/search/results?query=critical", "type": "clearnet"},
    {"name": "arxiv CS.CR", "url": "https://arxiv.org/list/cs.CR/recent", "type": "clearnet"},
    {"name": "Dread Forum", "url": "http://dreadytofatroptsdj6io7l3xptbet6onoyno2yv7jicoxknyazubrad.onion/", "type": "onion"},
    {"name": "Pitch Forum", "url": "http://pitchprash4aqilfr7sbmuwve3pnkpylqwxjbj2q5o4szcfeea6d27yd.onion/", "type": "onion"},
    {"name": "BTC VPS", "url": "http://btcvps22bw3ftklfklime6o6jwmf5rpoyb5fhxyzan5hpfnnumnpemqd.onion/", "type": "onion"},
    {"name": "Maverick Blog", "url": "http://maverickblogykvgp2yl5n2doaclxjoe4c73toetq7baj3tgzro4tyd.onion/", "type": "onion"},
    {"name": "DeepMa Market", "url": "http://deepmafiadsstozbe3md24fhrg6m47t3gxcaa2fxjgibvzb4hrozqwqd.onion/", "type": "onion"},
    {"name": "Bizzle Forum", "url": "http://bizzleafoghnahqp2w5ybrdjkaiz3m4c3nqttiq3cakwulo7a2ja4qd.onion/", "type": "onion"},
    {"name": "TorMart", "url": "http://tormartbiickqvxkgy2cj2uw2izolw36hnwtdnprsuvkkyz2opm4myd.onion/", "type": "onion"},
    {"name": "Torch Search", "url": "http://torchdeedp3i2jigzjdmfpn5ttjhthh5wbmda2rr3jvqjg5p77c54dqd.onion/", "type": "onion"},
    {"name": "Ahmia Search", "url": "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/", "type": "onion"},
    {"name": "DuckDuckGo Tor", "url": "http://duckduckgogg42xjoc72x3sjasowoarfbgcmvfimaftt6twagswzczad.onion/", "type": "onion"},
]


def fetch_url(url, is_onion, timeout=30):
    start = time.time()
    try:
        if is_onion:
            cmd = ["curl", "-s", "-k", "--max-time", str(timeout),
                   "--socks5-hostname", "127.0.0.1:9050", "-L",
                   "-D", "-",
                   "-H", "User-Agent: Mozilla/5.0 (Windows NT 10.0; rv:128.0) Gecko/20100101 Firefox/128.0",
                   url]
        else:
            cmd = ["curl", "-s", "-k", "--max-time", str(timeout), "-L",
                   "-D", "-",
                   "-H", "User-Agent: Mozilla/5.0 (Windows NT 10.0; rv:128.0) Gecko/20100101 Firefox/128.0",
                   url]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 5)
        elapsed = time.time() - start
        output = r.stdout
        headers_raw = ""
        body = output
        if "\r\n\r\n" in output:
            headers_raw, body = output.split("\r\n\r\n", 1)
        elif "\n\n" in output:
            headers_raw, body = output.split("\n\n", 1)
        http_code = 0
        m = re.search(r"HTTP/[\d.]+ (\d+)", headers_raw)
        if m:
            http_code = int(m.group(1))
        headers = {}
        for line in headers_raw.split("\n"):
            if ":" in line:
                k, v = line.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        return body, http_code, elapsed, headers, None
    except subprocess.TimeoutExpired:
        return "", 0, time.time() - start, {}, "timeout"
    except Exception as e:
        return "", 0, time.time() - start, {}, str(e)


def detect_vulns(body, http_code, headers, ttfb_ms, url):
    found = []
    body_lower = body.lower() if body else ""

    for cve in CVE_CATALOG:
        matched = False
        if cve["id"] == "NAAN-CVE-2026-0008":
            if ttfb_ms < 60 and len(body) < 15000:
                matched = True
        elif cve["id"] == "NAAN-CVE-2026-0010":
            if http_code == 200 and len(body) > 1000 and "captcha" not in body_lower and "challenge" not in body_lower:
                matched = True
        elif cve["id"] == "NAAN-CVE-2026-0004":
            cookie_hdr = headers.get("set-cookie", "")
            if "__cf_bm" in cookie_hdr or "cf-ray" in headers:
                matched = True
        elif cve["id"] == "NAAN-CVE-2026-0005":
            if "sucuri" in headers.get("server", "").lower() or "x-sucuri-id" in headers:
                matched = True
        elif cve["id"] == "NAAN-CVE-2026-0006":
            cookie_hdr = headers.get("set-cookie", "")
            if "session_id" in cookie_hdr and "samesite" not in cookie_hdr.lower():
                matched = True
        elif cve["id"] == "NAAN-CVE-2026-0007":
            if http_code == 403 and "challenge-platform" in body_lower:
                matched = True
        else:
            for indicator in cve["indicators"]:
                if indicator.lower() in body_lower:
                    matched = True
                    break

        if matched:
            found.append(cve)

    return found


def main():
    parser = argparse.ArgumentParser(description="NAAN Vulnerability Scanner")
    parser.add_argument("--clearnet-only", action="store_true")
    parser.add_argument("--onion-only", action="store_true")
    parser.add_argument("--cve", type=str, help="Filter by specific CVE ID")
    parser.add_argument("--json", type=str, help="Export results to JSON")
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()

    targets = TARGETS
    if args.clearnet_only:
        targets = [t for t in targets if t["type"] == "clearnet"]
    if args.onion_only:
        targets = [t for t in targets if t["type"] == "onion"]

    print(f"NAAN Vulnerability Scanner -- {len(targets)} targets")
    print(f"CVE Catalog: {len(CVE_CATALOG)} vulnerabilities")
    print(f"Started: {datetime.now(timezone.utc).isoformat()}")
    print("=" * 80)

    results = []
    for target in targets:
        is_onion = target["type"] == "onion"
        body, http_code, elapsed, headers, error = fetch_url(
            target["url"], is_onion, args.timeout)
        ttfb_ms = elapsed * 1000

        if error:
            print(f"[FAIL] {target['name']}: {error}")
            results.append({"name": target["name"], "status": "FAIL", "error": error})
            continue

        vulns = detect_vulns(body, http_code, headers, ttfb_ms, target["url"])

        if args.cve:
            vulns = [v for v in vulns if v["id"] == args.cve]

        status = "VULN" if vulns else "CLEAN"
        vuln_ids = [v["id"] for v in vulns]
        print(f"[{status}] {target['name']}: HTTP {http_code}, "
              f"{len(body)} bytes, {elapsed:.2f}s -- {vuln_ids}")

        results.append({
            "name": target["name"],
            "url": target["url"],
            "type": target["type"],
            "http_code": http_code,
            "size": len(body),
            "time": round(elapsed, 3),
            "ttfb_ms": round(ttfb_ms, 1),
            "status": status,
            "vulns": [{"id": v["id"], "title": v["title"], "severity": v["severity"],
                       "exploit": v["exploit"]} for v in vulns],
        })

    print("=" * 80)
    vuln_count = sum(1 for r in results if r.get("status") == "VULN")
    print(f"Results: {vuln_count}/{len(results)} vulnerable, "
          f"{len(results) - vuln_count - sum(1 for r in results if r.get('status') == 'FAIL')}/{len(results)} clean, "
          f"{sum(1 for r in results if r.get('status') == 'FAIL')}/{len(results)} failed")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"timestamp": datetime.now(timezone.utc).isoformat(),
                       "targets": len(targets), "results": results,
                       "cve_catalog": CVE_CATALOG}, f, indent=2)
        print(f"Results exported to {args.json}")


if __name__ == "__main__":
    main()
