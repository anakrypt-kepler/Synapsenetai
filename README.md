<p align="center">
  <img src="header.gif" width="420" />
</p>

<h1 align="center">⛏ SynapseNet</h1>

<p align="center"><strong>Decentralized AI Mining with Proof of Emergence</strong></p>

<p align="center">
  <em>"Satoshi gave us money without banks. I will give you brains without corporations."</em> — Kepler
</p>

<p align="center">
  <img src="https://img.shields.io/badge/SynapseNet-0.1.0--alpha-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-000000?style=for-the-badge&labelColor=000000" alt="License" /></a>
  <img src="https://img.shields.io/badge/Status-Active_Development-000000?style=for-the-badge&labelColor=000000" alt="Status" />
</p>

---

<p align="center">
  <img src="https://raw.githubusercontent.com/anakrypt/anakrypt/main/mining.png" alt="SynapseNet" />
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
</p>

---

> **Alpha Release**
>
> This is the alpha version of SynapseNet. The codebase has been developed locally since 2023, outside of GitHub — this is its first public release. The code is open for anyone to explore: look at the architecture, run it locally, see how mining works on a local devnet, trace the code structure and functions. This is not production-ready. Expect bugs. The long-term goal is to move everything from the terminal into a proper graphical interface so it's accessible to newcomers, not just people comfortable with CLI tools. Right now you can build it, poke around, break things, and report what you find. Beta is still a ways out — there's a lot of work left to get the UX where it needs to be.
>
> The website and VPS infrastructure are currently in development. Once the alpha release is finished, the public rollout will follow soon. Until then, I am continuing to stabilize the alpha, fix bugs, ship hardening updates, and add new improvements.

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

## Core Architecture

```
KeplerSynapseNet/
  src/
    main.cpp             Node orchestration, RPC, P2P dispatch
    network/             Socket layer + peer discovery + sync
    core/                Ledger, Transfer, Knowledge, PoE v1, Consensus
    model/               Model loading / inference / marketplace
    web/                 Web4 search + Tor + context injection
    tui/                 ncurses terminal UI
  include/               Public headers
  tests/                 C++ tests (ctest, 267 passing)
  ide/                   IDE engine (C++ — agent, session, LSP, MCP)
  third_party/
    llama.cpp            Local LLM inference engine
```

**synapsed** — C++ node daemon  
P2P networking, PoE v1 consensus, NGT ledger, local GGUF model inference, wallet management, optional Tor routing, ncurses TUI, integrated IDE engine with agent coordinator, LSP client, and MCP server.

**VS Code extension** — `ide/synapsenet-vscode/`  
GitHub Quests workflow, chat panel with Web4 injection, remote model sessions.

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
  <img src="https://img.shields.io/badge/Go-000000?style=for-the-badge&logo=go&logoColor=white" alt="Go" />
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

### 0.1.0-alphaV4 (In Development)

- Eliminated all Go code from the project; every component formerly in crush-main is now native C++
- Added IDE engine: agent coordinator, tool suite (bash, edit, grep, glob, fetch, write, download, web search), session management, config, patch, skills, LSP client, MCP server, and OAuth
- Exposed synapsed as a shared library (libsynapsed) with a stable C ABI for Tauri FFI integration
- Removed crush-main directory, go.mod, go.sum, and all Go build targets from CI
- Planned next: Tauri desktop application with Svelte frontend consuming libsynapsed

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
