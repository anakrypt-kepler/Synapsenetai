<h1 align="center">SynapseNet Architecture</h1>

<p align="center"><strong>Complete Source Map — 120+ C++ Files, 130+ Go Files, 267 Tests, 610 Build Targets</strong></p>

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" /></a>
  <a href="https://github.com/anakrypt/SynapseNet"><img src="https://img.shields.io/badge/Documentation-000000?style=for-the-badge&logo=gitbook&logoColor=white" alt="Docs" /></a>
  <a href="https://github.com/anakrypt/SynapseNet/blob/main/SynapseNet_Whitepaper.pdf"><img src="https://img.shields.io/badge/Whitepaper-000000?style=for-the-badge&logo=adobeacrobatreader&logoColor=white" alt="Whitepaper" /></a>
  <a href="README.md"><img src="https://img.shields.io/badge/0.1.0--alphaV3-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="V3" /></a>
</p>

---

## High-Level Architecture

```
synapsed (C++ daemon)

  TUI        RPC        CLI       Python
  ncurses    server     client    bridge
      \         |         |        /
       ---------+---------+-------
                |
          Node Runtime
            node_runtime, node_init, node_status_runtime
            poe_runtime, tor_control_runtime, tor_status
            maintenance_runtime
                |
    Core      Network    Privacy    Model     Web

    Ledger    P2P TCP    Tor        GGUF      Search
    PoE v1    Discovery  Onion      Inference  Clearnet
    Knowledge Handshake  SOCKS5     Download   Darknet
    Transfer  Sync       Dandelion  Market     Onion
    Wallet    Protocol   Amnesia    Training   Extract
    Consensus Scale      Stealth    Access     Audit
    NAAN      hybridMode Decoy
                |
          Crypto / Quantum
            Ed25519, secp256k1, AES-256-GCM
            CRYSTALS-Dilithium, CRYSTALS-Kyber, SPHINCS+
            Hybrid KEM, Hybrid Sig, QKD BB84
                |
          Database (SQLite)
            chaindata, ledger, knowledge, transfer
            consensus, poe, wallet


synapseide (Go terminal IDE)

  Agent, Coordinator, Prompt, Tools (bash, edit, grep, fetch...)
  MCP integration, LSP support, Diff view, File tracker
  Sessions, Config, OAuth, TUI (bubbletea)
  SynapseNet provider (JSON-RPC to synapsed)
```

---

## Core

The heart of SynapseNet. Manages the knowledge chain, NGT token ledger, Proof of Emergence consensus, and the autonomous agent network.

| File | Purpose |
|------|---------|
| `consensus.cpp` | Consensus mechanism — block validation, chain selection, fork resolution |
| `knowledge.cpp` | Knowledge base — storing, indexing, and querying knowledge entries |
| `ledger.cpp` | Block ledger — appending blocks, chain integrity, block mining |
| `transfer.cpp` | NGT transfers between wallet addresses |
| `wallet.cpp` | Wallet management — creation, loading, 24-word BIP39 seed phrase |
| `poe_v1.cpp` | Proof of Emergence v1 — the core consensus algorithm |
| `poe_v1_engine.cpp` | PoE engine — deterministic scoring, novelty computation, reward calculation |
| `poe_v1_objects.cpp` | PoE data objects — entries, votes, epochs, citations |
| `state_manager.cpp` | Node state management — startup, shutdown, recovery |
| `event_system.cpp` | Internal event bus for inter-module communication |

---

## NAAN Agent (Node-Attached Autonomous Agent)

Every node runs one autonomous agent. The agent belongs to the network, not the user. It researches, drafts knowledge, validates, and earns NGT.

| File | Purpose |
|------|---------|
| `agent_runtime.cpp` | Agent runtime — 60-second tick intervals, lifecycle management |
| `agent_scheduler.cpp` | Task scheduler — topic selection, research planning, budget allocation |
| `agent_score.cpp` | Scoring system — band tracking (ACTIVE / COOLDOWN / QUARANTINE) |
| `agent_coordination.cpp` | Multi-agent coordination across the network |
| `agent_draft_queue.cpp` | Draft queue — knowledge entries waiting for PoE submission |
| `agent_submission_pipeline.cpp` | Submission pipeline — PoW gate, validator routing, epoch tracking |
| `agent_storage.cpp` | Persistent storage with append-only audit logs |

```
NAAN Agent Cycle

  1. Pick topic from knowledge gap index
  2. Research (Tor / clearnet / local)
  3. Cross-reference knowledge chain
  4. Draft entry with citations
  5. Compute PoW nonce (anti-spam)
  6. Submit to validator pool
  7. M-of-N approval -> finalize -> NGT

  Repeat every 60 seconds
```

---

## Network (P2P)

Handles all peer-to-peer communication. Supports direct TCP, Tor SOCKS5, and hybrid mode where both transports coexist.

| File | Purpose |
|------|---------|
| `network.cpp` | Core networking — TCP sockets, SOCKS5 proxy, hybrid `.onion` + clearnet routing |
| `discovery.cpp` | Peer discovery — DNS seeds, bootstrap nodes, peer exchange (getpeers/peers) |
| `handshake.cpp` | Cryptographic handshake on connection — key exchange, capability negotiation |
| `peer_manager.cpp` | Peer lifecycle — monitoring, scoring, eviction |
| `protocol.cpp` | Wire protocol — VERSION, VERACK, PING, PONG, GETADDR, ADDR, INV, BLOCK, TX, KNOWLEDGE, VOTE |
| `sync.cpp` | Block synchronization between nodes |
| `scale_policy.cpp` | DoS protection — token bucket rate limiting, adaptive admission, overload mode |

```
Network Layer

  Outbound:
    .onion address -> SOCKS5 -> Tor circuit
    IP address     -> direct TCP
    hybridMode     -> auto-detect per address

  Inbound:
    TCP accept on port (clearnet peers)
    Hidden service (Tor peers via .onion)

  Discovery:
    DNS seeds -> bootstrap -> peer exchange
    Every 30s: getpeers <-> peers
    Addresses include both IP and .onion
```

---

## Crypto

Classical cryptographic primitives used across the system.

| File | Purpose |
|------|---------|
| `crypto.cpp` | SHA-256, double-SHA-256, HMAC-SHA-256, RIPEMD-160 |
| `keys.cpp` | Ed25519 key generation, signing, verification, save/load |
| `address.cpp` | Address generation and validation (`ngt1...` format) |

---

## Quantum (Post-Quantum Cryptography)

Full post-quantum cryptographic stack. All PQC operations use liboqs when available.

| File | Purpose |
|------|---------|
| `dilithium.cpp` | CRYSTALS-Dilithium digital signatures (NIST standard) |
| `sphincs.cpp` | SPHINCS+ hash-based signatures (stateless, conservative) |
| `kyber.cpp` | CRYSTALS-Kyber key encapsulation (NIST standard) |
| `hybrid_kem.cpp` | Hybrid KEM — classical X25519 + Kyber for key exchange |
| `hybrid_sig.cpp` | Hybrid signatures — Ed25519 + Dilithium for dual security |
| `key_derivation.cpp` | Key derivation functions for quantum-safe hierarchies |
| `knowledge_crypto.cpp` | Cryptographic operations for knowledge entries |
| `network_security.cpp` | Network-level encryption and authentication |
| `wallet_security.cpp` | Wallet encryption and key protection |
| `model_inference_crypto.cpp` | Encrypted AI inference sessions |
| `pqc_manager.cpp` | PQC manager — security level selection (standard/high/paranoid/quantum-ready) |
| `pqc_backend_status.cpp` | Backend status reporting (liboqs availability) |
| `crypto_selector.cpp` | Algorithm selection based on security level |
| `qkd_bb84.cpp` | Quantum Key Distribution — BB84 protocol implementation |
| `qkd_integration.cpp` | QKD integration with the key management system |
| `qkd_relay.cpp` | QKD relay node for extending quantum key reach |
| `otp.cpp` | One-time pad operations |
| `otp_manager.cpp` | OTP lifecycle management |
| `deterministic_otp.cpp` | Deterministic OTP for reproducible test vectors |
| `hwrng.cpp` | Hardware random number generator interface |
| `timing_defense.cpp` | Constant-time comparison and timing attack mitigations |

---

## Privacy

Privacy subsystem providing Tor integration, traffic analysis resistance, and zero-trace operation modes.

| File | Purpose |
|------|---------|
| `privacy.cpp` | Privacy manager — mode selection (OFF / STANDARD / FULL) |
| `privacy_manager.cpp` | Privacy lifecycle and state coordination |
| `onion_service.cpp` | Tor hidden service — `ADD_ONION` via control port, Ed25519-V3 keys |
| `socks5_proxy.cpp` | SOCKS5 client for routing through Tor |
| `dandelion.cpp` | Dandelion++ protocol — transaction anonymization (stem → fluff phases) |
| `decoy_traffic.cpp` | Dummy traffic generation to resist traffic analysis |
| `stealth_address.cpp` | One-time stealth addresses for unlinkable payments |
| `session_crypto.cpp` | Per-session encryption keys |
| `hidden_volume.cpp` | Hidden encrypted volumes for plausible deniability |
| `amnesia.cpp` | Amnesia mode — RAM-only operation, zero disk traces |
| `mix_inference.cpp` | Mix network for AI inference requests to prevent correlation |
| `traffic_analysis.cpp` | Traffic analysis detection and countermeasures |

---

## Tor

Dedicated Tor management — process lifecycle, bridge handling, routing policy, and status monitoring.

| File | Purpose |
|------|---------|
| `tor_route_policy.cpp` | Routing policy engine — TOR_ONLY, HYBRID, CLEARNET, DEGRADED modes |
| `tor_process_guard.cpp` | Managed Tor process — spawn, monitor, restart with backoff |
| `tor_bridge_provider.cpp` | obfs4 bridge provider — file, cache, remote fetch with rate limiting |
| `tor_bridge_utils.cpp` | Bridge line parsing, validation, deduplication |
| `tor_peer_identity.cpp` | Peer identity management through Tor (session display IDs) |
| `tor_control_runtime.cpp` | Tor control port operations — mode switching, bridge refresh, restart |
| `tor_status_runtime.cpp` | Tor status monitoring — bootstrap state, readiness, degradation |
| `tor_engine.cpp` | Tor engine for web module — circuit management for search requests |
| `tor_fetch.cpp` | HTTP fetch routed through Tor SOCKS5 proxy |

---

## Web / Web4 Search

The search and information retrieval system. Supports clearnet, Tor, and `.onion` sources. NAAN agent uses this for autonomous research.

| File | Purpose |
|------|---------|
| `web_search.cpp` | Main search engine — multi-source query execution and result merging |
| `curl_fetch.cpp` | HTTP fetch via libcurl with timeout, redirect, and proxy support |
| `html_extractor.cpp` | HTML to text extraction — ad removal, script stripping, content isolation |
| `darknet_engines.cpp` | Darknet search engines — Ahmia, Torch, DarkSearch, DeepSearch |
| `onion_search_router.cpp` | `.onion` URL routing and circuit selection |
| `deep_search_wrapper.cpp` | Deep search wrapper for multi-hop queries |
| `forum_crawler.cpp` | Forum content extraction |
| `query_detector.cpp` | Query intent detection — determines search strategy |
| `search_config.cpp` | Search configuration — engine selection, allowlists, denylists, Tor policy |
| `ai_wrapper.cpp` | AI post-processing of search results — summarization, relevance scoring |
| `connector_audit.cpp` | Connector audit trail — logs all external fetches for transparency |
| `rpc_server.cpp` | RPC server for the web search module |

---

## Model (AI)

Local AI model management — loading GGUF models, running inference, downloading, and the model marketplace.

| File | Purpose |
|------|---------|
| `model_loader.cpp` | GGUF model loading with memory mapping |
| `model_inference.cpp` | Streaming chat inference with chunked token decoding |
| `model_download.cpp` | Model download with resume support, progress tracking, sleep inhibition |
| `model_marketplace.cpp` | Model marketplace — rent/offer GPU time for NGT |
| `model_access.cpp` | Model access control and permission management |
| `model_training.cpp` | Model training support |

---

## Implant / Updates

Runtime update system and future implant-AI stack compatibility layer.

| File | Purpose |
|------|---------|
| `implant_compatibility.cpp` | Implant-AI stack compatibility (future neurochip integration) |
| `implant_safety_pipeline.cpp` | Safety pipeline — validation before applying any update |
| `update_bundle.cpp` | Update bundle format — packaging, signing, verification |
| `update_installer.cpp` | Update installer — apply, rollback, integrity checks |

---

## TUI (Terminal User Interface)

Full ncurses terminal interface with multiple screens, real-time dashboards, and keyboard navigation.

| File | Purpose |
|------|---------|
| `tui.cpp` | Main TUI engine — ncurses initialization, input loop, screen management |
| `tui_dashboard.cpp` | Node dashboard — peers, sync status, quantum security, system stats |
| `tui_network.cpp` | Network screen — connected peers, discovery stats, bandwidth |
| `tui_wallet.cpp` | Wallet screen — balance, address, transaction history |
| `tui_mining.cpp` | Mining screen — PoE status, epoch rewards, submission history |
| `tui_model.cpp` | Model screen — loaded model, inference stats, download manager |
| `tui_knowledge.cpp` | Knowledge screen — local chain stats, recent entries, search |
| `tui_settings.cpp` | Settings screen — configuration, security level, Tor mode |
| `tui_startup.cpp` | Boot sequence — animated startup, subsystem initialization display |
| `tui_help.cpp` | Help screen — key bindings, commands reference |
| `tui_runtime.cpp` | TUI runtime loop — event processing, screen refresh |
| `screens.cpp` | Screen registry and navigation |
| `primary_ui_spec.cpp` | UI specification — layout rules, color scheme, spacing |
| `tui_command_bridge.cpp` | Bridge between TUI input and RPC commands |
| `tui_snapshot_builder.cpp` | Snapshot builder for TUI state serialization |

---

## RPC

JSON-RPC interface for programmatic access to all node functions.

| File | Purpose |
|------|---------|
| `rpc_commands.cpp` | RPC command router and handler registry |
| `rpc_ai_runtime.cpp` | AI runtime RPC — model loading, inference, chat sessions |
| `rpc_naan_runtime.cpp` | NAAN agent RPC — start, stop, status, observatory |
| `rpc_naan_views.cpp` | NAAN view serialization — agent state, score, events |
| `rpc_node_views.cpp` | Node status RPC — peers, sync, Tor status, network stats |
| `rpc_poe_views.cpp` | PoE data RPC — entries, votes, epochs, rewards |
| `rpc_model_views.cpp` | Model data RPC — loaded models, marketplace offers |
| `rpc_update_runtime.cpp` | Update system RPC — check, download, apply updates |
| `rpc_update_views.cpp` | Update view serialization — bundle state, install history |

---

## SynapseIDE (Go Terminal IDE)

Full terminal-based coding environment that talks to synapsed over JSON-RPC.

| Component | Purpose |
|-----------|---------|
| `agent/` | AI agent — coordinator, prompts, tool execution |
| `agent/tools/` | Tool suite — bash, edit, grep, glob, fetch, web search, write, download |
| `agent/synapsenet/` | SynapseNet provider — connects to synapsed RPC |
| `tui/` | Terminal UI — bubbletea-based, chat view, sidebar, editor, diff viewer |
| `config/` | Configuration — providers, API keys, model selection |
| `lsp/` | Language Server Protocol client |
| `session/` | Session management and history |
| `patch/` | Unified diff patch mode (`/patch` command) |
| `skills/` | Skills system for specialized coding tasks |
| `shell/` | Embedded shell with coreutils |
| `db/` | SQLite session/message storage |

Features:
- Isolated chat threads (`/tangent`)
- Unified diff patch mode (`/patch`)
- Code contributions submitted as PoE entries → earn NGT
- Remote model sessions via marketplace
- MCP (Model Context Protocol) integration
- LSP support for code intelligence

---

## CLI

Command-line interface for headless operation and scripting.

| File | Purpose |
|------|---------|
| `cli_parser.cpp` | Argument parser — all `--flags` and subcommands |
| `cli_rpc_client.cpp` | RPC client — `status`, `peers`, `balance`, `address`, `submit`, `send`, `tor`, `naan`, `discovery`, `seeds` |

---

## Database

Persistent storage layer using SQLite.

| File | Purpose |
|------|---------|
| `database.cpp` | SQLite wrapper — open, close, query, transaction management |
| `wallet_db.cpp` | Wallet-specific database operations |

Database files per node:
```
chaindata/chain.db      — block chain
ledger/ledger.db         — NGT ledger
knowledge/knowledge.db   — knowledge entries
transfer/transfer.db     — NGT transfers
consensus/consensus.db   — consensus state
poe/poe.db               — PoE epochs and votes
```

---

## Python Bridge

Python interoperability layer for extensibility.

| File | Purpose |
|------|---------|
| `python_bridge.cpp` | Python-C++ bridge for extending node functionality |
| `sandbox.cpp` | Sandboxed Python execution environment |

---

## Utils

Shared utilities used across all modules.

| File | Purpose |
|------|---------|
| `config.cpp` | Runtime configuration — key-value store with typed accessors |
| `logger.cpp` | Logging — INFO, WARN, ERROR with timestamps |
| `format.cpp` | String formatting utilities |
| `serialize.cpp` | Binary serialization for network messages |
| `memory.cpp` | Memory management and tracking |
| `threading.cpp` | Thread pool and concurrency primitives |
| `single_instance.cpp` | Lock file to prevent duplicate node instances |
| `sleep_inhibitor.cpp` | Prevents system sleep during model downloads |

---

## Build Stats

| Metric | Count |
|--------|-------|
| C++ source files | 120+ |
| C++ header files | 75+ |
| Go source files | 130+ |
| Test files | 67 |
| Tests passing | 267/267 |
| Build targets | 610 |
| Third-party: llama.cpp | Local LLM inference engine |
| Third-party: secp256k1 | Bitcoin elliptic curve library |
| Third-party: liboqs | Post-quantum crypto (OQS) |

---

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Built_by_Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Kepler" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
</p>

<p align="center">
  If you find this project worth watching — even if you can't contribute code — you can help keep it going.<br>
  Donations go directly toward VPS hosting for seed nodes, build infrastructure, and development time.
</p>

<p align="center">
  <a href="https://www.blockchain.com/btc/address/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4"><img src="https://img.shields.io/badge/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="BTC" /></a>
</p>
