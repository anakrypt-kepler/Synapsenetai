<h1 align="center">SynapseNet 0.1.0-alphaV4</h1>

<p align="center"><strong>Kill Go — Rewrite Everything in C++ and Ship a Desktop App</strong></p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV4-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/Status-In_Development-000000?style=for-the-badge&labelColor=000000" alt="Status" />
  <img src="https://img.shields.io/badge/Migration-Go_to_C++-000000?style=for-the-badge&labelColor=000000" alt="Migration" />
  <img src="https://img.shields.io/badge/Frontend-Tauri_%2B_Svelte-000000?style=for-the-badge&labelColor=000000" alt="Frontend" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" /></a>
  <a href="https://github.com/anakrypt/SynapseNet"><img src="https://img.shields.io/badge/Documentation-000000?style=for-the-badge&logo=gitbook&logoColor=white" alt="Docs" /></a>
  <a href="https://github.com/anakrypt/SynapseNet/blob/main/SynapseNet_Whitepaper.pdf"><img src="https://img.shields.io/badge/Whitepaper-000000?style=for-the-badge&logo=adobeacrobatreader&logoColor=white" alt="Whitepaper" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV3.6"><img src="https://img.shields.io/badge/←_0.1.0--alphaV3.6-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V3.6" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES"><img src="https://img.shields.io/badge/All_Releases-000000?style=for-the-badge&logo=github&logoColor=white" alt="All Releases" /></a>
</p>

---

> V4 eliminates Go from the project entirely. Every component that lived in the Go-based SynapseIDE terminal client gets rewritten in C++ and integrated directly into synapsed. The terminal-only TUI is replaced by a Tauri desktop application — TypeScript/Svelte frontend, Rust backend shell, C++ node daemon underneath. Same architecture, zero Go dependency.

---

## Why Kill Go

The Go codebase (SynapseIDE / `crush-main/`) was a standalone terminal IDE that talked to synapsed over JSON-RPC. It worked, but it created problems:

- **Two languages, two build systems** — CMake for C++ and `go build` for Go, doubling CI complexity
- **Duplicated logic** — the Go agent reimplemented things that already existed in the C++ daemon
- **Terminal-only** — bubbletea TUI cannot compete with Cursor, Zed, or Windsurf as a real editor
- **No GUI** — impossible to ship a desktop app with a Go terminal client

The fix: move everything into C++ where it belongs, then wrap it in a Tauri desktop app.

---

## What Gets Migrated

Every Go component maps to a C++ equivalent. Nothing gets lost.

### Agent System

| Go (current) | C++ (target) | What it does |
|--------------|-------------|--------------|
| `agent/coordinator.go` | `src/ide/agent_coordinator.cpp` | Agent execution loop — prompt routing, tool dispatch, response streaming |
| `agent/prompt.go` | `src/ide/agent_prompt.cpp` | System prompts, context assembly, token budgeting |
| `agent/tools/*.go` | `src/ide/tools/*.cpp` | Tool suite — bash, edit, grep, glob, fetch, write, download, web search |
| `agent/synapsenet/provider.go` | Direct call (no RPC needed) | SynapseNet provider — was JSON-RPC, now direct C++ function calls |

### Editor and UI

| Go (current) | C++ / Tauri (target) | What it does |
|--------------|---------------------|--------------|
| `tui/*.go` (bubbletea) | `src-tauri/` + `src/` (Svelte) | Terminal TUI replaced by Tauri desktop app with Monaco Editor |
| `tui/chat_view.go` | Svelte chat panel | AI chat interface with streaming responses |
| `tui/sidebar.go` | Svelte sidebar | File tree, navigation, project structure |
| `tui/editor.go` | Monaco Editor (embedded) | Code editor with syntax highlighting, multi-tab |
| `tui/diff_view.go` | Monaco diff viewer | Side-by-side diff for patches |

### Infrastructure

| Go (current) | C++ (target) | What it does |
|--------------|-------------|--------------|
| `config/*.go` | `src/ide/config.cpp` | Configuration — providers, API keys, model selection |
| `lsp/*.go` | `src/ide/lsp_client.cpp` | Language Server Protocol client for code intelligence |
| `session/*.go` | `src/ide/session.cpp` | Session management — chat history, thread state |
| `patch/*.go` | `src/ide/patch.cpp` | Unified diff patch mode |
| `skills/*.go` | `src/ide/skills.cpp` | Skills system for specialized coding tasks |
| `shell/*.go` | `src/ide/shell.cpp` | Embedded shell (already exists in C++ partially) |
| `db/*.go` | `src/ide/session_db.cpp` | SQLite session/message storage (reuse existing db layer) |

### MCP and Protocols

| Go (current) | C++ (target) | What it does |
|--------------|-------------|--------------|
| `mcp/*.go` | `src/ide/mcp_server.cpp` | Model Context Protocol server — tool registration, resource serving |
| `oauth/*.go` | `src/ide/oauth.cpp` | OAuth flow for external service auth |

### VS Code Extension (`ide/synapsenet-vscode/`)

The VS Code extension is a TypeScript/JS client that talks to synapsed over JSON-RPC. It stays TypeScript — but all the RPC endpoints it calls must be verified and stabilized in the C++ daemon before the Go removal.

| Extension feature | RPC endpoint | C++ status |
|-------------------|-------------|------------|
| AI completions (insert + inline ghost text) | `ai.complete` | Already in synapsed |
| AI stop generation | `ai.stop` | Already in synapsed |
| Chat panel (Web4 optional) | `ai.complete` + web injection | Already in synapsed |
| Model load / unload / status / list | `model.*` | Already in synapsed |
| Remote model rent / list / end | `model.remote.*` | Already in synapsed |
| Suggest patch (preview / apply) | `ai.complete` (diff mode) | Already in synapsed |
| Submit code patch (PoE) | `poe.submit_code` | Already in synapsed |
| Fetch code submission | `poe.fetch_code` | Already in synapsed |
| GitHub Quests (configure, select, branch, fork, commit, PR) | GitHub API (client-side) | Stays in extension |
| Inline completion provider | `ai.complete` (short) | Already in synapsed |
| Node status | `node.status` | Already in synapsed |

The extension itself does not need rewriting — it is pure TypeScript calling JSON-RPC. But the Go agent code it was designed to work alongside gets deleted. After V4, the extension talks directly to synapsed C++ with no Go middleman.

What changes for the extension:
- **No Go dependency** — extension connects to synapsed only, Go process is gone
- **Same RPC interface** — all `model.*`, `ai.*`, `poe.*`, `node.*` endpoints stay identical
- **Quest workflow untouched** — GitHub Quests logic lives entirely in the extension (TypeScript + GitHub API)
- **Tauri app replaces terminal use** — the extension becomes one of two frontends (VS Code extension + Tauri desktop app)

---

## New Desktop App Architecture

Go terminal TUI is replaced by a Tauri desktop application. This is what Cursor, Zed, and Windsurf do — editor + AI in one window. Except here, the AI is decentralized and mines NGT.

```
SynapseNet App (Tauri)
  |-- Monaco Editor        <- built-in IDE
  |-- Chat panel           <- AI completions from your node
  |-- Node status          <- peers, Tor, NGT balance
  |-- Settings             <- node config

Tech stack:
  Frontend:  TypeScript + Svelte
  Backend:   Rust (Tauri shell)
  Engine:    C++ (synapsed, linked as library)
```

### Why Tauri

- **Rust backend** — Tauri's Rust core calls directly into synapsed C++ via FFI, no JSON-RPC overhead
- **Svelte frontend** — lightweight, fast, ships as a native desktop window
- **Cross-platform** — macOS, Linux, Windows from one codebase
- **Small binary** — Tauri apps are 5-10 MB, not 200+ MB like Electron
- **No Go runtime** — eliminates the entire Go dependency tree

---

## Desktop App Design

Black interface. Dark gray text. Pixel font. Minimal icons. No color unless it means something.

### First Launch — Setup Wizard

Five steps. Each step is one screen. No skipping ahead. No account creation, no email, no cloud.

**Step 1 — Wallet**

Two options: Create New or Restore Existing.

- Create New — app generates Ed25519 + post-quantum keys locally, shows the NGT address, displays the 24-word BIP39 seed phrase once, asks user to confirm they saved it. Optional password lock on the wallet file.
- Restore Existing — paste 24-word seed phrase, app regenerates keys deterministically.

Nothing leaves the machine. No registration. No server. Wallet is a local file under `~/.synapsenet/wallet.dat`.

**Step 2 — Connection Type**

Three buttons:

- **Clearnet** (selected by default) — direct TCP, fastest, no privacy
- **Tor** — app starts a managed Tor process internally, user installs nothing, all traffic routes through SOCKS5
- **Tor + Bridges** — same as Tor but shows a text field to paste obfs4 bridge lines for censored networks

Clearnet is the default because it works everywhere with zero config. Tor adds latency but full privacy. Bridges are for users behind firewalls that block Tor.

**Step 3 — AI Model**

Three options:

- **Download recommended** — Llama 3B (~2 GB), downloads in background with progress bar and resume support. Recommended for most users.
- **Select local file** — file picker for an existing `.gguf` model on disk
- **Skip** — node runs without AI. P2P networking, wallet, and blockchain sync work. AI completions and NGT mining through PoE knowledge submissions are unavailable.

**Step 4 — Resources**

App detects hardware automatically and sets safe defaults:

- CPU threads (slider, default = half of available cores)
- RAM limit (slider, default = 4 GB or 25% of available, whichever is lower)
- Disk space limit for chain data + models
- Checkbox: Launch at system startup
- Checkbox: Mine NGT in background (NAAN agent runs on idle cycles)

**Step 5 — Ready**

Status screen with checklist:

```
Wallet          OK
Connection      Tor connected (3 circuits)
AI Model        Llama-3B loaded (2.1 GB)
Peers           12 found

[ Open SynapseNet ]
```

The "Open SynapseNet" button is disabled until all critical checks pass. If Tor fails to bootstrap, the app shows the error and what to try — it does not silently fall back to clearnet. If the model failed to download, it says so. No hidden state.

### Main Window

The app opens to a dashboard, not an IDE. The IDE is one tab among many. Top navigation bar with tabs. Content area below. Status bar at the bottom.

```
[ Dashboard ] [ Wallet ] [ Transfers ] [ Knowledge ] [ NAAN Agent ] [ IDE ] [ Network ] [ Settings ]
+------------------------------------------------------------------------+
|                                                                        |
|                         Active tab content                             |
|                                                                        |
+------------------------------------------------------------------------+
|  Tor  |  12 peers  |  1,847.00 NGT  |  NAAN: mining  |  v0.1.0-V4    |
+------------------------------------------------------------------------+
```

**Top bar** — horizontal tabs, always visible:
- Dashboard
- Wallet
- Transfers
- Knowledge
- NAAN Agent
- IDE
- Network
- Settings

**Center** — content area, changes based on selected tab. Default tab on launch is Dashboard.

**Bottom status bar** — always visible, never hidden:
- Connection indicator: green dot = Tor, yellow dot = Clearnet, red dot = disconnected
- Peer count
- NGT balance
- NAAN agent status (mining / idle / cooldown / off)
- App version

### Dashboard Tab

Overview of everything at a glance:
- NGT balance (large)
- NAAN agent status and last reward
- Connected peers count
- Tor / Clearnet status
- Last synced block
- AI model status (loaded / not loaded)
- Quick actions: Send NGT, Submit Knowledge, Open IDE

### Wallet Tab

- NGT balance
- Wallet address (click to copy)
- QR code for address
- 24-word seed backup (requires password confirmation)
- Password lock / unlock wallet
- Export / import wallet

### Transfers Tab

- **Send NGT** — form with recipient address + amount + optional memo
- **Receive** — show wallet address + QR code
- **Transaction history** — list of all sent, received, and PoE reward transactions with timestamps, amounts, status (confirmed / pending)
- Filter by type (sent / received / rewards)

### Knowledge Tab

Manual knowledge submission and browsing:
- **Submit Knowledge** — form to write and submit a knowledge entry manually (title, content, citations). Goes through PoW gate + validator pool, same as NAAN auto-submissions.
- **Browse local chain** — search and read knowledge entries stored on this node
- **My submissions** — list of entries this node submitted, with status (pending / approved / rejected) and NGT earned
- **PoE stats** — current epoch, total entries, reward pool

### NAAN Agent Tab

NAAN (Node-Attached Autonomous Agent) control panel:
- **Status** — current state (ACTIVE / COOLDOWN / QUARANTINE / OFF)
- **Start / Stop** button
- **Score** — agent band, total submissions, approval rate
- **Current task** — what the agent is researching right now
- **Draft queue** — knowledge entries waiting for PoE submission
- **Submission history** — past submissions with results and NGT earned
- **Config** — topic preferences, research sources (clearnet / onion / both), tick interval, budget limits
- **Observatory** — live view of what other NAAN agents on the network are doing (read-only)

### IDE Tab

Full code editor + AI assistant. This is the Cursor/Zed equivalent.

```
+-------------------------------------------+
|           Monaco Editor                   |
|           (multi-tab, syntax highlight,   |
|            pixel font, dark theme)        |
+-------------------------------------------+
|           AI Chat Panel                   |
|           (streaming responses,           |
|            tool calls, code blocks)       |
+-------------------------------------------+
```

- Monaco Editor with multi-tab file editing
- Pixel monospace font (configurable)
- Dark theme only (black background, gray text)
- Inline AI ghost text completions (same as VS Code extension's `ai.complete`)
- Patch mode — AI suggests unified diff, user previews and applies
- PoE CODE submit — submit a patch as a knowledge contribution, earn NGT
- AI chat panel below the editor:
  - Streaming token output
  - Tool calls visible (bash, edit, grep, fetch)
  - Code blocks with copy button
  - Optional Web4 injection (clearnet + onion search context)
  - Session history (saved to SQLite, survives restart)
  - Isolated threads (`/tangent` equivalent)

### Network Tab

- Connected peers list (address, transport type, latency)
- Tor status (bootstrap state, circuit count, bridge status)
- Discovery stats (DNS seeds, peer exchange activity)
- Bandwidth usage (in/out)
- Peer map (visual representation of connected nodes)

### Settings Tab

- Connection type (switch between Clearnet / Tor / Tor+Bridges without restart)
- AI model (load different `.gguf`, download new, unload)
- Resource limits (CPU, RAM, disk)
- NAAN agent config (enable/disable background mining, topic preferences)
- NAAN site allowlist (clearnet + onion domains for research)
- Startup behavior (launch at login, minimize to tray)
- App updates (check now, auto-update toggle)

### Updates

Tauri has a built-in auto-updater. The app checks for new versions on launch and shows a notification when an update is available. User clicks "Update" and the app downloads, verifies the signature, and restarts. No manual downloads, no visiting GitHub. Can also be triggered manually from Settings.

### Data Directory

All node data lives under `~/.synapsenet/`. Nothing is stored outside this directory. Portable — copy the folder to another machine and everything works.

```
~/.synapsenet/
  wallet.dat           — Ed25519 + PQC keys, encrypted if password set
  config.toml          — connection type, model path, resource limits, NAAN config
  chain/
    chain.db           — block chain
    ledger.db          — NGT ledger
    knowledge.db       — knowledge entries
    transfer.db        — NGT transfers
    consensus.db       — consensus state
    poe.db             — PoE epochs and votes
  models/              — downloaded GGUF files
  sessions/
    sessions.db        — chat history, threads, file tracking
  tor/                 — managed Tor runtime data (if Tor mode selected)
  logs/                — node logs (rotated)
```

### Headless Mode

Server nodes run `synapsed` daemon directly — no Tauri, no GUI required. The standalone binary target stays as-is. Same C++ engine, same P2P network, same RPC interface. The only difference is no desktop window. Headless nodes are first-class citizens — they mine NGT, serve peers, and respond to RPC exactly like desktop nodes.

```
# Desktop (Tauri app)
./synapsenet-app

# Server / headless (daemon only)
./synapsed -D ~/.synapsenet --daemon
```

---

## Full Go-to-C++ File Map

The Go codebase has 254 source files across 50+ packages. Here is the complete migration map organized by module, based on the actual `crush-main/internal/` directory structure.

### Agent (35 files)

| Go path | C++ target | Notes |
|---------|-----------|-------|
| `agent/agent.go` | `src/ide/agent.cpp` | Agent lifecycle, message loop |
| `agent/agent_tool.go` | `src/ide/agent_tool.cpp` | Tool invocation wrapper |
| `agent/coordinator.go` | `src/ide/agent_coordinator.cpp` | Main execution loop |
| `agent/errors.go` | `src/ide/agent_errors.cpp` | Error types |
| `agent/event.go` | `src/ide/agent_event.cpp` | Event system |
| `agent/prompts.go` | `src/ide/agent_prompts.cpp` | System prompt templates |
| `agent/prompt/prompt.go` | `src/ide/agent_prompt.cpp` | Prompt assembly, token budgeting |
| `agent/synapsenet/provider.go` | Direct C++ calls | No more RPC — direct function calls |
| `agent/tools/bash.go` | `src/ide/tools/bash.cpp` | Shell execution |
| `agent/tools/edit.go` | `src/ide/tools/edit.cpp` | File editing |
| `agent/tools/multiedit.go` | `src/ide/tools/multiedit.cpp` | Multi-file editing |
| `agent/tools/grep.go` | `src/ide/tools/grep.cpp` | Regex search |
| `agent/tools/rg.go` | `src/ide/tools/rg.cpp` | Ripgrep integration |
| `agent/tools/glob.go` | `src/ide/tools/glob.cpp` | File pattern matching |
| `agent/tools/fetch.go` | `src/ide/tools/fetch.cpp` | HTTP fetch (reuse curl_fetch) |
| `agent/tools/write.go` | `src/ide/tools/write.cpp` | File creation |
| `agent/tools/download.go` | `src/ide/tools/download.cpp` | File download |
| `agent/tools/search.go` | `src/ide/tools/search.cpp` | Web search (reuse web_search) |
| `agent/tools/ls.go` | `src/ide/tools/ls.cpp` | Directory listing |
| `agent/tools/references.go` | `src/ide/tools/references.cpp` | Code references |
| `agent/tools/diagnostics.go` | `src/ide/tools/diagnostics.cpp` | LSP diagnostics |
| `agent/tools/todos.go` | `src/ide/tools/todos.cpp` | TODO tracking |
| `agent/tools/safe.go` | `src/ide/tools/safe.cpp` | Safety checks |
| `agent/tools/job_kill.go` | `src/ide/tools/job_kill.cpp` | Background job termination |
| `agent/tools/job_output.go` | `src/ide/tools/job_output.cpp` | Background job output |
| `agent/tools/mcp/*.go` | `src/ide/mcp_tools.cpp` | MCP tool bridge |
| `agent/tools/sourcegraph.go` | `src/ide/tools/sourcegraph.cpp` | Sourcegraph integration |

### Config (15 files)

| Go path | C++ target | Notes |
|---------|-----------|-------|
| `config/config.go` | `src/ide/config.cpp` | Main config struct |
| `config/load.go` | `src/ide/config_load.cpp` | Config file parsing |
| `config/resolve.go` | `src/ide/config_resolve.cpp` | Provider/model resolution |
| `config/provider.go` | `src/ide/config_provider.cpp` | AI provider config |
| `config/init.go` | `src/ide/config_init.cpp` | First-run defaults |
| `config/copilot.go` | Removed | Copilot-specific config (not needed) |
| `config/hyper.go` | Removed | Hyper-specific config (not needed) |
| `config/catwalk.go` | Removed | Catwalk-specific config (not needed) |

### Database (10 files)

| Go path | C++ target | Notes |
|---------|-----------|-------|
| `db/db.go` | `src/ide/session_db.cpp` | SQLite wrapper (extend existing database.cpp) |
| `db/connect.go` | Reuse existing | Already have SQLite in C++ |
| `db/messages.sql.go` | `src/ide/session_db.cpp` | Message queries |
| `db/sessions.sql.go` | `src/ide/session_db.cpp` | Session queries |
| `db/files.sql.go` | `src/ide/session_db.cpp` | File tracking queries |
| `db/models.go` | `src/ide/session_db.cpp` | Data models |

### LSP (5 files)

| Go path | C++ target | Notes |
|---------|-----------|-------|
| `lsp/client.go` | `src/ide/lsp_client.cpp` | LSP client (JSON-RPC over stdio) |
| `lsp/handlers.go` | `src/ide/lsp_handlers.cpp` | LSP response handlers |
| `lsp/language.go` | `src/ide/lsp_language.cpp` | Language detection |
| `lsp/util/edit.go` | `src/ide/lsp_util.cpp` | Text edit application |

### OAuth (7 files)

| Go path | C++ target | Notes |
|---------|-----------|-------|
| `oauth/token.go` | `src/ide/oauth.cpp` | Token management |
| `oauth/copilot/*.go` | Removed | Copilot OAuth (not needed) |
| `oauth/hyper/*.go` | Removed | Hyper OAuth (not needed) |

### Session, Patch, Skills, Shell

| Go path | C++ target | Notes |
|---------|-----------|-------|
| `session/session.go` | `src/ide/session.cpp` | Session lifecycle |
| `patch/patch.go` | `src/ide/patch.cpp` | Unified diff mode |
| `skills/skills.go` | `src/ide/skills.cpp` | Skills system |
| `shell/shell.go` | `src/ide/shell.cpp` | Embedded shell |
| `shell/coreutils.go` | `src/ide/shell_coreutils.cpp` | Built-in coreutils |
| `shell/background.go` | `src/ide/shell_background.cpp` | Background job management |

### Utilities (misc Go packages)

| Go path | C++ target | Notes |
|---------|-----------|-------|
| `diff/*.go` | `src/ide/diff.cpp` | Diff computation |
| `format/*.go` | Reuse existing | String formatting |
| `history/*.go` | `src/ide/history.cpp` | Command history |
| `filetracker/*.go` | `src/ide/filetracker.cpp` | File change tracking |
| `permission/*.go` | `src/ide/permission.cpp` | Tool permission system |
| `projects/*.go` | `src/ide/projects.cpp` | Project detection |
| `pubsub/*.go` | `src/ide/pubsub.cpp` | Internal event bus |
| `uicmd/*.go` | `src/ide/uicmd.cpp` | UI command dispatch |
| `update/*.go` | Reuse existing | Already have update system in C++ |
| `version/*.go` | Reuse existing | Already have versioning in C++ |

---

## Migration Order

The migration happens in six phases. Each phase is independently testable.

### Phase 1 — Agent Core (C++)

Rewrite the agent coordinator, prompt engine, and all tools in C++. This is the brain — everything else depends on it.

1. `agent_coordinator.cpp` — execution loop, tool dispatch
2. `agent_prompt.cpp` — system prompts, context window management
3. `tools/bash_tool.cpp` — shell command execution
4. `tools/edit_tool.cpp` — file editing with search/replace
5. `tools/grep_tool.cpp` — regex search across files
6. `tools/glob_tool.cpp` — file pattern matching
7. `tools/fetch_tool.cpp` — HTTP fetch (reuse existing curl_fetch)
8. `tools/write_tool.cpp` — file creation
9. `tools/download_tool.cpp` — file download with progress
10. `tools/web_search_tool.cpp` — web search (reuse existing web_search module)

Test: run the C++ agent against the same prompts the Go agent handles. Verify identical tool calls and outputs.

### Phase 2 — Session and Config (C++)

Move session management, config, and database layer.

1. `session.cpp` — session lifecycle, thread management, history
2. `session_db.cpp` — SQLite storage (extend existing database.cpp)
3. `config.cpp` — IDE-specific config (extend existing config.cpp)
4. `patch.cpp` — unified diff mode
5. `skills.cpp` — skills system

Test: create sessions, save/load history, apply patches.

### Phase 3 — LSP and MCP (C++)

Wire up protocol support.

1. `lsp_client.cpp` — Language Server Protocol client (JSON-RPC over stdio)
2. `mcp_server.cpp` — Model Context Protocol server
3. `oauth.cpp` — OAuth token flow

Test: connect to a language server, verify completions and diagnostics.

### Phase 4 — Expose synapsed as Shared Library

This is the hardest part of V4. synapsed currently compiles as a standalone binary. To embed it inside a Tauri app, it needs to become a linkable library with a stable C ABI.

1. Add CMake target `synapsed_lib` — builds `libsynapsed.so` (Linux), `libsynapsed.dylib` (macOS), `synapsed.dll` (Windows)
2. Extract `main()` logic into `synapsed_engine.cpp` — init, run loop, shutdown as callable functions
3. Define `include/synapsed_ffi.h` — C ABI header for Tauri FFI:
   - `synapsed_init(config_path)` — start the node engine
   - `synapsed_shutdown()` — graceful stop
   - `synapsed_rpc_call(method, params_json)` — direct RPC without TCP
   - `synapsed_subscribe(event_type, callback)` — push events to frontend
   - `synapsed_get_status()` — node status snapshot
4. Keep the standalone `synapsed` binary target as-is — `synapsed_lib` is an additional target, not a replacement
5. Write integration tests — load `libsynapsed`, init, call RPC, shutdown

Test: load the shared library from a Rust test binary, call `synapsed_init` + `synapsed_rpc_call("node.status")` + `synapsed_shutdown`. Verify clean lifecycle.

### Phase 5 — Tauri Desktop App

Build the Tauri shell and Svelte frontend on top of `libsynapsed`.

1. Initialize Tauri project (`src-tauri/`)
2. Rust FFI bridge — load `libsynapsed`, expose functions to frontend via Tauri commands
3. Svelte frontend with Monaco Editor
4. Chat panel with streaming AI responses (via `synapsed_subscribe` callback)
5. Node status dashboard (peers, Tor, NGT balance)
6. Settings panel (node config, model selection)

Test: launch app, connect to running synapsed instance, edit code, chat with AI.

### Phase 6 — Delete Go

Once all tests pass with the C++ + Tauri stack:

1. Remove `crush-main/` directory entirely
2. Remove all Go build targets from CI
3. Remove Go from `go.mod`, `go.sum`
4. Update build docs — no more `go build`
5. Update ARCHITECTURE.md — no more "130+ Go files"

---

## What Changes from V3

| V3 | V4 |
|----|-----|
| C++ daemon + Go terminal IDE | C++ daemon only (IDE merged in) |
| 130+ Go source files | 0 Go files |
| Terminal TUI (bubbletea) | Tauri desktop app (Svelte + Monaco) |
| JSON-RPC between Go and C++ | Direct C++ function calls |
| Two build systems (CMake + go build) | One build system (CMake + Tauri) |
| Terminal-only interface | Native desktop window |

---

## Final Stack

```
Before (V3):
  C++ (synapsed) + Go (synapseide) + ncurses TUI

After (V4):
  C++ (synapsed + IDE engine) + Rust (Tauri) + TypeScript/Svelte (frontend)
  Zero Go. One daemon. One app.
```

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
