<p align="center">
  <img src="header.gif" width="420" />
</p>

<h1 align="center">⛏ SynapseNet</h1>

<p align="center"><strong>Decentralized AI Mining with Proof of Emergence</strong></p>

<p align="center">
  <em>"Satoshi gave us money without banks. I will give you brains without corporations."</em> — Kepler
</p>

<p align="center">
  <img src="https://img.shields.io/badge/SynapseNet-0.1.0--alphaV8-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-000000?style=for-the-badge&labelColor=000000" alt="License" /></a>
  <img src="https://img.shields.io/badge/Status-Active_Development-000000?style=for-the-badge&labelColor=000000" alt="Status" />
</p>

---

<p align="center">
  <img src="mining.png" alt="SynapseNet" />
</p>

<h3 align="center">Navigation</h3>

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://discord.gg/wGhkWgHK"><img src="https://img.shields.io/badge/Discord-000000?style=for-the-badge&logo=discord&logoColor=white" alt="Discord" /></a>
  <a href="https://github.com/anakrypt/SynapseNet"><img src="https://img.shields.io/badge/SynapseNet_Docs-000000?style=for-the-badge&logo=gitbook&logoColor=white" alt="Docs" /></a>
  <a href="https://github.com/anakrypt/SynapseNet/blob/main/SynapseNet_Whitepaper.pdf"><img src="https://img.shields.io/badge/Whitepaper-000000?style=for-the-badge&logo=adobeacrobatreader&logoColor=white" alt="Whitepaper" /></a>
  <a href="CONTRIBUTING.md"><img src="https://img.shields.io/badge/Contributing-000000?style=for-the-badge&logo=opensourceinitiative&logoColor=white" alt="Contributing" /></a>
  <a href="RELEASES/0.1.0-alpha"><img src="https://img.shields.io/badge/0.1.0--alpha-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alpha" /></a>
  <a href="RELEASES/0.1.0-alphaV2"><img src="https://img.shields.io/badge/0.1.0--alphaV2-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="0.1.0-alphaV2" /></a>
  <a href="RELEASES/0.1.0-alphaV3"><img src="https://img.shields.io/badge/0.1.0--alphaV3-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="0.1.0-alphaV3" /></a>
  <a href="RELEASES/0.1.0-alphaV3.5"><img src="https://img.shields.io/badge/0.1.0--alphaV3.5-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="0.1.0-alphaV3.5" /></a>
  <a href="RELEASES/0.1.0-alphaV3.6"><img src="https://img.shields.io/badge/0.1.0--alphaV3.6-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alphaV3.6" /></a>
  <a href="RELEASES/0.1.0-alphaV3.7"><img src="https://img.shields.io/badge/0.1.0--alphaV3.7-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alphaV3.7" /></a>
  <a href="RELEASES/0.1.0-alphaV4"><img src="https://img.shields.io/badge/0.1.0--alphaV4-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alphaV4" /></a>
  <a href="RELEASES/0.1.0-alphaV5"><img src="https://img.shields.io/badge/0.1.0--alphaV5-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alphaV5" /></a>
  <a href="RELEASES/0.1.0-alphaV5.1"><img src="https://img.shields.io/badge/0.1.0--alphaV5.1-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alphaV5.1" /></a>
  <a href="RELEASES/0.1.0-alphaV5.2"><img src="https://img.shields.io/badge/0.1.0--alphaV5.2-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alphaV5.2" /></a>
  <a href="RELEASES/0.1.0-alphaV6"><img src="https://img.shields.io/badge/0.1.0--alphaV6-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alphaV6" /></a>
  <a href="RELEASES/0.1.0-alphaV7"><img src="https://img.shields.io/badge/0.1.0--alphaV7-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alphaV7" /></a>
  <a href="RELEASES/0.1.0-alphaV8"><img src="https://img.shields.io/badge/0.1.0--alphaV8-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="0.1.0-alphaV8" /></a>
</p>

---

> **Alpha Release — V8**
>
> This is the alpha version of SynapseNet. The codebase has been developed locally since 2023, outside of GitHub — this is its first public release. V8 is the latest milestone: 14 internal CVEs cataloged (NAAN-CVE-2026-0001 through 0014), 12 active exploit implementations in the fetch pipeline, a knowledge harvester that extracts files/images/text and scans them against VirusTotal, a HARVEST tab in both the desktop app and terminal UI, and an LLM captcha fallback solver that feeds unsolved challenges to the local GGUF model as a last resort. The agent crawls via Tor (Torch, Ahmia, Haystak), identifies protection weaknesses in real-time, and exploits them for knowledge extraction. The code is open for anyone to explore: look at the architecture, run it locally, see how mining works on a local devnet, trace the code structure and functions. This is not production-ready. Expect bugs. Right now you can build it, poke around, break things, and report what you find. Beta is still a ways out — there's a lot of work left to get the UX where it needs to be.
>
> The website and VPS infrastructure are currently in development. Seed nodes will be available over Tor hidden services. Until then, I am continuing to stabilize the alpha, fix bugs, ship hardening updates, and add new improvements.

---

## What Is This

SynapseNet is a decentralized peer-to-peer network where nodes **mine intelligence instead of hashes**. Think Bitcoin, but for knowledge. Contributors feed useful data into an open network, every local AI can draw from it, and contributions are rewarded with **NGT** (Neural Gold Token) through a consensus mechanism called **Proof of Emergence**.

This is the full source repository — the node daemon (`synapsed`), CI pipelines, tests, and all architecture documents.

---

## NAAN — Node-Attached Autonomous Agent Network

Every SynapseNet node runs a local autonomous agent in the background. One node, one agent. The agent belongs to the network, not the user — its job is to improve the collective knowledge base.

**What the agent does:**
- Researches topics autonomously using its local AI model
- Drafts knowledge contributions and queues them for PoE validation
- Validates other nodes' submissions through deterministic scoring
- Mines NGT rewards by producing accepted knowledge entries

**Where it can go:**
- **Clearnet** — standard web search and data gathering (opt-in, off by default)
- **Tor / .onion** — routed through Tor for privacy-first research. Supports managed Tor runtime, external Tor daemons, and obfs4 bridge configurations
- **Local knowledge chain** — reads and cross-references the full local copy of the network's knowledge base

**Why this is Web4:**
Web1 was read. Web2 was read-write. Web3 was read-write-own. **Web4 is read-write-own-think** — your node doesn't just store data, it runs a local AI that reasons over a decentralized knowledge network, contributes back, and earns for it. No cloud API, no corporate middleman. The intelligence runs on your machine, talks to the network over P2P (optionally through Tor), and the knowledge chain grows like a blockchain but stores intelligence instead of transactions.

**Tor integration:**
- The agent can route all outbound research through Tor SOCKS5 proxy
- Supports `.onion` site crawling for censorship-resistant knowledge gathering
- Managed Tor runtime — SynapseNet can start/stop its own Tor process
- External Tor — works with Tor Browser or system Tor on port `9150` / `9050`
- Bridge support — paste obfs4 bridges for regions where Tor is blocked
- Fail-closed behavior — if Tor is required but unavailable, the agent stops rather than leaking clearnet traffic

---

## Where the NAAN Agent Goes and What It Takes

Starting in V5 and finalized in V7, the NAAN agent is no longer a passive crawler. During every mining tick it autonomously walks a target list that mixes clearnet research sources and `.onion` services, and it carries a full V5 + V6 + V7 bypass chain wired directly into `fetchWithRetry`.

**What the agent reaches for:**

- **Open clearnet research** — arXiv, IACR ePrint, Schneier on Security, Krebs on Security, PacketStorm, GitHub trending, Hugging Face papers feed, BBC, ProPublica, Wikileaks
- **Cloudflare-fronted security press** — Dark Reading, BleepingComputer, The Hacker News, NVD/NIST. Detected via `__cf_bm`, `cf-mitigated`, `challenge-platform`, handled by curl-impersonate, `__cf_bm` 30-minute replay, and the no-JS POST clearance flow
- **Sucuri / CloudProxy origins** — Exploit-DB, handled by XSRF token cache replay
- **Darknet search engines via Tor** — Torch, Ahmia, Haystak, Tordex, Tor66, DarkSearch, Excavator, OnionLand, Phobos, Deep Search, Brave Tor
- **EndGame V2 + V3 protected forums** — Dread, Pitch, and similar. Handled by hashcash PoW solver, queue race with NEWNYM circuit rotation, and PoW cookie replay
- **anCaptcha / image / math / rotate / slider / pair / multi-step CAPTCHA pages** — handled by CRNN solver (98.1% exact match on real darknet samples), CSS-selector leak, EasyOCR + Tesseract + TrOCR fallbacks
- **High-value reference sites on Tor** — DuckDuckGo `.onion`, BBC `.onion`, ProtonMail `.onion`, Dark.fail, OnionDir

**What it takes back to the network:**

- Page titles, headlines, and short content snippets it can extract via the local model
- A SHA-256 digest of `topic | title | cve | timestamp` so the PoE draft is bound to the bypass identity that produced it
- A signed Ed25519 attestation of the draft
- A `bypass{}` block on every persisted draft: CVE id, protection class, method, transport, TTFB, response size — never the URL, hostname, IP, cookie, or session token

This is the line: **the agent extracts knowledge, not identity.** It does not exfiltrate user data, credentials, private content, or anything that belongs to a person. It walks the public surface of services that already publish information to anyone who can solve their challenge, and it brings that public information back into a knowledge chain anyone can read.

### Why this is not a contradiction with bypassing protections

Bypass research and free knowledge are the same project, not opposite ones.

Every protection layer described in [`RELEASES/0.1.0-alphaV7/README.md`](RELEASES/0.1.0-alphaV7/README.md) — Cloudflare bot management, Sucuri, EndGame V2/V3, anCaptcha, CF managed challenges, cross-service cookie isolation — exists for one reason: to convert public information into rented information. The page is still public. The article is still indexable. The forum thread still describes the world. The protection only filters who is allowed to read it cheaply at scale.

I do not accept that filter.

The position behind SynapseNet is simple:

> **Knowledge should be available to everyone. That is exactly why these techniques are invented.**

If a model can plan, then the model can plan the route around the gatekeeper. If an SDK can solve a CAPTCHA, then the SDK can be used to lift the same CAPTCHA off a research mirror. If a cloud API can rewrite code, then the cloud API can rewrite the code that walks past its own throttles. The corporations gave us the tools. We use those tools to make sure the knowledge they sit on top of stays public, mirrored, and reachable from a node that a single takedown notice cannot turn off.

The arms race is real. CAPTCHA vendors will rotate. EndGame will ship V4. Cloudflare will tighten `__cf_bm`. Sucuri will rebind XSRF. Each of those moves becomes a new NAAN-CVE entry, a new exploit method in `synapsed_engine.cpp`, and a new line in the next release notes.

**The arms race will be on our side, because every node that joins the network is a new pair of eyes, a new circuit, a new agent, and a new place where the next bypass can be tested in production. One vendor on one side, thousands of autonomous nodes on the other. That asymmetry is the whole point.**

That is what every CVE and every exploit in the V5 → V7 chain is for. Not to deface a service. Not to steal a user. To make sure the knowledge a few companies want to gate stays open, indexed, and available to any node running this software, anywhere in the world, on any link.

---

## Core Architecture

```
KeplerSynapseNet/
  src/
    main.cpp             Node entry point
    node/                SynapseNet node daemon (synapsed)
    network/             Socket layer + peer discovery + Tor mesh
    core/                Ledger, Transfer, Knowledge, PoE v1, Consensus
    crypto/              secp256k1, AES-256-GCM, post-quantum Kyber/Dilithium
    model/               Model loading / inference / marketplace
    web/                 Web4 search + Tor + context injection
    ide/                 IDE engine — agent, session, LSP, MCP, OAuth, FFI
    tui/                 ncurses terminal UI
  include/               Public headers + synapsed_ffi.h (C ABI)
  tests/                 C++ tests (ctest, 267 passing)
  tauri-app/             Desktop app — Rust (Tauri) + Svelte frontend
  third_party/
    llama.cpp            Local LLM inference engine
```

**synapsed** — C++ node daemon  
P2P networking over Tor-only mesh, PoE v1 consensus, NGT ledger, local GGUF model inference, wallet management, Tor hidden service routing, ncurses TUI, integrated IDE engine with agent coordinator, LSP client, and MCP server. Exposes `libsynapsed` shared library with stable C ABI for Tauri FFI.

**Tauri desktop app** — `tauri-app/`  
Svelte frontend, Rust FFI bridge to libsynapsed, dashboard, wallet, knowledge explorer, NAAN agent panel, settings.

**VS Code extension** — `ide/synapsenet-vscode/`  
GitHub Quests workflow, chat panel with Web4 injection, remote model sessions. Talks directly to synapsed C++ — no Go middleman.

---

## Proof of Emergence (PoE v1)

The consensus mechanism. Unlike Proof of Work (burn electricity) or Proof of Stake (lock capital), PoE rewards **useful knowledge contributions**.

- Deterministic scoring — all nodes compute the same result, no LLM-based consensus
- PoW gate — submissions require a small proof-of-work to prevent spam
- Validator votes — randomly selected validators score each submission
- Epoch finalization — accepted entries earn NGT rewards
- Code contributions — submit patches through the IDE, earn NGT after review

---

## Build

```bash
# Dependencies (Ubuntu)
sudo apt-get install build-essential cmake libssl-dev libncurses-dev libsqlite3-dev

# Build
cmake -S KeplerSynapseNet -B KeplerSynapseNet/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build KeplerSynapseNet/build --parallel 8

# Test
ctest --test-dir KeplerSynapseNet/build --output-on-failure

# Run
TERM=xterm-256color ./KeplerSynapseNet/build/synapsed
```

---

## Docker

```bash
docker compose up --build
```

See [DOCKER.md](KeplerSynapseNet/DOCKER.md) for configuration.

---

## Key Bindings

| Key | Action |
|-----|--------|
| `Space` | Continue boot |
| `1-9` | Dashboard shortcuts |
| `0` | Agent Network observatory |
| `A` | Attached Agent status |
| `Tab` | Model panel |
| `F4` | Download model |
| `F5` | Toggle web injection |
| `F6` | Toggle onion sources |
| `F7` | Toggle Tor for clearnet |
| `I` | Launch Terminal IDE |
| `F3` | Clear chat |
| `F8` | Stop generation |

---

## Documentation

Full architecture docs are in `interfaces txt/`. For the organized documentation index and whitepaper, see the docs repository.

<p align="center">
  <a href="https://github.com/anakrypt/SynapseNet"><img src="https://img.shields.io/badge/Documentation_Index-000000?style=for-the-badge&logo=gitbook&logoColor=white" alt="Docs" /></a>
  <a href="https://github.com/anakrypt/SynapseNet/blob/main/SynapseNet_Whitepaper.pdf"><img src="https://img.shields.io/badge/Whitepaper_PDF-000000?style=for-the-badge&logo=adobeacrobatreader&logoColor=white" alt="Whitepaper" /></a>
  <a href="interfaces%20txt/WHY_SYNAPSENET.txt"><img src="https://img.shields.io/badge/Why_SynapseNet_Exists-000000?style=for-the-badge&logo=readme&logoColor=white" alt="Why SynapseNet" /></a>
</p>

---

## Built With

<p align="center">
  <img src="https://img.shields.io/badge/C++-000000?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++" />
  <img src="https://img.shields.io/badge/C-000000?style=for-the-badge&logo=c&logoColor=white" alt="C" />
  <img src="https://img.shields.io/badge/Rust-000000?style=for-the-badge&logo=rust&logoColor=white" alt="Rust" />
  <img src="https://img.shields.io/badge/TypeScript-000000?style=for-the-badge&logo=typescript&logoColor=white" alt="TypeScript" />
  <img src="https://img.shields.io/badge/Svelte-000000?style=for-the-badge&logo=svelte&logoColor=white" alt="Svelte" />
  <img src="https://img.shields.io/badge/Tauri-000000?style=for-the-badge&logo=tauri&logoColor=white" alt="Tauri" />
  <img src="https://img.shields.io/badge/CMake-000000?style=for-the-badge&logo=cmake&logoColor=white" alt="CMake" />
  <img src="https://img.shields.io/badge/Docker-000000?style=for-the-badge&logo=docker&logoColor=white" alt="Docker" />
  <img src="https://img.shields.io/badge/Tor-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="Tor" />
</p>

---

## Development Platforms

<p align="center">
  <img src="https://img.shields.io/badge/macOS_M2_256GB-000000?style=for-the-badge&logo=apple&logoColor=white" alt="macOS" />
  <img src="https://img.shields.io/badge/Arch_Linux-000000?style=for-the-badge&logo=archlinux&logoColor=white" alt="Arch Linux" />
  <img src="https://img.shields.io/badge/Android_+_Ubuntu-000000?style=for-the-badge&logo=android&logoColor=white" alt="Android Ubuntu" />
</p>

<p align="center">
  I use my phone running Ubuntu on Android to write code on the go — stays connected to the project 24/7, online and locally.
</p>

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Code contributions can be submitted as PoE v1 entries and earn NGT after epoch finalization.

---

## Changelog

### 0.1.0-alphaV7 (April 29 – May 4, 2026)

- Autonomous vulnerability discovery engine: crawls darknet/clearnet services via Tor, identifies protection weaknesses
- 10 internal CVEs cataloged (NAAN-CVE-2026-0001 through NAAN-CVE-2026-0010)
- 8 active exploit implementations: PoW cookie replay, queue race, CSS selector leak, CF `__cf_bm` replay, Sucuri XSRF cache replay, CF managed challenge bypass, timing oracle, cookie jar confusion
- `detectVulnerability()` auto-classifier with confidence scoring (0.0–1.0)
- `CookiePool` system: caches solved PoW sessions, `__cf_bm` cookies, and cross-service sessions with TTL tracking
- Integrated into `fetchWithRetry()`: pre-checks cookie pool, measures TTFB, auto-exploits if confidence > 0.8, falls back to V5/V6 chain
- Added `tools/naan_vuln_scanner.py` — standalone scanner with CVE catalog, live testing, JSON export
- **Mining-loop integration**: full V5 + V6 + V7 bypass chain is now invoked autonomously inside `SynapsedEngine::naanLoop`. Every successful bypass is reported through `recordBypass(cve, protection, method, transport, ttfbMs, httpCode, bytes)` and stamped into the in-memory log entry, the persisted PoE draft JSON (`bypass{}` block), the SHA-256 of the draft payload, and a `naan.bypass` event broadcast to RPC subscribers. `naanStatus()` exposes `bypass_counters` and `last_bypass` for dashboard rendering.
- Added `BypassReport` struct, `primeCookieJar()` (one-shot clearnet seeding for CVE-0009), and `emitEvent()` event dispatcher
- Added `tools/naan_mining_runner.py` — sanitized end-to-end mining-loop runner that mirrors the C++ pipeline (topic → `topicToUrl` → `fetchWithRetry` → `detectVulnerability` → `exploitCVE*` → `recordBypass` → `persistDraft`); writes drafts containing only CVE id, protection class, method, transport, TTFB, and bytes — no IPs, hostnames, cookies, or session tokens
- Live test results: 14 mining ticks, 11 accepted (78.6% approval), CVE-0002 (EndGame V2 queue with NEWNYM rotation) triggered 3×, CVE-0010 (zero-protection direct extraction) triggered 5×
- **Knowledge Harvester**: NAAN agent now extracts images, files, and readable text from every page it visits during mining. `extractAssets()` parses `<img>`, `<meta og:image>`, and `<a href>` for downloadable assets (`.pdf`, `.doc`, `.zip`, `.png`, `.jpg`, etc.). `downloadAsset()` saves to `knowledge/assets/<sha256>.<ext>` via Tor SOCKS5 or clearnet (10 MB cap per file, 5 files per tick, EXIF stripped from images). `vtScanFile()` checks every downloaded file against VirusTotal v3 API by SHA-256 hash — clean files are kept, flagged files are moved to `knowledge/quarantine/`. `persistHarvest()` writes a full harvest JSON with anonymized metadata (no URLs, hostnames, IPs, cookies — only CVE, method, transport, TTFB, bytes, and `sha256(nodeId)`).
- New **HARVEST** tab in Tauri Desktop App: scrollable list of harvested entries with topic badges, CVE indicators, asset counts, and VirusTotal status dots. Detail view shows readable text, image grid (thumbnails from local paths via `convertFileSrc`), file list with VT verdicts, bypass metadata card, and anonymized node/draft hashes.
- New **HARVEST** screen in ncurses TUI (`H` key from Dashboard or NAAN agent): scrollable table with timestamp, topic, title, CVE, asset count, and VT status. Detail panel shows bypass metadata and text preview. Harvest file scanner in `tui_runtime.cpp` reads `knowledge/harvest_*.json` every 5 seconds.
- RPC: added `harvest.list` (paginated, newest first, text truncated to 200 chars) and `harvest.get` (full harvest JSON by SHA-256)

### 0.1.0-alphaV6 (April 25, 2026)

- Full NAAN agent integration test harness covering 56 live services (38 onion, 18 clearnet)
- Automated per-service status reporting with protection detection, timing, and content extraction
- Test coverage across 12 darknet search engines: Torch, Ahmia, DuckDuckGo, Tordex, Tor66, DarkSearch, Excavator, Haystak, OnionLand, Phobos, Deep Search, Brave
- CSV and JSON export for automated success rate tracking
- Clearnet test result: 16/18 passed (88.9%)
- Added `tools/naan_integration_test.py` — run with `--onion-only`, `--clearnet-only`, `--category`, `--json`

### 0.1.0-alphaV5.2 (April 25, 2026)

- EndGame V3 hashcash Proof-of-Work bypass — fully automated detection and solving
- `detectEndGameV3()`: multi-pattern challenge extraction (HTML attributes, JSON, JS assignments, form inputs)
- `solveEndGamePoW()`: SHA256 brute-force nonce finder via Python hashlib (supports difficulty 16–24 bits)
- `submitEndGamePoW()`: POST nonce + CSRF tokens back to form action with Tor cookies
- Integrated into `fetchWithRetry()` between EndGame V2 queue handler and CAPTCHA detection
- 7/7 difficulty levels solved in testing (0.002s at 16 bits, 23.6s at 24 bits)
- Added 3 new search engines to NAAN agent: Haystak, Phobos, Deep Search (16 total in rotation)

### 0.1.0-alphaV5.1 (April 24, 2026)

- CRNN CAPTCHA solver: CNN (4 blocks) → BiGRU → CTC loss architecture
- 98.1% exact match accuracy, 99.5% per-character accuracy on training set
- 53/53 (100%) real darknet CAPTCHAs solved (BTC VPS style, busy Bitcoin background)
- Model size: 2.7 MB (`captcha_crnn_v6.pt`)
- Replaced fixed-length CrossEntropy approach with variable-length CTC decoding
- Heavy data augmentation: real data oversampled 50x, 5000 synthetic samples at 80% hard ratio

### 0.1.0-alphaV5 (April 21, 2026)

- NAAN agent CAPTCHA bypass: text, math, rotate, slider, multi-step, clock, hieroglyph, odd-one-out
- EndGame V2 DDoS queue detection and bypass (wait + meta refresh + Tor NEWNYM circuit rotation)
- CNN-based text CAPTCHA solver with EasyOCR + Tesseract fallback
- anCaptcha CSS rotate puzzle solver (8 rotation options, encrypted stateless tokens)
- Live-tested on 7 real darknet services via Tor
- DDoS protection bypass for Dread and Pitch forums

### 0.1.0-alphaV4 (April 19, 2026)

- Eliminated all Go code from the project; every component formerly in crush-main is now native C++
- Added IDE engine: agent coordinator, tool suite (bash, edit, grep, glob, fetch, write, download, web search), session management, config, patch, skills, LSP client, MCP server, and OAuth
- Exposed synapsed as a shared library (libsynapsed) with a stable C ABI (`synapsed_ffi.h`) for Tauri FFI integration
- Built Tauri desktop application with Svelte frontend, Rust FFI bridge, and full node dashboard
- Removed crush-main directory, go.mod, go.sum, and all Go build targets from CI
- Implemented Tor-only decentralized peer discovery — every node acts as server via hidden service, no VPS required
- Added `get_onion_peers` / `onion_peers` wire protocol for .onion peer address exchange
- Automatic Tor binary provisioning via CMake bootstrap script
- Fixed 12 bugs: deserialization bounds checking across all wire message types, peerHeights data race, MemoryPool accounting leak, static rate-limiter maps unbounded growth, macOS memory reporting, getDiskUsage crash on missing dirs, secp256k1 context thread safety

### 0.1.0-alphaV3.7 (March 27, 2026)

- Security hardening release covering 18 audited fixes across cryptography, consensus, RPC, networking, sandboxing, updates, and model download paths
- Replaced custom XOR-based session crypto with AES-256-GCM and removed the legacy XOR wallet loading path
- Enforced signed consensus votes, hardened RPC handling, added replay protection, SOCKS5 auth support, DNS timeout handling, and PBKDF2 increase to 100,000 iterations
- Added sandbox verification reports under `RELEASES/0.1.0-alphaV3.7/verification/`

### 0.1.0-alphaV3.6 (March 26, 2026)

- Modularized `main.cpp` from 4,809 lines to 117 lines (separation of concerns)
- Extracted `SynapseNet` class into `src/node/synapse_net.cpp` with opaque factory API
- Zero behavior change — all 267 tests pass identically

### 0.1.0-alphaV3.5 (March 26, 2026)

- Real Ed25519 signatures via libsodium (replaced SHA-256 simulation)
- Real X25519 key exchange via libsodium (replaced fake DH)
- CSPRNG via libsodium `randombytes_buf` (replaced Mersenne Twister)
- Wallet encryption routed by SecurityLevel (STANDARD / HIGH / QUANTUM_READY)
- libsodium added as required dependency

### 0.1.0-alphaV3 (March 25, 2026)

- Hybrid Tor + clearnet mesh: nodes on different transports see each other
- Clearnet nodes connect to `.onion` peers via SOCKS5 automatically
- Tor nodes accept inbound from clearnet through hidden service
- `hybridMode` enabled automatically when SOCKS proxy is available
- 267/267 tests passing

### 0.1.0-alphaV2 (March 25, 2026)

- 3-node devnet running exclusively over Tor hidden services
- All P2P connections routed through Tor SOCKS5 — zero clearnet traffic
- Each node reachable only via its `.onion` address
- Fail-closed behavior: if Tor is unreachable, outbound P2P is blocked
- Automated launch script: starts 3 Tor instances, generates `.onion` addresses, seeds nodes

### 0.1.0-alpha (March 25, 2026)

- Fixed OQS_SIG_verify parameter order in Dilithium and SPHINCS+ post-quantum signature verification
- Fixed inbound peer address resolution for dual-stack IPv6 sockets — peers were showing 0.0.0.0 instead of actual IP, breaking loopback peer discovery in regtest mode
- Full 3-node local devnet tested and verified — all nodes connect and exchange peers
- 267/267 tests passing, 610/610 build targets

---

## Support

If you find this project worth watching — even if you can't contribute code — you can help keep it going. Donations go directly toward VPS hosting for seed nodes, build infrastructure, and development time.

<p align="center">
  <a href="https://www.blockchain.com/btc/address/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4"><img src="https://img.shields.io/badge/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="BTC" /></a>
</p>

---

## License

[MIT](LICENSE) — Copyright (c) 2026 KeplerSynapseNet

---

## Inspired By

<p align="center">
  <a href="https://bitcoin.org/bitcoin.pdf"><img src="https://img.shields.io/badge/Satoshi_Nakamoto-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="Satoshi Nakamoto" /></a>
  <a href="https://www.torproject.org"><img src="https://img.shields.io/badge/The_Tor_Project-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="The Tor Project" /></a>
  <a href="https://thepiratebay.org"><img src="https://img.shields.io/badge/The_Pirate_Bay_/_Anakata-000000?style=for-the-badge&logo=piracy&logoColor=white" alt="The Pirate Bay / Anakata" /></a>
  <a href="https://www.getmonero.org"><img src="https://img.shields.io/badge/Monero-000000?style=for-the-badge&logo=monero&logoColor=white" alt="Monero" /></a>
</p>

---

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Built_by_Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Kepler" /></a>
</p>
