<h1 align="center">SynapseNet 0.1.0-alphaV5</h1>

<p align="center"><strong>Darknet CAPTCHA Bypass &amp; NAAN Agent Autonomous Navigation</strong></p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV5-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/Bug_Fixes-15-000000?style=for-the-badge&labelColor=000000" alt="Bug Fixes" />
  <img src="https://img.shields.io/badge/CAPTCHA-6_Types_Solved-000000?style=for-the-badge&labelColor=000000" alt="CAPTCHA" />
  <img src="https://img.shields.io/badge/Tested-Live_Darknet-000000?style=for-the-badge&labelColor=000000" alt="Tested" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" /></a>
  <a href="https://github.com/anakrypt-kepler/SynapseNet"><img src="https://img.shields.io/badge/Documentation-000000?style=for-the-badge&logo=gitbook&logoColor=white" alt="Docs" /></a>
  <a href="https://github.com/anakrypt-kepler/SynapseNet/blob/main/SynapseNet_Whitepaper.pdf"><img src="https://img.shields.io/badge/Whitepaper-000000?style=for-the-badge&logo=adobeacrobatreader&logoColor=white" alt="Whitepaper" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV4"><img src="https://img.shields.io/badge/%E2%86%90_0.1.0--alphaV4-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V4" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai/tree/main/RELEASES"><img src="https://img.shields.io/badge/All_Releases-000000?style=for-the-badge&logo=github&logoColor=white" alt="All Releases" /></a>
</p>

---

> V5 gives the NAAN agent the ability to autonomously bypass CAPTCHA protections on darknet (.onion) and clearnet sites. Six distinct CAPTCHA types are now detected and solved programmatically — from simple math challenges to CSS-based rotate puzzles and ML-assisted image OCR. Every solver was tested against live darknet services over Tor. This release also fixes 15 bugs across the fetch pipeline, OCR engine, form submission logic, and DDoS queue handling.

---

## What's New

The NAAN agent previously could not get past CAPTCHA-protected pages. If a forum, marketplace, or service presented a challenge, the agent would return empty content and move on. V5 changes that — the agent now detects, solves, and submits CAPTCHAs automatically as part of its `fetchWithRetry` loop.

### CAPTCHA Types Solved

| Type | Detection Method | Solver | Accuracy | Live-Tested On |
|------|-----------------|--------|----------|----------------|
| **Text Image (simple)** | `<img>` tag near `captcha` keyword | EasyOCR + Tesseract dual pipeline | 95-100% | Bizzle Forum (.onion) |
| **Text Image (hard background)** | Image dimensions ≥ 300x80 | ML pipeline: K-means color segmentation + HSV isolation + EasyOCR + TrOCR transformer | 70-80% | BTC VPS (.onion) |
| **Base64 Inline Image** | `data:image/jpeg;base64,` in `src` attribute | Base64 decode → same ML pipeline | 90-98% | DeepMa / TorMart (.onion) |
| **Math (XenForo QA)** | `data-xf-init="qa-captcha"` or digit+operator pattern | Arithmetic expression parser | 100% | Altenens / DWF (.onion) |
| **CSS Rotate (anCaptcha)** | `anC_` prefix, `type="radio"`, `rotate(deg)` in CSS | Parse `#id:checked~rotate()` selectors, pick 0° option | 100% | Maverick Blog (.onion), Dread |
| **Cyrillic Text Image** | Cyrillic characters in surrounding HTML | EasyOCR with `['en','ru']` readers | 80-90% | Various RU forums |

### DDoS Protection Bypass

| Protection | How It Works | How We Handle It |
|------------|-------------|-----------------|
| **EndGame V2 (Dread)** | NGINX+LUA queue system, rate-limits by Tor circuit ID, session cookies via AES | Detect queue keywords → parse `<meta http-equiv="refresh">` timer → wait exact ETA → persist cookies → NEWNYM circuit refresh to reset rate limit |
| **Generic Queue** | "Please wait" / "DDoS protection" pages | Detect → wait 30s + 15s per attempt → retry with cookies |
| **Cloudflare** | JS challenge + cookie | `curl-impersonate-chrome` bypass (existing V4 logic) |
| **reCAPTCHA** | Audio challenge fallback | Speech recognition solver (existing V4 logic) |
| **hCaptcha** | API-based solve | External solver integration (existing V4 logic) |

### Live Test Results (April 2026)

All tests performed over Tor (`--socks5-hostname 127.0.0.1:9050`) against real darknet services.

| Site | Protection Type | Result |
|------|----------------|--------|
| **Dread Forum** | EndGame V2 queue + session | **PASSED** — waited through queue, got session cookie, loaded Frontpage (65KB) |
| **DeepMa / TorMart** | Base64 JPEG text CAPTCHA | **PASSED** — EasyOCR read `PXAM` at 97.8% confidence, submitted successfully |
| **Bizzle Forum** | PNG text CAPTCHA | **PASSED** — EasyOCR read `PXZYF` at 100% confidence on first attempt |
| **BTC VPS** | Hard background text CAPTCHA (Bitcoin pattern) | **PARTIAL** — K-means extracted 4/5 chars (`5U63` vs `5UB63`), retry logic compensates |
| **Altenens / DWF** | XenForo QA math CAPTCHA | **PASSED** — parsed `5+3`, answered `8`, 100% solve rate |
| **Maverick Blog** | Real anCaptcha CSS rotate | **PASSED** — parsed 8 radio options with ID-based CSS selectors, selected 0° rotation |

---

## Bug Fixes (15)

### Critical (3)

| # | Bug | Fix | File |
|---|-----|-----|------|
| 1 | **CAPTCHA images not downloaded** — `fetchViaTor` did not pass cookies when downloading CAPTCHA images, causing session mismatch and 403 responses | Added `-c` and `-b` cookie flags to all Tor curl commands in `solveTextCaptcha` | `synapsed_engine.cpp` |
| 2 | **Form action not parsed** — CAPTCHA answers were POST'd to the current URL instead of the form's `action` attribute, causing submission failures on sites where the form target differs | Parse `<form action="...">` relative and absolute URLs, resolve against base URL | `synapsed_engine.cpp` |
| 3 | **CSRF tokens missing from submission** — POST requests did not include hidden form fields (`_token`, `csrf`, `form_token`, `sid`), resulting in server-side rejection | Scan for hidden inputs near the CAPTCHA form, append all token fields to POST data | `synapsed_engine.cpp` |

### High (5)

| # | Bug | Fix | File |
|---|-----|-----|------|
| 4 | **Tesseract misread styled text** — Tesseract consistently misread characters on colored/styled CAPTCHAs (e.g., `V` → `N`, `1` → `I`) | Integrated EasyOCR as primary reader for styled text, Tesseract as fallback on preprocessed images | `synapsed_engine.cpp` |
| 5 | **Base64 CAPTCHAs not detected** — inline `data:image/jpeg;base64,...` CAPTCHAs were missed because detector only searched forward from `captcha` keyword | Added bidirectional `<img>` search (both `rfind` and `find` from the keyword position) with 500-char range | `synapsed_engine.cpp` |
| 6 | **anCaptcha fields not recognized** — real anCaptcha uses randomized CSS class names (`anC_ysl`, `anC_yry`) that did not match hardcoded patterns | Added `anC_` prefix detection, ID-based CSS selector parsing (`#id:checked~rotate()`), and hidden token extraction for randomized field names | `synapsed_engine.cpp` |
| 7 | **Invalid captcha not retried** — when OCR produced a wrong answer and the server responded with "Invalid captcha", the agent treated it as success | Check response body for `Invalid captcha` / `Wrong captcha` / `Incorrect captcha`, re-detect CAPTCHA in response, and `continue` the retry loop | `synapsed_engine.cpp` |
| 8 | **Dread queue treated as content** — EndGame V2 queue pages (~8KB) were returned as valid content instead of triggering a wait-and-retry | Detect `placed in a queue`, `awaiting forwarding`, `estimated entry time`, parse `<meta http-equiv="refresh">` timer, wait exact duration, retry | `synapsed_engine.cpp` |

### Medium (4)

| # | Bug | Fix | File |
|---|-----|-----|------|
| 9 | **Self-signed TLS rejected** — Tor hidden services often use self-signed certificates, causing curl to fail silently | Added `-k` flag to all `fetchViaTor` curl commands to accept self-signed certs | `synapsed_engine.cpp` |
| 10 | **No User-Agent rotation** — all Tor requests used the same static User-Agent, making fingerprinting trivial | Implemented `randomUserAgent()` with 10+ rotating browser strings, applied to every Tor and clearnet request | `synapsed_engine.cpp` |
| 11 | **OCR confidence scoring wrong** — the best-result picker favored long garbage strings over short correct answers | Replaced `len*0.3 + conf*0.7` with confidence-weighted scoring that penalizes results outside 3-6 character range | `synapsed_engine.cpp` |
| 12 | **XenForo QA not detected** — math CAPTCHAs on XenForo forums (`data-xf-init="qa-captcha"`) were not recognized as CAPTCHAs | Added dedicated `qa-captcha` detection branch with digit+operator extraction and `solveMathCaptcha` dispatch | `synapsed_engine.cpp` |

### Low (3)

| # | Bug | Fix | File |
|---|-----|-----|------|
| 13 | **Cookie file not persisted between requests** — Tor session cookies were lost between CAPTCHA image download and form submission | Unified cookie file path (`/tmp/synapsed_tor_cookies.txt`) across all Tor curl calls with both `-c` (write) and `-b` (read) | `synapsed_engine.cpp` |
| 14 | **Tor timeout too short for Dread** — 30-second curl timeout was insufficient for Dread's queue system which can hold connections for 2-3 minutes | Dynamic timeout: 45s default for onion sites, 90s specifically for Dread (detected by URL pattern) | `synapsed_engine.cpp` |
| 15 | **Onion retry count too low** — default 3 retries was not enough to get through EndGame V2 queue + CAPTCHA submission cycle | Onion sites get minimum 8 retries, with NEWNYM circuit refresh every 2 attempts to avoid rate limiting by circuit ID | `synapsed_engine.cpp` |

---

## Architecture

### CAPTCHA Detection Flow

```
fetchWithRetry(url)
  │
  ├─ fetchViaTor(url)  ←  -k, User-Agent rotation, cookies, 45-90s timeout
  │
  ├─ Queue Detection
  │   ├─ EndGame V2: "placed in a queue" → parse meta refresh → wait ETA → NEWNYM
  │   └─ Generic: "Please wait" / "DDoS protection" → wait 30s+15s*attempt
  │
  ├─ detectCaptcha(html)
  │   ├─ anCaptcha (anC_ / ancaptcha / decaptcha)
  │   │   ├─ Rotate → solveRotateCaptcha()  ←  CSS #id:checked~rotate() parser
  │   │   ├─ Slider → solveSliderCaptcha()  ←  CSS translateX parser
  │   │   └─ Pair   → solvePairCaptcha()    ←  Perceptual hash matching
  │   │
  │   ├─ XenForo QA Math → solveMathCaptcha()  ←  expression parser
  │   │
  │   ├─ Text Image / Base64 Image → solveTextCaptcha()
  │   │   └─ ML Pipeline:
  │   │       ├─ Strategy 1: Otsu threshold + resize (simple CAPTCHAs)
  │   │       ├─ Strategy 2: K-means color segmentation (busy backgrounds)
  │   │       ├─ Strategy 3: HSV color isolation (colored text)
  │   │       ├─ EasyOCR on original + all variants
  │   │       ├─ Tesseract multi-PSM (7, 8, 6) on preprocessed
  │   │       ├─ TrOCR transformer fallback (scene text)
  │   │       └─ Best-result picker: confidence * length_penalty
  │   │
  │   └─ Standard rotate/slider (data-angle, puzzle, drag)
  │
  ├─ Submit CAPTCHA
  │   ├─ Parse <form action="...">
  │   ├─ Collect hidden fields (_token, csrf, form_token, sid, anC_*)
  │   ├─ POST answer with cookies
  │   └─ Validate response (no "Invalid captcha" message)
  │
  └─ Retry on failure (up to 8x for onion)
```

### ML Text CAPTCHA Pipeline

```
Input Image
  │
  ├─ is_hard? (width ≥ 300 and height ≥ 80)
  │   ├─ YES → K-means (K=5) color clustering
  │   │         ├─ Darkest cluster → EasyOCR
  │   │         ├─ Darkest 2 clusters → EasyOCR
  │   │         ├─ Lightest cluster → EasyOCR     ← best for BTC VPS style
  │   │         ├─ Lightest 2 clusters → EasyOCR
  │   │         └─ HSV isolation (green+white+dark) → EasyOCR
  │   │
  │   └─ NO → Otsu threshold + resize → Tesseract
  │
  ├─ EasyOCR on original (always)
  ├─ Tesseract PSM 7/8/6 on preprocessed (always)
  ├─ TrOCR transformer (if available)
  │
  └─ Score: confidence * length_penalty
           (1.0 if 3-6 chars, 0.6 if 7-8, 0.3 if >8)
           Pick highest score
```

### anCaptcha CSS Rotate Solver

```
HTML with anCaptcha
  │
  ├─ Find all <input type="radio"> tags
  │   └─ Filter: tag contains "anC_" OR within 5000 chars of ancaptcha/decaptcha context
  │
  ├─ Extract: name, value, id from each radio
  │
  ├─ Map ID → rotation degree:
  │   ├─ Primary: CSS rule  #radioId:checked ~ ... rotate(Ndeg)
  │   └─ Fallback: search  value="radioValue" near rotate(Ndeg)
  │
  ├─ For each radio group:
  │   └─ Pick option where abs(degree) is closest to 0° (upright)
  │
  ├─ Find hidden token:
  │   ├─ Known names: ancaptcha_token, _token, token, captcha_token
  │   └─ Fallback: any <input type="hidden"> with anC_ in name
  │
  └─ Build POST: tokenName=tokenValue&radioGroupName=bestValue
```

### EndGame V2 Queue Handler

```
Response from Dread (.onion)
  │
  ├─ Detect: "placed in a queue" / "awaiting forwarding" / "estimated entry time"
  │
  ├─ Parse wait time:
  │   ├─ <meta http-equiv="refresh" content="N">  → wait N+2 seconds
  │   └─ "estimated ... <number>"                  → wait number+5 seconds
  │   └─ Default: 30 + 15*attempt seconds
  │
  ├─ SIGNAL NEWNYM (Tor control port 9051)
  │   ├─ First EndGame queue hit: immediate NEWNYM + 8s wait
  │   └─ Every 2nd attempt: NEWNYM + 5s wait
  │   └─ Purpose: new Tor circuit ID → reset EndGame rate limiter
  │
  └─ Retry with same cookies (session continuity)
```

---

## Dependencies

### Required (already present in V4)

- `curl` with `--socks5-hostname` support
- `tesseract` OCR engine
- `python3` with `cv2` (OpenCV)

### New in V5

- `easyocr` — neural network OCR, significantly better on styled/handwritten text
- `transformers` + `torch` — TrOCR transformer model (optional, used as fallback for hard CAPTCHAs)

Install:
```bash
pip3 install easyocr
pip3 install transformers torch   # optional, for TrOCR fallback
```

---

## Files Changed

| File | Lines Changed | What |
|------|--------------|------|
| `KeplerSynapseNet/src/ide/synapsed_engine.cpp` | +380 / -85 | All CAPTCHA detection, solving, submission, queue handling, ML pipeline |

Key functions modified:
- `solveTextCaptcha()` — complete rewrite with ML multi-strategy pipeline
- `detectCaptcha()` — added anCaptcha, XenForo QA, base64 inline, bidirectional img search
- `solveRotateCaptcha()` — ID-based CSS selector parsing, randomized field name support
- `fetchViaTor()` — dynamic timeout, `-k` flag, User-Agent rotation, cookie persistence
- `fetchWithRetry()` — EndGame V2 queue detection, meta refresh parsing, NEWNYM circuit refresh, form action parsing, CSRF token collection, invalid captcha retry, 8x onion retries

---

## What's Next

- **V5.1** — Train a lightweight CNN specifically for darknet CAPTCHA fonts to improve BTC VPS-style hard background accuracy from 80% to 95%+
- **V5.2** — Implement Proof-of-Work computation for EndGame V3 (hashcash-style challenge)
- **V6** — Full NAAN agent integration testing across 50+ darknet services with automated success rate reporting

---

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Built_by_Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Kepler" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
</p>

<p align="center">
  If you find this project worth watching — even if you can't contribute code — you can help keep it going.<br>
  Donations go directly toward VPS hosting for seed nodes, build infrastructure, and development time.
</p>

<p align="center">
  <a href="https://www.blockchain.com/btc/address/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4"><img src="https://img.shields.io/badge/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="BTC" /></a>
</p>
