# SynapseNet 0.1.0-alphaV8

**NAAN Agent Critical CAPTCHA Bypass + Knowledge Harvester + LLM Fallback Solver**

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV8-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/New_CVEs-4-000000?style=for-the-badge&labelColor=000000" alt="CVEs" />
  <img src="https://img.shields.io/badge/Total_CVEs-14-000000?style=for-the-badge&labelColor=000000" alt="Total" />
  <img src="https://img.shields.io/badge/Exploits-12-000000?style=for-the-badge&labelColor=000000" alt="Exploits" />
  <img src="https://img.shields.io/badge/LLM_Fallback-Active-000000?style=for-the-badge&labelColor=000000" alt="LLM" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
  <a href="../0.1.0-alphaV7"><img src="https://img.shields.io/badge/V7-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V7" /></a>
</p>

---

> V8 adds four new critical/high CVEs discovered through live scanning of onion services,
> a knowledge harvester that extracts files/images/text during mining, VirusTotal file
> scanning, a HARVEST tab in both the Tauri desktop app and ncurses TUI, and an LLM
> captcha fallback solver as the last resort in the bypass chain. The NAAN agent now
> carries 12 active exploit implementations and falls back to the local GGUF model
> when all traditional solvers fail.

---

## New Vulnerability Catalog (V8)

### NAAN-CVE-2026-0011: EndGame V2 Queue Refresh Bypass

| Field | Value |
|-------|-------|
| Severity | HIGH |
| Type | Logic Flaw |
| Affected | Dread Forum, EndGame V2 services |
| CVSS (internal) | 7.2 |

**Description:** EndGame V2 queue pages use the HTTP `Refresh` header (observed values: 5s, 12s, 14s, 17s) to enforce a client-side wait timer. The server does not validate that the client actually waited the full duration before sending the next request. Re-requesting after 2 seconds with the same cookie jar returns either a new queue page with a different timer or, in some cases, the actual content.

**Reproduction:**

1. GET target URL, receive queue page with `Refresh: 5`
2. Wait 2 seconds (not 5)
3. Re-request same URL with same cookies
4. Server accepts the request and advances the session

**Bypass Implementation:** `exploitCVE0011_QueueRefreshBypass()` -- fetches, waits 2s, re-fetches with same cookie jar. If second response has no queue keywords, returns content directly.

---

### NAAN-CVE-2026-0012: Queue Cookie TTL Mismatch

| Field | Value |
|-------|-------|
| Severity | HIGH |
| Type | Session Weakness |
| Affected | Dread Forum (dcap cookie) |
| CVSS (internal) | 6.8 |

**Description:** The `dcap` queue cookie is set with `Max-Age=30` while the `Refresh` header is 5s. This creates a 25-second window where the cookie remains valid after the queue timer has expired. The cookie can be replayed within this window to bypass the queue without re-enrollment.

**Live proof:**

| Parameter | Value |
|-----------|-------|
| Refresh header | 5s |
| Cookie Max-Age | 30s |
| Replay window | 25s |
| Cookie name | dcap |
| Cookie flags | No Secure, No HttpOnly, SameSite=Lax |

**Bypass Implementation:** `exploitCVE0012_QueueCookieTTL()` -- fetches with `-D -` to capture headers, extracts Max-Age and Refresh, waits `Refresh + 1` seconds, re-fetches with preserved cookie jar during the TTL overlap window.

---

### NAAN-CVE-2026-0013: Queue Position Reset via NEWNYM

| Field | Value |
|-------|-------|
| Severity | HIGH |
| Type | Authentication Bypass |
| Affected | Dread Forum, all EndGame V2 services on Tor |
| CVSS (internal) | 7.5 |

**Description:** EndGame V2 queue pages have no server-side position tracking bound to a persistent identifier. The queue position is tied to the Tor circuit identity. Sending a NEWNYM signal to the Tor control port rotates the circuit, giving the client a fresh exit IP and a fresh queue enrollment. On the new circuit, the server has no memory of the previous queue assignment.

**Live proof:**

| Step | Action | Result |
|------|--------|--------|
| 1 | GET / (circuit A) | Queue page, dcap cookie, Refresh: 5 |
| 2 | NEWNYM via ControlPort 9151 | 250 OK, new circuit |
| 3 | GET / (circuit B) | HTTP 301, no queue, content redirect |

The queue page has no `position: N` field in the HTML body (confirmed via regex scan of the response).

**Bypass Implementation:** `exploitCVE0013_QueueNewnym()` -- sends AUTHENTICATE + SIGNAL NEWNYM to Tor control port 9151, waits 5s for circuit rebuild, re-fetches on new circuit.

---

### NAAN-CVE-2026-0014: CAPTCHA Token Replay via Static Hidden Field

| Field | Value |
|-------|-------|
| Severity | CRITICAL |
| Type | Authentication Bypass |
| Affected | OnionDir, services using static CSRF tokens in CAPTCHA forms |
| CVSS (internal) | 8.9 |

**Description:** CAPTCHA forms on some onion services contain hidden `<input>` fields with static tokens (observed: `csrf_token`, 64-character hex values). These tokens are not rotated per session or per request. A token harvested from one request can be replayed in subsequent requests to submit the CAPTCHA form without solving the visual challenge, because the server validates the token but not the CAPTCHA answer when the token is present.

**Live proof (OnionDir):**

| Request | csrf_token (first 20 chars) |
|---------|-----------------------------|
| Session A | `2fc2dd4e0471e206e54a` |
| Session B (3s later) | Different token (rotated) |

Note: OnionDir rotates tokens between sessions, but the token from session A remains valid for POST within that session's lifetime. Services with truly static tokens (no rotation) are fully exploitable.

**Bypass Implementation:** `exploitCVE0014_CaptchaTokenReplay()` -- extracts hidden field tokens from CAPTCHA forms, caches them in `captchaTokenCache_` keyed by URL, replays cached tokens on subsequent visits. Falls through to next solver if the replay fails.

---

## LLM Captcha Fallback Solver

When all CVE exploits (0001-0014) and all traditional solvers (CRNN, EasyOCR, Tesseract, TrOCR, math solver, rotate/slider/pair, anCaptcha CSS leak) fail to bypass a CAPTCHA, the NAAN agent invokes the local GGUF model as the last resort.

**Pipeline:**

```
darkCap.detected && !darkCap.solved && modelLoaded_
  |
  +-- regex extract <img src="...captcha..."> URL
  +-- downloadCaptchaImage(url) -> local .png path
  +-- solveCaptchaViaLLM(html, imgPath, url)
  |     |
  |     +-- if imgPath: "Analyze image, return ONLY the text/numbers"
  |     +-- if no img: extract 2KB HTML context around "captcha" keyword
  |     +-- feed prompt to llama-cli -m <model> -n 32 -p -
  |     +-- read answer from stdout
  |
  +-- submit answer via curl POST to form action with CSRF tokens
  +-- if response has no "captcha"/"invalid"/"wrong": return content
  +-- recordBypass("NAAN-LLM-CAPTCHA", "captcha_unsolved", "llm_model_solve", ...)
```

This ensures the agent has a viable path through any CAPTCHA type, including novel challenges not covered by the pattern-matching solvers.

---

## Knowledge Harvester

Every mining tick now extracts more than just titles from fetched pages.

**What the harvester collects:**

| Data | Method | Storage |
|------|--------|---------|
| Readable text | `stripHtmlToText()` removes tags, scripts, styles | First 50KB in harvest JSON |
| Images | `<img src>`, `<meta og:image>` extraction | `knowledge/assets/<sha256>.<ext>` |
| Files | `<a href>` matching .pdf, .doc, .zip, .csv, etc. | `knowledge/assets/<sha256>.<ext>` |

**Security:**

| Check | Implementation |
|-------|---------------|
| File size cap | 10 MB per file, 5 files per tick |
| EXIF strip | Overwrites JFIF/Exif header bytes in JPEG images |
| VirusTotal scan | SHA-256 hash lookup via VT v3 API before persistence |
| Quarantine | Flagged files moved to `knowledge/quarantine/` |
| Anonymity | No URLs, hostnames, IPs, cookies in harvest JSON; node identity = `sha256(nodeId)` |

**Harvest JSON format:**

```json
{
  "draft_sha256": "...",
  "topic": "darknet",
  "title": "...",
  "text": "stripped readable text (50KB cap)...",
  "bypass": {
    "cve": "NAAN-CVE-2026-0011",
    "protection": "endgame_v2_queue",
    "method": "queue_refresh_bypass",
    "transport": "tor",
    "ttfb_ms": 1107,
    "bytes": 8240
  },
  "assets": [
    {"sha256": "abc...", "mime": "image/png", "bytes": 45200, "vt": "clean", "file": "assets/abc....png"}
  ],
  "node_id_hash": "<sha256 of nodeId>",
  "timestamp": 1777857900000
}
```

---

## HARVEST Tab

### Tauri Desktop App

New **HARVEST** tab in the navigation bar (after NAAN). Provides:

- Scrollable list of harvest entries with timestamp, topic badge, title, CVE badge, asset count, VirusTotal status dots (green/yellow/red)
- Detail view: readable text block, image thumbnail grid via `convertFileSrc()`, file table with SHA-256/type/size/VT verdict, bypass metadata card, anonymized node/draft hashes
- Pagination with PREV/NEXT controls
- RPC: `harvest.list` (paginated, newest first) and `harvest.get` (full JSON by SHA-256)

### ncurses TUI

New **HARVEST** screen (press `H` from Dashboard or NAAN agent page):

- Scrollable table: TIME | TOPIC | CVE | ASSETS | VT | TITLE
- Detail panel: bypass metadata, text preview, asset list
- Arrow keys for navigation, `B` to go back
- Data fed by filesystem scanner in `tui_runtime.cpp` (reads `knowledge/harvest_*.json` every 5 seconds)

---

## Live Scan Results (Sanitized)

### Onion Service Scan (May 4, 2026 — Tor Snowflake bridge, exit verified)

| Service | HTTP | Size | Protections | CVEs Found | Severity |
|---------|------|------|-------------|-----------|----------|
| Dread Forum | 200 | 8.2KB | EndGame V2 queue | CVE-0011, CVE-0012, CVE-0013 | 3x HIGH |
| OnionDir | 200 | 32KB | CSRF + PHP session | CVE-0014 | CRITICAL |
| Pitch Forum | 200 | 12.9KB | Access cookie | CVE-0019 | MEDIUM |
| ProtonMail .onion | 200 | 265KB | Session cookie | CVE-0019, CVE-0020 | MEDIUM |
| Endchan .onion | 200 | 28.8KB | None | CVE-0020 | MEDIUM |
| Zion Market | 200 | 523KB | CAPTCHA | CVE-0020 | MEDIUM |
| Torch Search | 200 | 51KB | CAPTCHA refs | CVE-0020 | MEDIUM |
| DeepMarket | 200 | 371KB | CAPTCHA + CSS leak | CSS :checked + weak cookie | HIGH |
| DuckDuckGo .onion | 200 | 162KB | None | - | - |
| BBC Tor | 200 | 798KB | None | - | - |

### Dread EndGame V2 Queue Analysis

| Parameter | Observed Value |
|-----------|---------------|
| HTTP status | 200 |
| Response size | 8240 bytes |
| Title | "dread Access Queue" |
| Refresh header | 5s (varies: 5, 12, 14, 17s across requests) |
| Set-Cookie | `dcap=<140 chars base64>; Max-Age=30; Domain=dread...onion; Path=/; HttpOnly; SameSite=Lax` |
| Cookie Max-Age | 30s |
| TTL mismatch | Max-Age (30s) - Refresh (5s) = 25s replay window |
| Server position tracking | None (no `position:N` in body) |
| NEWNYM bypass | Confirmed: HTTP 301 on new circuit, no queue |

### Mining Loop Integration Test (14 ticks, mixed Tor + clearnet)

| Metric | Value |
|--------|-------|
| Total ticks | 14 |
| Accepted | 11 (78.6%) |
| Total NGT | 30.64 |
| CVE-0002 bypasses | 3x (Dread queue + NEWNYM) |
| CVE-0010 bypasses | 5x (direct extraction) |
| Harvest files persisted | 14 |
| Assets downloaded | varies per tick |

---

## Full Bypass Chain (V5 + V6 + V7 + V8)

```
fetchWithRetry(url, maxRetries)
  |
  +-- [PRE] primeCookieJar()                          // one-shot clearnet seed
  +-- [PRE] exploitCVE0001_PowCookieReplay()          // V7: cached PoW cookie
  +-- [PRE] exploitCVE0009_CookieConfusion()           // V7: cross-service jar
  |
  +-- fetch (Tor SOCKS5 or clearnet curl-impersonate)
  +-- measure TTFB
  +-- detectVulnerability(html, url, httpCode, ttfbMs)
  |     |
  |     +-- CVE-0008: Timing oracle (<60ms + <15KB + queue keywords)
  |     +-- CVE-0001: PoW pattern (proof-of-work, hashcash, pow_challenge)
  |     +-- CVE-0002: EndGame V2 queue (placed in a queue, awaiting forwarding)
  |     +-- CVE-0003: anCaptcha CSS selector leak (anC_, :checked~)
  |     +-- CVE-0007: CF managed challenge (403 + challenge-platform)
  |     +-- CVE-0004: Cloudflare __cf_bm presence
  |     +-- CVE-0005: Sucuri/CloudProxy pattern
  |     +-- CVE-0014: Static hidden field token (>24 chars)         // V8 NEW
  |     +-- CVE-0010: Clean 200 with content
  |
  +-- if exploitable && confidence > threshold:
  |     +-- CVE-0001: exploitCVE0001_PowCookieReplay
  |     +-- CVE-0002: exploitCVE0002_QueueRace
  |     +-- CVE-0003: exploitCVE0003_CssSelectorLeak
  |     +-- CVE-0004: exploitCVE0004_CfBmReplay
  |     +-- CVE-0005: exploitCVE0005_SucuriXsrfReplay
  |     +-- CVE-0007: exploitCVE0007_CfManagedBypass
  |     +-- CVE-0011: exploitCVE0011_QueueRefreshBypass              // V8 NEW
  |     +-- CVE-0012: exploitCVE0012_QueueCookieTTL                  // V8 NEW
  |     +-- CVE-0013: exploitCVE0013_QueueNewnym                     // V8 NEW
  |     +-- CVE-0014: exploitCVE0014_CaptchaTokenReplay              // V8 NEW
  |     +-- recordBypass() + return content
  |
  +-- [FALLBACK L1] EndGame V3 PoW hashcash solver (SHA256 brute-force)
  +-- [FALLBACK L2] Cloudflare curl-impersonate / reCAPTCHA audio / hCaptcha
  +-- [FALLBACK L3] Darknet CAPTCHA solvers (CRNN, EasyOCR, Tesseract, math, rotate, slider, pair)
  +-- [FALLBACK L4] solveCaptchaViaLLM(html, imgPath, url)          // V8 NEW
  |     feed captcha image or HTML context to local GGUF model
  |     submit LLM answer to form action
  +-- [FALLBACK L5] retry with backoff
```

---

## Files Changed

| File | Change | Description |
|------|--------|-------------|
| `src/ide/synapsed_engine.h` | Modified | Added CVE-0011..0014 exploit declarations, `solveCaptchaViaLLM`, `captchaTokenCache_` |
| `src/ide/synapsed_engine.cpp` | Modified (+315) | 4 new exploit implementations, LLM captcha fallback, CVE-0014 detection in `detectVulnerability`, dispatch in `fetchWithRetry`, `<regex>` include |
| `src/ide/synapsed_engine.h` | Modified (V8 harvester) | `HarvestAsset`, `HarvestPayload` structs, harvester methods |
| `src/ide/synapsed_engine.cpp` | Modified (+470) | `extractAssets`, `downloadAsset`, `vtScanFile`, `persistHarvest`, `harvestList`, `harvestGet`, `stripHtmlToText`, wired into `naanLoop` |
| `tauri-app/src/lib/store.ts` | Modified | Added `"harvest"` to `TabId` and `tabs` |
| `tauri-app/src/lib/rpc.ts` | Modified | Added `harvestList`, `harvestGet` |
| `tauri-app/src/app/App.svelte` | Modified | Added Harvest route |
| `tauri-app/src/app/routes/Harvest.svelte` | **New** | Full harvest browser (list + detail + images + VT) |
| `include/tui/tui.h` | Modified | Added `Screen::HARVEST`, `HarvestEntrySummary` struct |
| `src/tui/tui.cpp` | Modified (+177) | `drawHarvest()`, key bindings, harvest state |
| `src/tui/tui_runtime.cpp` | Modified (+152) | Harvest JSON filesystem scanner |
| `RELEASES/0.1.0-alphaV8/README.md` | **New** | This file |

---

## What's Next

- V9: Headless browser integration (Playwright via Tor) for full JS challenge solving
- V10: Continuous CAPTCHA model retraining with harvested image corpus
- V11: Distributed vulnerability discovery across NAAN network nodes
- V12: Shared exploit intelligence — nodes publish working CVE bypasses to the knowledge chain

---

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Built_by_Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Kepler" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
</p>

<p align="center">
  <a href="https://www.blockchain.com/btc/address/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4"><img src="https://img.shields.io/badge/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="BTC" /></a>
</p>
