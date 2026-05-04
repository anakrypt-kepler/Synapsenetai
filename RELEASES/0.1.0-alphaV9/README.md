# SynapseNet 0.1.0-alphaV9

**Distributed Exploit Intelligence + Shared CVE Chain Across NAAN Network**

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV9-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/Shared_CVEs-14+-000000?style=for-the-badge&labelColor=000000" alt="CVEs" />
  <img src="https://img.shields.io/badge/Exploit_Chain-Distributed-000000?style=for-the-badge&labelColor=000000" alt="Chain" />
  <img src="https://img.shields.io/badge/P2P_Intel-Active-000000?style=for-the-badge&labelColor=000000" alt="P2P" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
  <a href="../0.1.0-alphaV8"><img src="https://img.shields.io/badge/V8-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V8" /></a>
</p>

---

> V9 makes every NAAN node a distributed vulnerability research platform. When one node
> discovers a working CVE bypass during mining, it publishes the exploit intelligence to
> the shared exploit chain. Every other node on the network receives it immediately and
> can use that bypass without rediscovering it. The more nodes mine, the faster the
> network learns to bypass new protections. This is the arms race asymmetry: one vendor
> on one side, thousands of autonomous nodes sharing intelligence on the other.

---

## Core Concept

```
Node A discovers CVE-0011 works on Dread
  |
  +-> publishExploit(CVE-0011, queue_refresh_bypass, 85% confidence)
  +-> persists to exploit_chain.json
  +-> broadcasts naan.exploit_intel event to peers
  |
Node B receives the intel
  |
  +-> ingestExploit(CVE-0011) -- validates confidence >= 30%
  +-> merges into local exploit chain (bumps success counter)
  +-> next mining tick: bestExploitFor("endgame_v2_queue") returns CVE-0011
  +-> uses queue_refresh_bypass immediately without rediscovery
  |
Node C joins the network
  |
  +-> loadExploitChain() from disk (if returning node)
  +-> exploit.sync RPC pulls latest chain
  +-> starts mining with full exploit knowledge from day one
```

---

## ExploitIntel Structure

Every exploit entry in the shared chain carries:

| Field | Type | Description |
|-------|------|-------------|
| cveId | string | Internal CVE identifier (NAAN-CVE-2026-XXXX) |
| protectionType | string | Protection class (endgame_v2_queue, cloudflare_bot_mgmt, captcha_token_static, etc.) |
| bypassMethod | string | Specific method used (queue_refresh_bypass, cf_bm_cookie_replay, token_replay, etc.) |
| transport | string | Network transport (tor, clearnet) |
| confidence | int | Success confidence 0-100%, updated by network consensus |
| discoveredBy | string | SHA-256 hash of the discovering node's ID (anonymous) |
| timestamp | int64 | Discovery timestamp in milliseconds |
| successCount | int | Total successful uses across all nodes |
| failCount | int | Total failed attempts across all nodes |
| signature | string | SHA-256 of cveId + method + timestamp (integrity check) |

---

## How It Works

### Publishing (after successful bypass)

Every time `naanLoop` completes a mining tick where `fetchWithRetry` triggered a CVE bypass (recorded in `lastBypass_`), the agent constructs an `ExploitIntel` entry and calls `publishExploit()`:

1. Checks if the CVE already exists in the chain
2. If yes: bumps `successCount`, updates confidence if higher
3. If no: adds new entry to `exploitChain_` vector + index
4. Persists to `~/.synapsenet/exploit_chain.json`
5. Emits `naan.exploit_intel` event for RPC subscribers and peer relay

### Ingesting (from network peers)

When a node receives an `ExploitIntel` from a peer via the `naan.exploit_intel` event or the `EXPLOIT_INTEL` P2P message type:

1. Validates `confidence >= 30%` (rejects low-quality intel)
2. Merges with existing entry if CVE already known (bumps counters, keeps highest confidence method)
3. Adds as new entry if CVE is novel
4. Persists to disk

### Pre-fetch Lookup

Before `fetchWithRetry` runs the full bypass chain, the NAAN agent can call `bestExploitFor(protectionType)` to check if the network already knows a working bypass for the detected protection type. This allows skipping the discovery phase entirely and jumping straight to the known-good method.

### Persistence

The exploit chain is stored as `~/.synapsenet/exploit_chain.json`:

```json
[
  {"cveId":"NAAN-CVE-2026-0011","protectionType":"endgame_v2_queue","bypassMethod":"queue_refresh_bypass","transport":"tor","confidence":85,"discoveredBy":"a1b2c3...","timestamp":1777857900000,"successCount":47,"failCount":3,"signature":"d4e5f6..."},
  {"cveId":"NAAN-CVE-2026-0014","protectionType":"captcha_token_static","bypassMethod":"token_replay","transport":"tor","confidence":75,"discoveredBy":"x7y8z9...","timestamp":1777857950000,"successCount":12,"failCount":2,"signature":"g7h8i9..."}
]
```

Loaded at NAAN startup (`loadExploitChain`), persisted after every publish/ingest (`persistExploitChain`).

---

## RPC API

| Method | Params | Returns |
|--------|--------|---------|
| `exploit.list` | `{"offset": 0, "limit": 100}` | JSON array of ExploitIntel entries with `successRate` field |
| `exploit.stats` | `{}` | `{"total": N, "critical": N, "high": N, "total_success": N, "total_fail": N, "success_rate": N}` |
| `exploit.sync` | `{}` | `{"ok": true, "count": N}` — reloads chain from disk + peers |

---

## EXPLOITS Tab — Tauri Desktop App

New **EXPLOITS** tab in the navigation bar (after HARVEST):

- **Stats cards**: total network CVEs, overall success rate, critical count, high count
- **Exploit chain table**: scrollable, columns: CVE | PROTECTION | METHOD | CONF | OK/FAIL | RATE | TRANSPORT | TIME
- **Confidence color coding**: red (>=90%), orange (>=70%), green (<70%)
- **Detail view on click**: full ExploitIntel fields including anonymous discoverer hash
- **SYNC FROM PEERS button**: triggers `exploit.sync` RPC
- **REFRESH button**: reloads the list

---

## EXPLOITS Screen — ncurses TUI

New **Screen::EXPLOITS** (press `E` from Dashboard, NAAN agent, or Harvest screen):

- **Scrollable table**: CVE | PROTECTION | METHOD | CONF | OK/FL | RATE | TRANS
- **Detail panel**: all fields on selected entry
- **Key bindings**: Arrow keys for navigation, `B` to go back, `H` for Harvest, `A` for NAAN agent
- **Data feed**: `exploit_chain.json` scanner in `tui_runtime.cpp` runs every 5 seconds

---

## P2P Wire Protocol

`EXPLOIT_INTEL` has been added to the `MessageType` enum in `include/network/network.h`. The gossip pattern follows the existing knowledge/PoE relay model:

1. Node discovers exploit -> `broadcastInv(InvType::EXPLOIT_INTEL, hash)`
2. Peers check if hash is known -> if not, send `GetData`
3. Discoverer replies with full `ExploitIntel` payload
4. Receiving peer calls `ingestExploit` -> re-announces to its own peers

This creates epidemic spread: a bypass discovered by one node reaches the entire network in O(log N) hops.

---

## Network Effect

| Nodes | Discovery speed | Coverage |
|-------|----------------|----------|
| 1 | Baseline: agent finds CVEs alone during mining | Single node's targets only |
| 10 | 10x: each node contributes unique CVEs from different targets | 10 nodes' combined target lists |
| 100 | Near-instant: most protections already have a known bypass in the chain | Practically all common protections covered |
| 1000+ | Real-time arms race: new protection deployed -> first node to bypass shares -> entire network updated in seconds | Full coverage with continuous adaptation |

The asymmetry is structural: protection vendors must defend every endpoint, but the network only needs one node to find one bypass, and every other node benefits immediately.

---

## Files Changed

| File | Change | Description |
|------|--------|-------------|
| `include/network/network.h` | Modified | Added `EXPLOIT_INTEL` to `MessageType` enum |
| `src/ide/synapsed_engine.h` | Modified (+25) | `ExploitIntel` struct, exploit chain storage, 8 method declarations |
| `src/ide/synapsed_engine.cpp` | Modified (+236) | `publishExploit`, `ingestExploit`, `bestExploitFor`, `loadExploitChain`, `persistExploitChain`, `exploitChainList`, `exploitChainStats`, `syncExploitChainFromPeers`, naanLoop wiring, RPC methods |
| `tauri-app/src/lib/store.ts` | Modified | Added `"exploits"` to `TabId` and `tabs` |
| `tauri-app/src/lib/rpc.ts` | Modified (+12) | `exploitList`, `exploitStats`, `exploitSync` |
| `tauri-app/src/app/App.svelte` | Modified | Added Exploits route |
| `tauri-app/src/app/routes/Exploits.svelte` | **New** | Full exploit chain browser with stats, table, detail, sync |
| `include/tui/tui.h` | Modified | `Screen::EXPLOITS`, `ExploitIntelSummary` struct, `updateExploitChain` |
| `src/tui/tui.cpp` | Modified (+165) | `drawExploits()`, key bindings, input handler, state |
| `src/tui/tui_runtime.cpp` | Modified (+54) | Exploit chain JSON scanner |
| `RELEASES/0.1.0-alphaV9/README.md` | **New** | This file |

---

## What's Next

- V10: Full wire-level exploit relay via INV/GETDATA gossip (currently event-based, wire message type reserved)
- V11: Exploit reputation system — nodes vote on bypass quality, low-success exploits auto-demoted
- V12: Continuous CAPTCHA model retraining with harvested image corpus from the network
- V13: Headless browser integration (Playwright via Tor) for full JS challenge solving

---

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Built_by_Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Kepler" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
</p>

<p align="center">
  <a href="https://www.blockchain.com/btc/address/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4"><img src="https://img.shields.io/badge/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="BTC" /></a>
</p>
