#!/usr/bin/env python3
import argparse
import base64
import hashlib
import os
import re
import subprocess
import sys
import time
from urllib.parse import urlsplit

ONION_CAPTCHA_SOURCES = [
    {
        "name": "bizzle",
        "url": "http://bizzlefiql2vuw6i5oaxguwcnpmqydq3b5uc6xyccvhm5fkxnjrzlid.onion/generate_captcha.php",
        "type": "direct_image",
    },
    {
        "name": "deepma",
        "url": "http://deepmafiadugf2n7a7e2hkze2rtrxjgqt23yskzmo7bgheg7nyp7eid.onion/login",
        "type": "inline_base64",
    },
    {
        "name": "tormart",
        "url": "http://tormartap55z7dhn6fgz625lk55y3appygq5ts6uk4k77zqykxxreqid.onion/captcha",
        "type": "inline_base64",
    },
    {
        "name": "btcvps",
        "url": "http://btcvps7d2dmi6oo6pw2vh6qcncxexvh3zdz7n4isquuiqfz3wqx7xyd.onion/register",
        "type": "page_img_tag",
    },
]

AHMIA_SEARCH = "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/search/?q="
TORCH_SEARCH = "http://torchdeedp3i2jigzjdmfpn5ttjhthh5wbmda2rr3jvqjg5p77c54dqd.onion/search?query="

_ONION_ALPHABET = frozenset("abcdefghijklmnopqrstuvwxyz234567")


def http_hostname(url):
    """Hostname from the URL authority. Userinfo, path, and query do not count."""
    try:
        parts = urlsplit((url or "").strip())
    except ValueError:
        return ""
    if parts.scheme not in ("http", "https"):
        return ""
    return (parts.hostname or "").lower().rstrip(".")


def is_onion_http_url(url):
    """True only when the hostname's last label is onion.

    The address label is the one before that suffix. A lookalike earlier in
    the path, query, or userinfo is a different host.
    """
    host = http_hostname(url)
    labels = [part for part in host.split(".") if part]
    if len(labels) < 2 or labels[-1] != "onion":
        return False
    address = labels[-2]
    if not 1 <= len(address) <= 63:
        return False
    return all(ch in _ONION_ALPHABET for ch in address)


def onion_urls_from_html(html, limit=20):
    found = []
    for raw in re.findall(r"https?://[^\s\"'<>]+", html or ""):
        if not is_onion_http_url(raw) or raw in found:
            continue
        found.append(raw)
        if len(found) >= limit:
            break
    return found


def _same_http_host(url, base_url):
    host = http_hostname(url)
    return bool(host) and host == http_hostname(base_url)


def _origin_join(base_url, path):
    try:
        parts = urlsplit(base_url)
    except ValueError:
        return ""
    host = (parts.hostname or "").lower().rstrip(".")
    if parts.scheme not in ("http", "https") or not host:
        return ""
    netloc = host if not parts.port else f"{host}:{parts.port}"
    return f"{parts.scheme}://{netloc}{path}"


def fetch_tor(url, timeout=30):
    if not is_onion_http_url(url):
        return b""
    try:
        result = subprocess.run(
            [
                "curl", "-s", "-k", "--max-time", str(timeout),
                "--socks5-hostname", "127.0.0.1:9050", "-L",
                "-H", "User-Agent: Mozilla/5.0 (Windows NT 10.0; rv:128.0) Gecko/20100101 Firefox/128.0",
                url,
            ],
            capture_output=True,
            timeout=timeout + 10,
        )
        return result.stdout
    except Exception:
        return b""


def fetch_tor_binary(url, output_path, timeout=30):
    if not is_onion_http_url(url):
        return False
    try:
        subprocess.run(
            [
                "curl", "-s", "-k", "--max-time", str(timeout),
                "--socks5-hostname", "127.0.0.1:9050", "-L",
                "-o", output_path,
                "-H", "User-Agent: Mozilla/5.0 (Windows NT 10.0; rv:128.0) Gecko/20100101 Firefox/128.0",
                url,
            ],
            capture_output=True,
            timeout=timeout + 10,
        )
        return os.path.exists(output_path) and os.path.getsize(output_path) > 100
    except Exception:
        return False


def extract_base64_images(html_bytes):
    html = html_bytes.decode("utf-8", errors="ignore")
    pattern = r'data:image/(?:png|jpeg|jpg|gif);base64,([A-Za-z0-9+/=\s]+)'
    matches = re.findall(pattern, html)
    images = []
    for m in matches:
        clean = m.replace("\n", "").replace("\r", "").replace(" ", "")
        try:
            data = base64.b64decode(clean)
            if len(data) > 100:
                images.append(data)
        except Exception:
            pass
    return images


def extract_img_src_captcha(html_bytes, base_url):
    html = html_bytes.decode("utf-8", errors="ignore")
    captcha_region = ""
    for keyword in ["captcha", "CAPTCHA", "Captcha"]:
        idx = html.find(keyword)
        if idx >= 0:
            start = max(0, idx - 500)
            end = min(len(html), idx + 500)
            captcha_region = html[start:end]
            break
    if not captcha_region:
        return []

    pattern = r'<img[^>]+src=["\']([^"\']+)["\']'
    srcs = re.findall(pattern, captcha_region)

    urls = []
    for src in srcs:
        if src.startswith("data:"):
            continue
        if src.startswith("http://") or src.startswith("https://"):
            if _same_http_host(src, base_url):
                urls.append(src)
        elif src.startswith("//"):
            try:
                scheme = urlsplit(base_url).scheme
            except ValueError:
                scheme = ""
            if scheme not in ("http", "https"):
                continue
            absolute = scheme + ":" + src
            if _same_http_host(absolute, base_url):
                urls.append(absolute)
        elif src.startswith("/"):
            joined = _origin_join(base_url, src)
            if joined and _same_http_host(joined, base_url):
                urls.append(joined)
    return urls


def search_ahmia_for_captcha_sites(query="captcha register forum"):
    url = AHMIA_SEARCH + query.replace(" ", "+")
    raw = fetch_tor(url, timeout=45)
    html = raw.decode("utf-8", errors="ignore")
    return onion_urls_from_html(html)


def search_torch_for_captcha_sites(query="captcha register"):
    url = TORCH_SEARCH + query.replace(" ", "+")
    raw = fetch_tor(url, timeout=45)
    html = raw.decode("utf-8", errors="ignore")
    return onion_urls_from_html(html)


def harvest_direct_image(source, output_dir, count):
    saved = 0
    for i in range(count):
        fname = f"{source['name']}_{int(time.time())}_{i}.png"
        fpath = os.path.join(output_dir, fname)
        if fetch_tor_binary(source["url"], fpath, timeout=20):
            saved += 1
            print(f"  [{saved}/{count}] {fname}")
        time.sleep(1)
    return saved


def harvest_base64_page(source, output_dir, count):
    saved = 0
    for i in range(count):
        raw = fetch_tor(source["url"], timeout=30)
        if not raw:
            time.sleep(2)
            continue
        images = extract_base64_images(raw)
        for j, data in enumerate(images):
            h = hashlib.md5(data).hexdigest()[:8]
            fname = f"{source['name']}_{h}_{int(time.time())}.png"
            fpath = os.path.join(output_dir, fname)
            with open(fpath, "wb") as f:
                f.write(data)
            saved += 1
            print(f"  [{saved}] {fname}")
            if saved >= count:
                break
        time.sleep(2)
        if saved >= count:
            break
    return saved


def harvest_img_tag_page(source, output_dir, count):
    saved = 0
    for i in range(count):
        raw = fetch_tor(source["url"], timeout=30)
        if not raw:
            time.sleep(2)
            continue

        b64_imgs = extract_base64_images(raw)
        for data in b64_imgs:
            h = hashlib.md5(data).hexdigest()[:8]
            fname = f"{source['name']}_b64_{h}.png"
            fpath = os.path.join(output_dir, fname)
            with open(fpath, "wb") as f:
                f.write(data)
            saved += 1
            print(f"  [{saved}] {fname}")
            if saved >= count:
                return saved

        img_urls = extract_img_src_captcha(raw, source["url"])
        for img_url in img_urls:
            fname = f"{source['name']}_{int(time.time())}_{saved}.png"
            fpath = os.path.join(output_dir, fname)
            if fetch_tor_binary(img_url, fpath, timeout=20):
                saved += 1
                print(f"  [{saved}] {fname}")
                if saved >= count:
                    return saved
        time.sleep(2)
    return saved


def discover_and_harvest(output_dir, max_sites=10, per_site=5):
    print("Searching Ahmia for captcha sites...")
    ahmia_urls = search_ahmia_for_captcha_sites()
    print(f"  Found {len(ahmia_urls)} onion URLs from Ahmia")

    print("Searching Torch for captcha sites...")
    torch_urls = search_torch_for_captcha_sites()
    print(f"  Found {len(torch_urls)} onion URLs from Torch")

    all_urls = list(set(ahmia_urls + torch_urls))
    print(f"Total unique onion URLs: {len(all_urls)}")

    saved = 0
    tested = 0
    for url in all_urls:
        if tested >= max_sites:
            break
        if not is_onion_http_url(url):
            continue
        tested += 1
        print(f"\nProbing [{tested}/{min(len(all_urls), max_sites)}] {url[:60]}...")
        raw = fetch_tor(url, timeout=20)
        if not raw:
            continue
        html = raw.decode("utf-8", errors="ignore").lower()
        if "captcha" not in html:
            continue

        print(f"  CAPTCHA detected on {url[:60]}")

        b64_imgs = extract_base64_images(raw)
        for data in b64_imgs:
            h = hashlib.md5(data).hexdigest()[:8]
            name = (http_hostname(url) or "unknown")[:12]
            fname = f"discovered_{name}_{h}.png"
            fpath = os.path.join(output_dir, fname)
            with open(fpath, "wb") as f:
                f.write(data)
            saved += 1
            print(f"  Saved: {fname}")

        img_urls = extract_img_src_captcha(raw, url)
        for img_url in img_urls[:per_site]:
            name = (http_hostname(url) or "unknown")[:12]
            fname = f"discovered_{name}_{int(time.time())}_{saved}.png"
            fpath = os.path.join(output_dir, fname)
            if fetch_tor_binary(img_url, fpath, timeout=20):
                saved += 1
                print(f"  Saved: {fname}")
        time.sleep(1)

    return saved


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="captcha_dataset/unlabeled")
    parser.add_argument("--count", type=int, default=50)
    parser.add_argument("--discover", action="store_true")
    parser.add_argument("--source", default="all")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    total = 0

    if args.discover:
        total += discover_and_harvest(args.output)

    sources = ONION_CAPTCHA_SOURCES
    if args.source != "all":
        sources = [s for s in sources if s["name"] == args.source]

    per_source = max(1, args.count // max(1, len(sources)))

    for source in sources:
        print(f"\nHarvesting from {source['name']} ({source['type']})...")
        if source["type"] == "direct_image":
            total += harvest_direct_image(source, args.output, per_source)
        elif source["type"] == "inline_base64":
            total += harvest_base64_page(source, args.output, per_source)
        elif source["type"] == "page_img_tag":
            total += harvest_img_tag_page(source, args.output, per_source)

    print(f"\nTotal harvested: {total} images in {args.output}")


if __name__ == "__main__":
    main()
