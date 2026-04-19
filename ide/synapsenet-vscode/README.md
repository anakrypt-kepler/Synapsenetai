# SynapseNet VS Code Extension (prototype)

This is a minimal first step towards **Synapse IDE (AI CODING + NGT)**.

It talks to a running `synapsed` via local JSON-RPC:
- `model.*` (load/status)
- `ai.complete` (completion)
- IDE chat panel (Copilot-like) via `ai.complete` (optional Web4 injection)
- remote model rentals via `model.remote.list|rent|end`
- inline (ghost text) completions via `ai.complete`
- patch suggestions (preview/apply) via `ai.complete` (unified diff)
- `poe.submit_code` (submit a patch/diff as a CODE contribution)
- GitHub Quests parity workflow (configure/select active quest, branch/fork/commit/PR with PoE submitId linkage)

## Run synapsed

Start the node (TUI or daemon). RPC must be enabled (default `8332`).

Example:

```bash
cd KeplerSynapseNet
TERM=xterm-256color ./build/synapsed -D /tmp/synapsenet_vscode_dev --dev

# external Tor 9150 mode (optional)
# TERM=xterm-256color ./build/synapsed \
#   -D /tmp/synapsenet_vscode_dev \
#   --dev \
#   -c /tmp/synapsenet_external_9150.conf
```

For bridge-enabled external Tor (`9150`) setup and Tor Browser sharing, see:
- `KeplerSynapseNet/docs/tor_shared_external_9150_runbook.md`
- `KeplerSynapseNet/docs/macos_tor_obfs4_automation_runbook.md`

No-conflict shared Tor sequence (macOS, one Tor runtime on `9150`):

```bash
cd <repo-root>/KeplerSynapseNet
pkill -f "/Applications/Tor Browser.app/Contents/MacOS/Tor/tor" || true
pkill -f "/opt/homebrew/bin/tor" || true
sleep 1

tools/macos_tor_obfs4_helper.sh \
  --bridges-file /tmp/bridges.txt \
  --socks-port 9150 \
  --control-port 9151 \
  --bootstrap-check \
  --bootstrap-attempts 6 \
  --bridge-subset-size 4 \
  --takeover-port-owner \
  --keep-running \
  --out /tmp/tor-obfs4-synapsenet.conf

TERM=xterm-256color ./build/synapsed -D /tmp/synapsenet_vscode_dev --dev -c /tmp/synapsenet_external_9150.conf

TOR_PROVIDER=none TOR_SOCKS_HOST=127.0.0.1 TOR_SOCKS_PORT=9150 \
"/Applications/Tor Browser.app/Contents/MacOS/firefox" --new-instance
```

Do not run plain `tor` manually after this sequence.

Quick RPC check:

```bash
curl -sS -H 'content-type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"node.status","params":[]}' \
  http://127.0.0.1:8332 | jq '.result | {running,rpcPort,torRuntimeMode,torBootstrapState}'
```

## Configure NAAN site allowlist in SynapseNet TUI

When the node is running, configure allowed web domains for NAAN directly in TUI:

1. `Settings`
2. `[W] NAAN Site Allowlist (clearnet/onion)`
3. `[C]` clearnet rules, `[O]` onion rules
4. `Enter` adds a rule, empty `Enter` saves to:
   - `<DATA_DIR>/naan_agent_web.conf`

Security:
- Only add trusted domains.
- Operator is responsible for malicious-site risk.
- Use AV/EDR + sandboxing for untrusted content.
- AI/IDE analysis can assist triage, but is not an antivirus replacement.

## Use in VS Code

1. Open this folder in VS Code.
2. Open `ide/synapsenet-vscode/`.
3. Run the Extension Host (F5 in the VS Code extension dev workflow).
4. In the Extension Host window:
   - `SynapseNet: Model Status`
   - `SynapseNet: Model List (Quick Pick)`
   - `SynapseNet: Load Model`
   - `SynapseNet: Unload Model`
   - `SynapseNet: Remote Offers`
   - `SynapseNet: Remote Rent Model`
   - `SynapseNet: Remote End Session`
   - `SynapseNet: AI Complete (Insert)`
   - `SynapseNet: AI Stop`
   - `SynapseNet: Open Chat (Web4 optional)`
   - `SynapseNet: Toggle Inline Completions`
   - `SynapseNet: Quest Configure`
   - `SynapseNet: Quest Select Active Issue`
   - `SynapseNet: Quest Active`
   - `SynapseNet: Quest Clear Active`
   - `SynapseNet: Quest Checkout Branch`
   - `SynapseNet: Quest Fork Repo`
   - `SynapseNet: Quest Commit`
   - `SynapseNet: Quest Create PR`
   - `SynapseNet: Suggest Patch (Preview/Apply)`
   - `SynapseNet: Submit Code Patch (PoE)`

Settings:
- `synapsenet.rpcHost` (default `127.0.0.1`)
- `synapsenet.rpcPort` (default `8332`)
- `synapsenet.inlineEnabled` (default `true`)
- `synapsenet.inlineDebounceMs` (default `250`)
- `synapsenet.inlineMaxTokens` (default `96`)
- `synapsenet.patchMaxTokens` (default `1024`)
- `synapsenet.remoteSessionId` (default `""`)
- `synapsenet.remoteUseByDefault` (default `false`)
- `synapsenet.questRepo` (default `""`, format `owner/name`)
- `synapsenet.questToken` (default `""`)
- `synapsenet.questMinSubmitPowBits` (default `16`)

## Deterministic Manual Verification

1. Start `synapsed` and make sure JSON-RPC is reachable at `synapsenet.rpcHost:synapsenet.rpcPort`.
2. Run `SynapseNet: Quest Configure` and set:
   - `questRepo` to a real `owner/name`
   - `questToken` with repo fork/PR permissions
   - `questMinSubmitPowBits` to expected policy (default `16`)
3. Run `SynapseNet: Quest Select Active Issue`:
   - verify open issues load from GitHub
   - pick one issue and confirm status bar shows `Q#<issue>`
4. Run `SynapseNet: Quest Checkout Branch`:
   - verify current branch becomes `quest-<issue>`
5. Run `SynapseNet: Quest Commit`:
   - make a small file change first
   - verify commit is created with quest metadata in body
6. Run `SynapseNet: Quest Fork Repo`:
   - verify fork is created and local git remote `fork` is added when applicable
7. Run `SynapseNet: Quest Create PR`:
   - provide a valid PoE CODE `submitId`
   - verify submitId PoW gate + `poe.fetch_code` validation run before PR creation
   - verify created PR body includes quest id and submitId

## Deterministic Verification Script

Use the bundled verifier to validate post-conditions after running the VS Code quest commands:

```bash
bash KeplerSynapseNet/ide/synapsenet-vscode/scripts/verify_quest_flow.sh \
  --repo owner/name \
  --issue 123 \
  --submit-id <64-hex-submit-id> \
  --workspace /absolute/path/to/repo
```

Optional flags:
- `--rpc-url http://127.0.0.1:8332` (default)
- `--min-pow-bits 16` (default)
- `--pr-url https://github.com/owner/name/pull/123` (format check)

The script checks:
- required tools (`git`, `curl`, `jq`)
- workspace is a git repository
- current branch is `quest-<issue>`
- latest commit includes `Quest: #<issue>` and `PoE CODE submitId: <id>`
- submitId leading-zero PoW bits meet policy
- `poe.fetch_code` for the submitId succeeds via RPC

## Recovery Cases

### Missing token (`Quest Fork Repo`, `Quest Create PR`)
- Expected message: `token required`.
- Recovery:
  1. Run `SynapseNet: Quest Configure`.
  2. Set `questToken` to a token with repo fork/PR permissions.
  3. Re-run the quest command.

### Invalid submitId (`Quest Create PR`)
- Expected messages:
  - `submitId must be 64 hex chars`, or
  - `submitId PoW too low: <bits> < <min> leading-zero bits`, or
  - RPC error from `poe.fetch_code` (submitId not found).
- Recovery:
  1. Confirm submitId comes from `poe.submit_code`.
  2. Verify policy in `synapsenet.questMinSubmitPowBits`.
  3. Re-submit code if needed to get a valid submitId, then retry PR command.

### Cooldown/rate-limit (`Quest Fork Repo`, `Quest Create PR`)
- Expected message: `rate limited: wait <N>s`.
- Recovery:
  1. Wait for the cooldown window to expire (`fork` = 10 minutes, `pr` = 60 seconds).
  2. Re-run command without changing quest state.

---

Maintained by [Kepler](https://github.com/anakrypt).
