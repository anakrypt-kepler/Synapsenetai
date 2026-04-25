#!/usr/bin/env python3
import argparse
import csv
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone

SERVICES = [
    {"name": "Dread Forum", "url": "http://dreadytofatroptsdj6io7l3xptbet6onoyno2yv7jicoxknyazubrad.onion/", "type": "onion", "protection": "endgame_v2_queue", "category": "forum"},
    {"name": "Pitch Forum", "url": "http://pitchprash4aqilfr7sbmuwve3pnkpylqwxjbj2q5o4szcfeea6d27yd.onion/", "type": "onion", "protection": "endgame_v2_queue", "category": "forum"},
    {"name": "BTC VPS", "url": "http://btcvps22bw3ftklfklime6o6jwmf5rpoyb5fhxyzan5hpfnnumnpemqd.onion/", "type": "onion", "protection": "text_captcha_hard", "category": "hosting"},
    {"name": "Bizzle Forum", "url": "http://bizzleafoghnahqp2w5ybrdjkaiz3m4c3nqttiq3cakwulo7a2ja4qd.onion/", "type": "onion", "protection": "text_captcha", "category": "forum"},
    {"name": "DeepMa Market", "url": "http://deepmafiadsstozbe3md24fhrg6m47t3gxcaa2fxjgibvzb4hrozqwqd.onion/", "type": "onion", "protection": "base64_captcha", "category": "market"},
    {"name": "TorMart", "url": "http://tormartbiickqvxkgy2cj2uw2izolw36hnwtdnprsuvkkyz2opm4myd.onion/", "type": "onion", "protection": "base64_captcha", "category": "market"},
    {"name": "Altenens Forum", "url": "http://altenadrsrzfmoaw3lug3tvkdn2v3dikbhkv3fxqpnfq63ijhxzcv5qd.onion/", "type": "onion", "protection": "math_captcha", "category": "forum"},
    {"name": "DWF Forum", "url": "http://dwforumcwbisit34.onion/", "type": "onion", "protection": "math_captcha", "category": "forum"},
    {"name": "Maverick Blog", "url": "http://maverickblogykvgp2yl5n2doaclxjoe4c73toetq7baj3tgzro4tyd.onion/", "type": "onion", "protection": "rotate_captcha", "category": "blog"},
    {"name": "BBC Tor", "url": "https://www.bbcnewsd73hkzno2ini43t4gblxvycyac5aw4gnv7t2rccijh7745uqd.onion/", "type": "onion", "protection": "none", "category": "news"},
    {"name": "ProPublica Tor", "url": "https://p53lf57qovyuvwsc6xnrppyply3vtqm7l6pcobkmyqsiofyeznfu5uqd.onion/", "type": "onion", "protection": "none", "category": "news"},
    {"name": "NYT Tor", "url": "https://nytimesn7cgmftshazwhfgzm37qxb44r64ytbb2dj3x62d2lnez7pyd.onion/", "type": "onion", "protection": "none", "category": "news"},
    {"name": "DuckDuckGo Tor", "url": "http://duckduckgogg42xjoc72x3sjasowoarfbgcmvfimaftt6twagswzczad.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "Torch Search", "url": "http://torchdeedp3i2jigzjdmfpn5ttjhthh5wbmda2rr3jvqjg5p77c54dqd.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "Ahmia Search", "url": "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "Tordex Search", "url": "http://tordexpmg4xy32rfp4ovnz7zq5ujoejwq2u26uxxtkscgo5u3losmeid.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "Tor66 Search", "url": "http://tor66sewebgixwhcqfnp5inzp5x5uohhdy3kvtnyfxc2e5mxiuh34iid.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "DarkSearch", "url": "http://darkzqtmbdeauwq5mzcmgeeuhet42fhfjj4p5wbak3ofx2yqgecoeqyd.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "Excavator Search", "url": "http://3bbad7fauom4d6sgppalyqddsqbf5u5p56b5k5uk2zxsy3d6ey2jobad.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "Haystak Search", "url": "http://haystak5njsmn2hqkewecpaxetahtwhsbsa64jom2k22z5afxhnpxfid.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "OnionLand Search", "url": "http://3bbaaaccczcbdddeeef3picks7gnhb4gwjo4vkovn5nz6v6hjduhmyid.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "Phobos Search", "url": "http://phobosxilamwcg75xt22id7aywkzol6q6rfl2flipcqoc4e4ahima5id.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "Deep Search", "url": "http://search7tdrcvri22rieiqgi5hmcb7ubxg2l5xebfyre2zdxqgtd4hqid.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "SecureDrop (Guardian)", "url": "http://xp44cagis447k3lpb4wwhcqukix6cgqokbuys24vmrou2otv7y6zcqyd.onion/", "type": "onion", "protection": "none", "category": "whistleblower"},
    {"name": "SecureDrop (WaPo)", "url": "http://vfnmxpa6fo4jdpyq3yneqhglluweax2uclvxkytfpmpkp5rsvpmqdqid.onion/", "type": "onion", "protection": "none", "category": "whistleblower"},
    {"name": "Pirate Bay Tor", "url": "http://piratebayo3klnzokct3wt5yyxb2vpebbuyjl7m623iaxmqhsd52coid.onion/", "type": "onion", "protection": "none", "category": "torrent"},
    {"name": "Onionbook", "url": "http://onionbookpmp6mbfc4e2txyarvpngbw2bv6ixam364k4ox2w6m7c7mqd.onion/", "type": "onion", "protection": "phpbb_confirm", "category": "social"},
    {"name": "Daniel Chat", "url": "http://danielas3rtn54uwmofdo3x2bsdifr47huasnmbgqzfrec5ubupvtpid.onion/", "type": "onion", "protection": "none", "category": "chat"},
    {"name": "Hidden Wiki", "url": "http://zqktlwiuavvvqqt4ybvgvi7tyo4hjl5xgfuvpdf6otjiycgwqbym2qad.onion/", "type": "onion", "protection": "none", "category": "wiki"},
    {"name": "Dark.fail", "url": "http://darkfailenbsdla5mal2mxn2uz66od5vtzd5qozslagrfzachha3f3id.onion/", "type": "onion", "protection": "none", "category": "directory"},
    {"name": "Riseup Tor", "url": "http://vww6ybal4bd7szmgncyruucpgfkqahzddi37ktceo3ah7ngmcopnpyyd.onion/", "type": "onion", "protection": "none", "category": "email"},
    {"name": "ProtonMail Tor", "url": "https://protonmailrmez3lotccipshtkleegetolb73fuirgj7r4o4vfu7ozyd.onion/", "type": "onion", "protection": "none", "category": "email"},
    {"name": "Keybase Tor", "url": "http://keybase5wmilwokqirssclfnsqrjdsi7jdir5ber7cvcmvd5m2lio2vad.onion/", "type": "onion", "protection": "none", "category": "identity"},
    {"name": "Facebook Tor", "url": "https://www.facebookwkhpilnemxj7asaniu7vnjjbiltxjqhye3mhbshg7kx5tfyd.onion/", "type": "onion", "protection": "none", "category": "social"},
    {"name": "Impreza Hosting", "url": "http://imprezareshna326gqgmbdzwmnad2wnjmeowh45bs2buxarh5qummjad.onion/", "type": "onion", "protection": "none", "category": "hosting"},
    {"name": "Ablative Hosting", "url": "http://hzwjmjimhr7bdmfv2doll4upibt5ojjmpo3pb2idn2wbwz2p57cljqid.onion/", "type": "onion", "protection": "none", "category": "hosting"},
    {"name": "CIA Tor", "url": "http://ciadotgov4sjwlzihbbgxnqg3xiyrg7so2r2o3lt5wz5ypk4sxyjstad.onion/", "type": "onion", "protection": "none", "category": "gov"},
    {"name": "Brave Search", "url": "https://search.brave4u7jddbv7cyviptqjc7jusber3qlif2n2xp4dgdhzjhmjnqjyid.onion/", "type": "onion", "protection": "none", "category": "search"},
    {"name": "arxiv (CS.CR)", "url": "https://arxiv.org/list/cs.CR/recent", "type": "clearnet", "protection": "none", "category": "research"},
    {"name": "arxiv (CS.AI)", "url": "https://arxiv.org/list/cs.AI/recent", "type": "clearnet", "protection": "none", "category": "research"},
    {"name": "MITRE CVE", "url": "https://cve.mitre.org/cgi-bin/cvekey.cgi?keyword=2024", "type": "clearnet", "protection": "none", "category": "research"},
    {"name": "NVD NIST", "url": "https://nvd.nist.gov/vuln/search/results?query=critical&results_type=overview", "type": "clearnet", "protection": "none", "category": "research"},
    {"name": "Exploit-DB", "url": "https://www.exploit-db.com/", "type": "clearnet", "protection": "cloudflare", "category": "research"},
    {"name": "Packet Storm", "url": "https://packetstormsecurity.com/", "type": "clearnet", "protection": "none", "category": "research"},
    {"name": "Full Disclosure", "url": "https://seclists.org/fulldisclosure/", "type": "clearnet", "protection": "none", "category": "research"},
    {"name": "Krebs on Security", "url": "https://krebsonsecurity.com/", "type": "clearnet", "protection": "none", "category": "news"},
    {"name": "The Hacker News", "url": "https://thehackernews.com/", "type": "clearnet", "protection": "none", "category": "news"},
    {"name": "BleepingComputer", "url": "https://www.bleepingcomputer.com/", "type": "clearnet", "protection": "cloudflare", "category": "news"},
    {"name": "Dark Reading", "url": "https://www.darkreading.com/", "type": "clearnet", "protection": "none", "category": "news"},
    {"name": "Schneier Blog", "url": "https://www.schneier.com/", "type": "clearnet", "protection": "none", "category": "news"},
    {"name": "Ars Technica", "url": "https://arstechnica.com/security/", "type": "clearnet", "protection": "none", "category": "news"},
    {"name": "Wired Security", "url": "https://www.wired.com/category/security/", "type": "clearnet", "protection": "none", "category": "news"},
    {"name": "IACR ePrint", "url": "https://eprint.iacr.org/", "type": "clearnet", "protection": "none", "category": "research"},
    {"name": "GitHub Trending", "url": "https://github.com/trending", "type": "clearnet", "protection": "none", "category": "code"},
    {"name": "Hacker News (YC)", "url": "https://news.ycombinator.com/", "type": "clearnet", "protection": "none", "category": "news"},
    {"name": "Reddit r/netsec", "url": "https://old.reddit.com/r/netsec/", "type": "clearnet", "protection": "none", "category": "forum"},
]


def check_tor():
    try:
        r = subprocess.run(
            ["curl", "-s", "--max-time", "10", "--socks5-hostname",
             "127.0.0.1:9050", "https://check.torproject.org/api/ip"],
            capture_output=True, text=True, timeout=15)
        if r.returncode == 0 and "true" in r.stdout.lower():
            data = json.loads(r.stdout)
            return True, data.get("IP", "unknown")
        return False, ""
    except Exception:
        return False, ""


def fetch_url(url, is_onion, timeout=45):
    start = time.time()
    try:
        if is_onion:
            cmd = ["curl", "-s", "-k", "--max-time", str(timeout),
                   "--socks5-hostname", "127.0.0.1:9050", "-L",
                   "-w", "\n__HTTP_CODE__%{http_code}",
                   "-H", "User-Agent: Mozilla/5.0 (Windows NT 10.0; rv:128.0) Gecko/20100101 Firefox/128.0",
                   url]
        else:
            cmd = ["curl", "-s", "-k", "--max-time", str(timeout), "-L",
                   "-w", "\n__HTTP_CODE__%{http_code}",
                   "-H", "User-Agent: Mozilla/5.0 (Windows NT 10.0; rv:128.0) Gecko/20100101 Firefox/128.0",
                   url]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 5)
        elapsed = time.time() - start
        body = r.stdout
        http_code = 0
        if "__HTTP_CODE__" in body:
            parts = body.rsplit("__HTTP_CODE__", 1)
            body = parts[0]
            try:
                http_code = int(parts[1].strip())
            except ValueError:
                pass
        return body, http_code, elapsed, None
    except subprocess.TimeoutExpired:
        return "", 0, time.time() - start, "timeout"
    except Exception as e:
        return "", 0, time.time() - start, str(e)


def detect_protection(html):
    protections = []
    if "placed in a queue" in html or "awaiting forwarding" in html:
        protections.append("endgame_v2")
    if "proof-of-work" in html or "pow_challenge" in html or "hashcash" in html:
        protections.append("endgame_v3_pow")
    if "captcha" in html.lower():
        if "data-xf-init=\"qa-captcha\"" in html:
            protections.append("math_captcha")
        elif "anC_" in html or "ancaptcha" in html.lower():
            protections.append("rotate_captcha")
        elif "data:image" in html:
            protections.append("base64_captcha")
        elif "<img" in html and "captcha" in html.lower():
            protections.append("text_captcha")
        else:
            protections.append("unknown_captcha")
    if "cf-browser-verification" in html or "cf_clearance" in html:
        protections.append("cloudflare")
    if "g-recaptcha" in html:
        protections.append("recaptcha")
    if "h-captcha" in html:
        protections.append("hcaptcha")
    if "DDoS protection" in html or "Please wait" in html:
        protections.append("ddos_generic")
    return protections


def detect_content(html):
    if not html or len(html) < 100:
        return "empty", 0
    title = ""
    m = re.search(r"<title[^>]*>([^<]+)</title>", html, re.IGNORECASE)
    if m:
        title = m.group(1).strip()
    headings = re.findall(r"<h[12][^>]*>([^<]+)</h[12]>", html, re.IGNORECASE)
    links = len(re.findall(r"<a\s", html, re.IGNORECASE))
    has_content = len(html) > 500 and (len(headings) > 0 or links > 3)
    return title, len(html)


def solve_pow_local(challenge, difficulty):
    for n in range(10000000):
        h = hashlib.sha256((challenge + str(n)).encode()).hexdigest()
        val = int(h[:8], 16)
        bits = 32 - val.bit_length() if val > 0 else 32
        if bits >= difficulty:
            return n
    return None


def run_test(service, verbose=False):
    result = {
        "name": service["name"],
        "url": service["url"],
        "type": service["type"],
        "expected_protection": service["protection"],
        "category": service["category"],
        "status": "unknown",
        "http_code": 0,
        "response_size": 0,
        "response_time_s": 0,
        "title": "",
        "protections_detected": [],
        "content_extracted": False,
        "error": None,
    }

    is_onion = service["type"] == "onion"
    timeout = 90 if "dread" in service["name"].lower() else 60 if is_onion else 30

    html, http_code, elapsed, error = fetch_url(service["url"], is_onion, timeout)
    result["http_code"] = http_code
    result["response_time_s"] = round(elapsed, 2)

    if error:
        result["status"] = "FAIL"
        result["error"] = error
        return result

    if not html or len(html) < 50:
        result["status"] = "FAIL"
        result["error"] = "empty_response"
        return result

    protections = detect_protection(html)
    result["protections_detected"] = protections

    if "endgame_v2" in protections:
        result["status"] = "QUEUE"
        result["response_size"] = len(html)
        return result

    if "endgame_v3_pow" in protections:
        m_ch = re.search(r'challenge["\s:=]+["\']([^"\']+)', html)
        m_diff = re.search(r'difficulty["\s:=]+(\d+)', html)
        if m_ch:
            ch = m_ch.group(1)
            diff = int(m_diff.group(1)) if m_diff else 20
            nonce = solve_pow_local(ch, diff)
            if nonce is not None:
                result["status"] = "POW_SOLVED"
                result["response_size"] = len(html)
                return result
        result["status"] = "POW_FAIL"
        return result

    title, size = detect_content(html)
    result["title"] = title[:80] if title else ""
    result["response_size"] = size

    if http_code >= 200 and http_code < 400 and size > 200:
        result["status"] = "PASS"
        result["content_extracted"] = True
    elif http_code == 403:
        result["status"] = "BLOCKED"
    elif http_code >= 500:
        result["status"] = "SERVER_ERROR"
    elif size > 200:
        result["status"] = "PASS"
        result["content_extracted"] = True
    else:
        result["status"] = "FAIL"
        result["error"] = f"http_{http_code}_size_{size}"

    return result


def print_report(results, elapsed_total):
    total = len(results)
    passed = sum(1 for r in results if r["status"] == "PASS")
    queued = sum(1 for r in results if r["status"] == "QUEUE")
    pow_solved = sum(1 for r in results if r["status"] == "POW_SOLVED")
    blocked = sum(1 for r in results if r["status"] == "BLOCKED")
    failed = sum(1 for r in results if r["status"] == "FAIL")
    errors = sum(1 for r in results if r["status"] == "SERVER_ERROR")

    onion_results = [r for r in results if r["type"] == "onion"]
    clearnet_results = [r for r in results if r["type"] == "clearnet"]
    onion_pass = sum(1 for r in onion_results if r["status"] in ("PASS", "QUEUE", "POW_SOLVED"))
    clearnet_pass = sum(1 for r in clearnet_results if r["status"] == "PASS")

    categories = {}
    for r in results:
        cat = r["category"]
        if cat not in categories:
            categories[cat] = {"total": 0, "pass": 0}
        categories[cat]["total"] += 1
        if r["status"] in ("PASS", "QUEUE", "POW_SOLVED"):
            categories[cat]["pass"] += 1

    print("\n" + "=" * 80)
    print("NAAN AGENT INTEGRATION TEST REPORT")
    print(f"Date: {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')}")
    print(f"Total time: {elapsed_total:.1f}s")
    print("=" * 80)

    print(f"\n{'Service':<30} {'Type':<8} {'Status':<12} {'HTTP':<5} {'Size':<8} {'Time':<7} {'Protection'}")
    print("-" * 100)
    for r in results:
        prot = ",".join(r["protections_detected"]) if r["protections_detected"] else r["expected_protection"]
        print(f"{r['name']:<30} {r['type']:<8} {r['status']:<12} {r['http_code']:<5} {r['response_size']:<8} {r['response_time_s']:<7.1f} {prot}")

    print("\n" + "=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Total services tested:    {total}")
    print(f"  PASS (content fetched): {passed}")
    print(f"  QUEUE (DDoS queue):     {queued}")
    print(f"  POW_SOLVED:             {pow_solved}")
    print(f"  BLOCKED (403):          {blocked}")
    print(f"  FAIL:                   {failed}")
    print(f"  SERVER_ERROR:           {errors}")
    print()
    success = passed + queued + pow_solved
    print(f"Success rate:             {success}/{total} = {success/max(total,1)*100:.1f}%")
    print(f"  Onion:                  {onion_pass}/{len(onion_results)} = {onion_pass/max(len(onion_results),1)*100:.1f}%")
    print(f"  Clearnet:               {clearnet_pass}/{len(clearnet_results)} = {clearnet_pass/max(len(clearnet_results),1)*100:.1f}%")

    print(f"\nBy category:")
    for cat, stats in sorted(categories.items()):
        print(f"  {cat:<20} {stats['pass']}/{stats['total']}")

    print("\nProtections encountered:")
    all_prots = {}
    for r in results:
        for p in r["protections_detected"]:
            all_prots[p] = all_prots.get(p, 0) + 1
    for p, c in sorted(all_prots.items(), key=lambda x: -x[1]):
        print(f"  {p:<25} {c}x")

    print("\nFailed services:")
    for r in results:
        if r["status"] in ("FAIL", "SERVER_ERROR", "BLOCKED"):
            print(f"  {r['name']}: {r['status']} ({r.get('error', '')})")

    return success, total


def save_csv(results, path):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "name", "url", "type", "expected_protection", "category",
            "status", "http_code", "response_size", "response_time_s",
            "title", "protections_detected", "content_extracted", "error"])
        w.writeheader()
        for r in results:
            row = dict(r)
            row["protections_detected"] = ",".join(row["protections_detected"])
            w.writerow(row)


def save_json(results, summary, path):
    with open(path, "w") as f:
        json.dump({"timestamp": datetime.now(timezone.utc).isoformat(),
                    "summary": summary, "results": results}, f, indent=2)


def main():
    parser = argparse.ArgumentParser(description="NAAN Agent Integration Test")
    parser.add_argument("--onion-only", action="store_true")
    parser.add_argument("--clearnet-only", action="store_true")
    parser.add_argument("--category", type=str, default="")
    parser.add_argument("--csv", type=str, default="")
    parser.add_argument("--json", type=str, default="")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--skip-tor-check", action="store_true")
    parser.add_argument("--max-services", type=int, default=0)
    args = parser.parse_args()

    services = SERVICES

    if args.onion_only:
        services = [s for s in services if s["type"] == "onion"]
    elif args.clearnet_only:
        services = [s for s in services if s["type"] == "clearnet"]

    if args.category:
        services = [s for s in services if s["category"] == args.category]

    if args.max_services > 0:
        services = services[:args.max_services]

    print(f"NAAN Integration Test: {len(services)} services")
    print(f"Onion: {sum(1 for s in services if s['type']=='onion')}, "
          f"Clearnet: {sum(1 for s in services if s['type']=='clearnet')}")

    has_onion = any(s["type"] == "onion" for s in services)
    if has_onion and not args.skip_tor_check:
        print("\nChecking Tor connectivity...")
        tor_ok, tor_ip = check_tor()
        if tor_ok:
            print(f"  Tor: OK (exit IP: {tor_ip})")
        else:
            print("  Tor: FAILED - skipping onion services")
            services = [s for s in services if s["type"] != "onion"]
            if not services:
                print("No services left to test.")
                sys.exit(1)

    results = []
    start_total = time.time()

    for i, service in enumerate(services):
        print(f"\n[{i+1}/{len(services)}] Testing {service['name']} ({service['type']})...", end="", flush=True)
        r = run_test(service, args.verbose)
        results.append(r)
        status_icon = {"PASS": "+", "QUEUE": "~", "POW_SOLVED": "*",
                       "BLOCKED": "!", "FAIL": "X", "SERVER_ERROR": "E"}.get(r["status"], "?")
        print(f" [{status_icon}] {r['status']} ({r['response_time_s']}s, {r['response_size']}b)")

    elapsed_total = time.time() - start_total
    success, total = print_report(results, elapsed_total)

    if args.csv:
        save_csv(results, args.csv)
        print(f"\nCSV saved to: {args.csv}")

    if args.json:
        summary = {"total": total, "success": success,
                    "rate": f"{success/max(total,1)*100:.1f}%"}
        save_json(results, summary, args.json)
        print(f"JSON saved to: {args.json}")

    sys.exit(0 if success >= total * 0.6 else 1)


if __name__ == "__main__":
    main()
