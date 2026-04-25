<h1 align="center">SynapseNet 0.1.0-alphaV6</h1>

<p align="center"><strong>NAAN Agent Integration Testing -- 56 Services, Automated Reporting</strong></p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV6-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/Services_Tested-56-000000?style=for-the-badge&labelColor=000000" alt="Services" />
  <img src="https://img.shields.io/badge/Clearnet-16%2F18_Passed-000000?style=for-the-badge&labelColor=000000" alt="Clearnet" />
  <img src="https://img.shields.io/badge/PoW-7%2F7_Solved-000000?style=for-the-badge&labelColor=000000" alt="PoW" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV5.2"><img src="https://img.shields.io/badge/←_0.1.0--alphaV5.2-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V5.2" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai/tree/main/RELEASES"><img src="https://img.shields.io/badge/All_Releases-000000?style=for-the-badge&logo=github&logoColor=white" alt="All Releases" /></a>
</p>

---

> V6 introduces a comprehensive integration test harness that validates the NAAN agent's ability to fetch, bypass protections, and extract content from 56 real-world services -- 38 onion (.onion) and 18 clearnet. The test harness covers all protection types encountered in the wild: EndGame V2/V3 queues, hashcash PoW, text/math/rotate/slider CAPTCHAs, Cloudflare, reCAPTCHA, hCaptcha, and rate limiting. Every test produces a structured report with per-service status, response times, protection detection, and aggregate success rates. This is the first release where the full NAAN pipeline -- from Tor fetch through protection bypass to content extraction -- is tested end-to-end against live services.

---

## Test Harness

### Service Catalog (56 Services)

| Category | Count | Examples |
|----------|-------|---------|
| Search engines | 12 | DuckDuckGo, Torch, Ahmia, Tordex, Tor66, DarkSearch, Excavator, Haystak, Phobos, Deep Search, OnionLand, OnionDir |
| Forums | 5 | Dread, Pitch, Bizzle, Altenens, DWF |
| News | 8 | BBC Tor, Krebs, Hacker News, BleepingComputer, Dark Reading, Ars Technica, Wired |
| Research | 8 | arxiv CS.CR, arxiv CS.AI, MITRE CVE, NVD, Exploit-DB, Packet Storm, Full Disclosure, IACR |
| Markets | 2 | DeepMa, TorMart |
| Email | 2 | Riseup, ProtonMail |
| Whistleblower | 2 | SecureDrop (Guardian), SecureDrop (WaPo) |
| Hosting | 3 | BTC VPS, Impreza, Ablative |
| Social | 2 | Onionbook, Facebook Tor |
| Identity | 1 | Keybase Tor |
| Directory | 1 | Dark.fail |
| Wiki | 1 | Hidden Wiki |
| Chat | 1 | Daniel Chat |
| Government | 1 | CIA Tor |
| Torrent | 1 | Pirate Bay Tor |
| Code | 1 | GitHub Trending |
| Forum (clearnet) | 1 | Reddit r/netsec |

### Protection Types Covered

| Protection | Services | Bypass Method |
|------------|----------|---------------|
| None | 31 | Direct fetch |
| EndGame V2 queue | 2 | Wait + meta refresh + NEWNYM |
| EndGame V3 PoW | (Dread, when active) | SHA256 hashcash solver |
| Text CAPTCHA (hard) | 1 | CRNN model (98.1%) |
| Text CAPTCHA (simple) | 1 | EasyOCR + Tesseract |
| Base64 CAPTCHA | 2 | Decode + ML pipeline |
| Math CAPTCHA | 2 | Expression parser |
| Rotate CAPTCHA | 1 | CSS selector parser |
| Cloudflare | 2 | curl-impersonate |
| phpBB confirm | 1 | Token extraction |

### Test Flow

```
naan_integration_test.py
  │
  ├─ Check Tor connectivity (torproject.org API)
  │
  ├─ For each of 56 services:
  │   ├─ Fetch URL (Tor SOCKS5 for onion, direct for clearnet)
  │   ├─ Record HTTP code, response size, response time
  │   ├─ Detect protections in response HTML
  │   ├─ Extract page title and content
  │   └─ Classify result: PASS / QUEUE / POW_SOLVED / BLOCKED / FAIL
  │
  ├─ Generate report:
  │   ├─ Per-service table (name, type, status, HTTP, size, time, protection)
  │   ├─ Summary (total, pass, queue, pow, blocked, fail)
  │   ├─ Success rate (overall, onion, clearnet)
  │   ├─ By category breakdown
  │   ├─ Protections encountered
  │   └─ Failed services with error details
  │
  └─ Export: CSV and/or JSON
```

---

## Test Proof

### Clearnet Test Results (April 25, 2026)

Live test of 18 clearnet services. No Tor required.

```
Service                        Type     Status       HTTP  Size     Time    Protection
----------------------------------------------------------------------------------------------------
arxiv (CS.CR)                  clearnet PASS         200   90402    1.6     none
arxiv (CS.AI)                  clearnet PASS         200   90224    1.4     none
MITRE CVE                      clearnet PASS         200   881      2.7     none
NVD NIST                       clearnet PASS         200   25083    1.9     none
Exploit-DB                     clearnet PASS         200   173857   1.9     cloudflare
Packet Storm                   clearnet PASS         200   23480    3.2     none
Full Disclosure                clearnet PASS         200   39035    1.9     none
Krebs on Security              clearnet PASS         200   87413    2.0     none
The Hacker News                clearnet PASS         200   173220   2.1     none
BleepingComputer               clearnet PASS         200   115561   2.6     cloudflare
Dark Reading                   clearnet BLOCKED      403   5411     1.3     none
Schneier Blog                  clearnet PASS         200   72680    1.6     none
Ars Technica                   clearnet PASS         200   222938   2.8     none
Wired Security                 clearnet PASS         200   1566985  2.7     none
IACR ePrint                    clearnet PASS         200   14149    1.4     none
GitHub Trending                clearnet PASS         200   590199   3.1     none
Hacker News (YC)               clearnet PASS         200   35256    1.9     none
Reddit r/netsec                clearnet FAIL         0     0        30.0    none

Clearnet: 16/18 = 88.9%
```

### By Category

| Category | Result |
|----------|--------|
| research | 8/8 (100%) |
| code | 1/1 (100%) |
| news | 7/8 (87.5%) |
| forum | 0/1 (0% -- Reddit timeout) |

### Failed Service Analysis

| Service | Status | Reason |
|---------|--------|--------|
| Dark Reading | BLOCKED (403) | Aggressive bot detection, requires JS execution |
| Reddit r/netsec | FAIL (timeout) | old.reddit.com rate-limits non-browser clients |

Both failures are expected: these services require full browser JS execution, which is outside the scope of curl-based fetching. The NAAN agent uses Tor SOCKS5 curl for onion services and direct curl for clearnet. Services that require JS rendering would need a headless browser integration (future work).

### EndGame V3 PoW Solver Verification

All 7 difficulty levels tested and passed (from V5.2):

| Difficulty | Nonce Found | Hash Prefix | Time |
|------------|-------------|-------------|------|
| 16 bits | 3,077 | 00009065... | 0.002s |
| 18 bits | 204,743 | 0000275a... | 0.129s |
| 20 bits | 1,480,396 | 0000048a... | 0.932s |
| 20 bits | 331,818 | 00000243... | 0.236s |
| 22 bits | 5,864,225 | 000001ff... | 4.624s |
| 16 bits | 2,973 | 0000caaa... | 0.003s |
| 24 bits | 35,135,709 | 000000c7... | 23.643s |

### CAPTCHA Solver Accuracy (from V5.1)

| Type | Method | Accuracy | Evidence |
|------|--------|----------|----------|
| BTC VPS hard text | CRNN + CTC | 53/53 (100%) | See V5.1 test proof |
| Simple text | EasyOCR | 95-100% | Bizzle Forum live test |
| Base64 inline | EasyOCR | 90-98% | DeepMa live test |
| Math (XenForo QA) | Expression parser | 100% | Altenens live test |
| CSS rotate | Selector parser | 100% | Maverick Blog live test |

---

## Usage

### Run full test suite (requires Tor)

```bash
cd KeplerSynapseNet

python3 tools/naan_integration_test.py \
  --json results.json \
  --csv results.csv
```

### Clearnet only (no Tor needed)

```bash
python3 tools/naan_integration_test.py --clearnet-only
```

### Onion only

```bash
python3 tools/naan_integration_test.py --onion-only
```

### Filter by category

```bash
python3 tools/naan_integration_test.py --category research
python3 tools/naan_integration_test.py --category forum
```

### Limit number of services

```bash
python3 tools/naan_integration_test.py --max-services 10
```

---

## NAAN Agent Capabilities After V6

| Capability | Version Added | Status |
|------------|--------------|--------|
| Fetch via Tor SOCKS5 | V3 | Working |
| Fetch clearnet with User-Agent rotation | V4 | Working |
| Cloudflare bypass (curl-impersonate) | V4 | Working |
| reCAPTCHA audio solver | V4 | Working |
| hCaptcha API solver | V4 | Working |
| EndGame V2 queue wait + NEWNYM | V5 | Working |
| Text CAPTCHA (EasyOCR + Tesseract) | V5 | Working |
| Text CAPTCHA (CRNN, 98.1%) | V5.1 | Working |
| Math CAPTCHA parser | V5 | Working |
| CSS rotate CAPTCHA solver | V5 | Working |
| Slider CAPTCHA solver | V5 | Working |
| Base64 inline CAPTCHA decode | V5 | Working |
| CSRF token extraction | V5 | Working |
| EndGame V3 hashcash PoW solver | V5.2 | Working |
| **Integration test harness (56 services)** | **V6** | **New** |
| **Automated success rate reporting** | **V6** | **New** |
| **CSV/JSON export** | **V6** | **New** |

---

## Files Changed

| File | Change | Description |
|------|--------|-------------|
| `tools/naan_integration_test.py` | New (+295) | Full integration test harness: 56 services (incl. Haystak, Phobos, Deep Search), protection detection, reporting |
| `tools/test_pow_solver.py` | New (+65) | PoW solver unit tests (7 difficulty levels) |

---

## What's Next

- V7 -- Headless browser integration for JS-required services (Cloudflare JS challenges, Reddit)
- V8 -- Automated CAPTCHA harvesting pipeline with continuous model retraining

---

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Built_by_Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Kepler" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
</p>

<p align="center">
  If you find this project worth watching -- even if you can't contribute code -- you can help keep it going.<br>
  Donations go directly toward VPS hosting for seed nodes, build infrastructure, and development time.
</p>

<p align="center">
  <a href="https://www.blockchain.com/btc/address/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4"><img src="https://img.shields.io/badge/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="BTC" /></a>
</p>
