#!/usr/bin/env python3
# NAAN harvest target policy. Keep in lockstep with naan_harvest_plan.cpp.
from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from urllib.parse import quote_plus


class LoopAction(str, Enum):
    FETCH = "fetch"
    WAIT_TOPIC = "wait_topic"
    WAIT_BUDGET = "wait_budget"
    STOP_USER = "stop_user"


@dataclass(frozen=True)
class HarvestTarget:
    url: str
    engine: str
    onion: bool = False


CLEARNET_ENGINES = (
    "https://html.duckduckgo.com/html/?q=",
    "https://search.brave.com/search?q=",
    "https://en.wikipedia.org/w/index.php?search=",
    "https://arxiv.org/search/?query=",
)

DARKNET_ENGINES = (
    "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/search/?q=",
    "http://torchdeedp3i2jigzjdmfpn5ttjhthh5wbmda2rr3jvqjg5p77c54dqd.onion/search?query=",
    "http://duckduckgogg42xjoc72x3sjasowoarfbgcmvfimaftt6twagswzczad.onion/?q=",
    "http://darkzqtmbdeauwq5mzcmgeeuhet42fhfjj4p5wbak3ofx2yqgecoeqyd.onion/search?query=",
    "http://search7tdrcvri22rieiwgi5g46qnwsesvnubqav2xakhezv4hjzkkad.onion/result.php?search=",
    "http://haystak5njsmn2hqkewecpaxetahtwhsbsa64jom2k22z5afxhnpxfid.onion/?q=",
)

JUNK_NEEDLES = (
    "dread access queue",
    "awaiting forwarding to the platform",
    "just a moment...",
    "cf-challenge",
    "attention required! | cloudflare",
    "enable javascript and cookies to continue",
    "checking your browser before accessing",
    "have not deployd non-javascript",
    "this site requires javascript",
    "please enable javascript to use ahmia",
)


def decide_loop(user_stop: bool, topics_empty: bool, spent: float, budget: float) -> LoopAction:
    if user_stop:
        return LoopAction.STOP_USER
    if topics_empty:
        return LoopAction.WAIT_TOPIC
    if budget > 0 and spent + 1.0 > budget:
        return LoopAction.WAIT_BUDGET
    return LoopAction.FETCH


def _wants_onion(topic: str) -> bool:
    t = topic.lower()
    return any(k in t for k in ("onion", "darknet", "tor", "whistle", "leak"))


def pick_target(topic: str, tick: int) -> HarvestTarget:
    q = quote_plus(topic)
    t = topic.lower()
    if _wants_onion(topic):
        idx = tick % len(DARKNET_ENGINES)
        return HarvestTarget(DARKNET_ENGINES[idx] + q, f"darknet-{idx}", onion=True)
    if tick % 3 == 0:
        if any(k in t for k in ("crypto", "zero-day", "cve", "exploit")):
            return HarvestTarget("https://arxiv.org/list/cs.CR/recent", "arxiv-cr")
        if "ai" in t:
            return HarvestTarget("https://arxiv.org/list/cs.AI/recent", "arxiv-ai")
        return HarvestTarget(
            f"https://arxiv.org/search/?query={q}&searchtype=all&source=header",
            "arxiv-search",
        )
    idx = tick % len(CLEARNET_ENGINES)
    url = CLEARNET_ENGINES[idx] + q
    name = "clearnet"
    if "duckduckgo" in url:
        name = "duckduckgo"
    elif "brave" in url:
        name = "brave"
    elif "wikipedia" in url:
        name = "wikipedia"
    elif "arxiv" in url:
        name = "arxiv"
    return HarvestTarget(url, name, onion=False)


def is_junk_html(html: str) -> bool:
    if len(html) < 80:
        return True
    h = html[:8000].lower()
    return any(n in h for n in JUNK_NEEDLES)


def is_search_surface(url: str) -> bool:
    u = url.lower()
    return (
        "duckduckgo.com" in u
        or "duckduckgogg" in u
        or "search.brave.com" in u
        or "wikipedia.org/w/" in u
        or "arxiv.org/search" in u
        or "arxiv.org/list" in u
        or "ahmia." in u
        or "juhanurmihxlp77" in u
        or "torchdeedp3i2" in u
        or "darkzqtmbdeauw" in u
        or "search7tdrcvri" in u
        or "haystak5njsmn" in u
    )


def _origin(base: str) -> str:
    i = base.find("://")
    if i < 0:
        return ""
    slash = base.find("/", i + 3)
    return base if slash < 0 else base[:slash]


def resolve_href(href: str, base: str = "") -> str:
    if href.startswith("http://") or href.startswith("https://"):
        return href
    if href.startswith("//"):
        i = base.find("://")
        prefix = base[: i + 1] if i >= 0 else "https:"
        return prefix + href
    if href.startswith("/") and base:
        origin = _origin(base)
        return origin + href if origin else ""
    return ""


def extract_hrefs(html: str, limit: int = 8, base: str = "") -> list[str]:
    out: list[str] = []
    pos = 0
    while len(out) < limit:
        pos = html.find("href=", pos)
        if pos < 0:
            break
        pos += 5
        while pos < len(html) and html[pos] in " '\"":
            pos += 1
        if pos >= len(html):
            break
        end = pos
        while end < len(html) and html[end] not in "\"' >#":
            end += 1
        url = resolve_href(html[pos:end], base)
        pos = end
        if not url or is_search_surface(url):
            continue
        if url not in out:
            out.append(url)
    return out
