<h1 align="center">SynapseNet 0.1.0-alphaV5.2</h1>

<p align="center"><strong>EndGame V3 Hashcash Proof-of-Work Bypass</strong></p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV5.2-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/PoW_Solver-SHA256_Hashcash-000000?style=for-the-badge&labelColor=000000" alt="PoW" />
  <img src="https://img.shields.io/badge/Tests-7%2F7_Passed-000000?style=for-the-badge&labelColor=000000" alt="Tests" />
  <img src="https://img.shields.io/badge/Difficulty-Up_to_24_bits-000000?style=for-the-badge&labelColor=000000" alt="Difficulty" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV5.1"><img src="https://img.shields.io/badge/←_0.1.0--alphaV5.1-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V5.1" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV6"><img src="https://img.shields.io/badge/→_0.1.0--alphaV6-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V6" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai/tree/main/RELEASES"><img src="https://img.shields.io/badge/All_Releases-000000?style=for-the-badge&logo=github&logoColor=white" alt="All Releases" /></a>
</p>

---

> V5.2 adds a hashcash-style Proof-of-Work solver for EndGame V3, the latest DDoS protection system deployed on Dread and other darknet forums. EndGame V2 used a queue-and-wait system that V5 already handles. EndGame V3 adds a computational challenge: the server sends a challenge string and a difficulty (number of leading zero bits required), and the client must find a nonce such that `SHA256(challenge + nonce)` has the required leading zeros. V5.2 detects these challenges automatically, solves them via a fast Python hashlib solver, and submits the proof to obtain a session cookie -- all within the existing `fetchWithRetry` loop.

---

## What Changed from V5.1

| Feature | V5.1 | V5.2 |
|---------|------|------|
| EndGame V2 (queue) | Handled (wait + NEWNYM) | Unchanged |
| EndGame V3 (PoW) | Not supported | **Fully supported** |
| DDoS protection types bypassed | 5 | **6** |
| PoW difficulty supported | N/A | **Up to 24 bits (tested)** |

---

## EndGame V3 Protocol

EndGame V3 is a server-side DDoS protection that requires clients to perform computational work before accessing the site. The protocol:

```
Client                                          Server
  │                                               │
  ├─ GET /page ──────────────────────────────────→ │
  │                                               │
  │ ←── HTML with PoW challenge ──────────────────┤
  │     challenge="eg3_f4e8a1b2c3d5..."           │
  │     difficulty=20                              │
  │     <form action="/pow_verify">                │
  │                                               │
  ├─ Compute: find nonce where                    │
  │   SHA256(challenge + nonce) has               │
  │   ≥20 leading zero bits                       │
  │                                               │
  ├─ POST /pow_verify ───────────────────────────→ │
  │   challenge=...&nonce=1480396                  │
  │                                               │
  │ ←── Session cookie + redirect ────────────────┤
  │                                               │
  ├─ GET /page (with cookie) ────────────────────→ │
  │                                               │
  │ ←── Actual page content ──────────────────────┤
```

### Detection

The detector scans response HTML for any of these markers:

- `proof-of-work`, `proof_of_work`
- `hashcash`, `pow_challenge`, `endgame_pow`, `work_challenge`
- Co-occurrence of `challenge` + `nonce` + `difficulty`

Challenge string extraction searches for:

- `data-challenge="..."`, `challenge="..."`
- JSON `"challenge":"..."`
- `<input name="challenge" value="...">`
- JavaScript assignment `challenge = "..."`

Difficulty extraction from `data-difficulty`, `difficulty=`, or JSON. Default: 20 bits.

### Solver

The solver uses Python's `hashlib.sha256` for speed. For each candidate nonce (0, 1, 2, ...), it computes `SHA256(challenge + str(nonce))` and checks if the first 32 bits have enough leading zeros.

```
solveEndGamePoW(challenge, difficulty)
  │
  ├─ Launch Python subprocess with hashlib
  │
  ├─ for nonce in range(100,000,000):
  │     hash = SHA256(challenge + str(nonce))
  │     leading_zero_bits = count_leading_zeros(hash[:8])
  │     if leading_zero_bits >= difficulty:
  │         return nonce
  │
  └─ Return nonce string (or empty on failure)
```

### Submission

Once a valid nonce is found, it is POST'd back to the form action with the challenge and any CSRF/session tokens extracted from the page. If the response no longer contains a PoW challenge, the content is returned. Otherwise, the retry loop continues.

---

## Integration in fetchWithRetry

The PoW check runs after EndGame V2 queue detection and before CAPTCHA detection:

```
fetchWithRetry(url)
  │
  ├─ Fetch page
  │
  ├─ EndGame V2 queue? → wait + NEWNYM (existing V5 logic)
  │
  ├─ EndGame V3 PoW? ← NEW
  │   ├─ detectEndGameV3(html, url)
  │   │   ├─ Extract challenge string
  │   │   ├─ Extract difficulty
  │   │   ├─ Extract form action + CSRF tokens
  │   │   └─ Return EndGameV3Challenge struct
  │   │
  │   ├─ solveEndGamePoW(challenge, difficulty)
  │   │   └─ Return nonce (SHA256 brute force)
  │   │
  │   ├─ submitEndGamePoW(challenge, nonce)
  │   │   └─ POST to form action with cookies
  │   │
  │   └─ Verify response has no more PoW → return content
  │
  ├─ Clearnet protection? → Cloudflare / reCAPTCHA / hCaptcha
  │
  ├─ Darknet CAPTCHA? → text / math / rotate / slider / etc.
  │
  └─ No protection → return content
```

---

## Test Proof

All results below are real -- captured on April 25, 2026. Solver tested on CPU (Apple Silicon).

### PoW Solver Verification (7/7 Passed)

```
EndGame V3 Hashcash PoW Solver Test
======================================================================

Test 1/7: difficulty=16 bits
  Challenge: endgame_v3_test_challenge_abc123
  PASS: nonce=3077 hash=00009065dc1fa7f2... bits=16 time=0.002s

Test 2/7: difficulty=18 bits
  Challenge: dread_pow_session_xyz789
  PASS: nonce=204743 hash=0000275a7df3a042... bits=18 time=0.129s

Test 3/7: difficulty=20 bits
  Challenge: endgame_challenge_2026_04_25
  PASS: nonce=1480396 hash=0000048ad571f242... bits=21 time=0.932s

Test 4/7: difficulty=20 bits
  Challenge: hashcash_1:20:260425:dreadytofatroptsdj6io7l:::
  PASS: nonce=331818 hash=000002436ee33271... bits=22 time=0.236s

Test 5/7: difficulty=22 bits
  Challenge: pow_challenge_random_f4e8a1b2c3d5
  PASS: nonce=5864225 hash=000001ffc6d609c3... bits=23 time=4.624s

Test 6/7: difficulty=16 bits
  Challenge: eg3_3aafc067df5987b45912d10f774d06fb
  PASS: nonce=2973 hash=0000caaa47f1f159... bits=16 time=0.003s

Test 7/7: difficulty=24 bits
  Challenge: eg3_e5698238f3caeaf18cd9bb4a12d43f14
  PASS: nonce=35135709 hash=000000c7f164dd76... bits=24 time=23.643s

======================================================================
Total time: 29.568s
ALL 7 TESTS PASSED
```

### Performance by Difficulty

| Difficulty (bits) | Expected Hashes | Avg Solve Time | Test Result |
|-------------------|----------------|----------------|-------------|
| 16 | ~65,536 | <10ms | 0.002-0.003s |
| 18 | ~262,144 | ~130ms | 0.129s |
| 20 | ~1,048,576 | ~0.6s | 0.236-0.932s |
| 22 | ~4,194,304 | ~4.6s | 4.624s |
| 24 | ~16,777,216 | ~24s | 23.643s |

Dread typically uses difficulty 18-22. At difficulty 20, the solver finds a valid nonce in under 1 second.

### Hash Verification

Every solved nonce was independently verified:

```
SHA256("endgame_v3_test_challenge_abc123" + "3077") = 00009065dc1fa7f2...
  Binary: 0000 0000 0000 0000 1001 0000 0110 0101 ...
  Leading zero bits: 16 ✓ (required: 16)

SHA256("endgame_challenge_2026_04_25" + "1480396") = 0000048ad571f242...
  Binary: 0000 0000 0000 0000 0000 0100 1000 1010 ...
  Leading zero bits: 21 ✓ (required: 20)

SHA256("eg3_e5698238f3caeaf18cd9bb4a12d43f14" + "35135709") = 000000c7f164dd76...
  Binary: 0000 0000 0000 0000 0000 0000 1100 0111 ...
  Leading zero bits: 24 ✓ (required: 24)
```

---

## DDoS Protection Stack After V5.2

| Protection | Version | Method | Status |
|------------|---------|--------|--------|
| EndGame V2 | Dread/Pitch | Queue wait + meta refresh + NEWNYM | Bypassed (V5) |
| **EndGame V3** | **Dread (new)** | **Hashcash SHA256 PoW** | **Bypassed (V5.2)** |
| Cloudflare | Various | curl-impersonate-chrome | Bypassed (V4) |
| reCAPTCHA | Various | Audio challenge solver | Bypassed (V4) |
| hCaptcha | Various | API-based solve | Bypassed (V4) |
| Generic DDoS | Various | Wait + retry + cookies | Bypassed (V5) |
| Text CAPTCHA | BTC VPS | CRNN model (98.1%) | Bypassed (V5.1) |
| Math CAPTCHA | XenForo | Expression parser | Bypassed (V5) |
| Rotate CAPTCHA | anCaptcha | CSS selector parser | Bypassed (V5) |
| Slider CAPTCHA | anCaptcha | Image offset detection | Bypassed (V5) |

---

## Files Changed

| File | Change | Description |
|------|--------|-------------|
| `src/ide/synapsed_engine.h` | +13 lines | EndGameV3Challenge struct, detectEndGameV3, solveEndGamePoW, submitEndGamePoW declarations |
| `src/ide/synapsed_engine.cpp` | +130 lines | PoW detection (multi-pattern), solver (Python hashlib), submission, integration in fetchWithRetry |
| `tools/test_pow_solver.py` | New (+65) | Unit tests for PoW solver: 7 difficulty levels, hash verification |

---

## What's Next

- V6 -- Full NAAN agent integration testing across 50+ darknet services with automated success rate reporting

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
