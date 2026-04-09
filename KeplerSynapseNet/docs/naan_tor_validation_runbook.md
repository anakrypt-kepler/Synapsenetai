# NAAN Tor Validation Runbook

## Scope

This runbook validates four reliability requirements:

1. Tor SOCKS is reachable and actively used.
2. DuckDuckGo clearnet traffic can be routed through Tor.
3. Ahmia onion traffic can be routed through Tor.
4. `naan.status` exposes route and connector observability fields required for runtime verification.

## `naan.status` Fields

The following keys are now available at top-level and inside `connectorStatus`:

- `activeEngines`
- `torRoute`
- `policyBlocks`
- `fetchCounts`

`activeEngines` reports active clearnet/darknet engines and custom engine counts.

`torRoute` reports route mode and route policy decisions:

- `mode`
- `required`
- `socksReachable`
- `reachable`
- `webReady`
- `managed`
- `managedPid`
- `runtimeMode`
- `degraded`
- `allowWebClearnet`
- `allowWebOnion`
- `allowP2PDiscovery`
- `clearnetFallbackAllowed`
- `p2pFallbackAllowed`

`policyBlocks` reports:

- `clearnet`
- `tor`
- `onion`
- `total`

`fetchCounts` reports:

- `totalSearches`
- `clearnetSearches`
- `darknetSearches`
- `successfulFetches`
- `failedFetches`
- `pagesExtracted`
- `bytesDownloaded`
- `avgResponseTime`

Additional top-level Tor readiness fields:

- `torSocksReachable`
- `torReadyForWeb`
- `torRuntimeMode` (`auto`, `external`, `managed`)
- `torControlReachable`
- `torReadyForOnionService`
- `torOnionServiceActive`
- `torOnionServiceState`
- `torManagedPid`
- `torWebProbeLastAt`
- `torWebProbeLastOkAt`
- `torWebProbeExitCode`
- `torWebProbeLastError`
- `torBridgeProvider`
- `torBridgeProviderUpdatedAt`
- `torBridgeCacheAgeSeconds`

## End-to-End Script

Path: `tools/verify_tor_naan_e2e.sh`

Modes:

- `--live-tor`: full Tor validation.
- `--ci-offline`: deterministic `naan.status` validation without external network fetches.

Live mode behavior:

- Uses Tor SOCKS at `127.0.0.1:9050`.
- Runs bootstrap precheck before external fetches:
  - TCP probe to SOCKS (`TOR_SOCKS_HOST`/`TOR_SOCKS_PORT`)
  - ControlPort probe + `PROTOCOLINFO` query (`TOR_CONTROL_HOST`/`TOR_CONTROL_PORT`) when reachable.
- Uses DuckDuckGo clearnet URL `https://html.duckduckgo.com/html/?q=synapsenet+tor+validation`.
- Uses Ahmia onion URL `http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/search/?q=synapsenet`.
- Starts `build/synapsed` in regtest mode and calls `naan.status` over RPC.
- Writes timestamped evidence under `build/evidence/tor_naan_e2e_<UTC timestamp>`.

Offline mode behavior:

- Skips live Tor fetch checks.
- Still starts node and validates required `naan.status` fields.
- Produces deterministic artifacts for CI.

## Run

```bash
cmake --build build
ctest --test-dir build --output-on-failure
tools/verify_tor_naan_e2e.sh --live-tor
tools/verify_tor_naan_e2e.sh --ci-offline
```

## External Tor on 9150 (Bridges / Tor Browser style)

Use this mode when the operator runs Tor separately (for example, an obfs4 bridge Tor on
`127.0.0.1:9150`) and SynapseNet must not start or stop its own managed Tor runtime.

Add to the active `synapsenet.conf`:

```ini
agent.tor.required=true
agent.tor.mode=external
agent.routing.allow_clearnet_fallback=false
agent.routing.allow_p2p_clearnet_fallback=false

agent.tor.socks_host=127.0.0.1
agent.tor.socks_port=9150
tor.socks.host=127.0.0.1
tor.socks.port=9150
```

Expected `node.status` / `naan.status` characteristics in this mode:

- `torRuntimeMode = "external"`
- `torManaged = false`
- `torSocksReachable = true` when external Tor SOCKS is listening
- `torReadyForWeb = true` only after a real Tor-routed web probe succeeds

## macOS obfs4 Helper (Generate torrc + Bootstrap Validation)

Use `tools/macos_tor_obfs4_helper.sh` to generate a valid obfs4 `torrc`, verify it with
`tor --verify-config`, optionally run a live bootstrap check, and write a SynapseNet
`agent.tor.mode=external` snippet for `9150`.

Example:

```bash
nano /tmp/bridges.txt
tools/macos_tor_obfs4_helper.sh \
  --bridges-file /tmp/bridges.txt \
  --out /tmp/tor-obfs4.conf \
  --write-synapsenet-snippet /tmp/synapsenet_external_9150.conf \
  --bootstrap-check
```

Notes:

- Bridge lines can be `Bridge obfs4 ...` or `obfs4 ...`; the helper normalizes and deduplicates.
- You can also paste bridges in the SynapseNet TUI startup wizard (bridge-mode step): press `[P]` to save them to `<DATA_DIR>/tor/bridges.obfs4.txt`.
- After startup, you can update bridges from the Settings screen: press `[T]` -> paste bridges.
- Do not commit bridge lines into git. Treat them as sensitive and rotate them as needed.
- `--bootstrap-check` starts a temporary Tor runtime, waits for `Bootstrapped 100%`, then probes a URL via SOCKS.
- Use `--keep-running` if you want the temporary Tor runtime to remain active after validation.

## Bridge pool sources (SynapseNet runtime)

SynapseNet stores and reads the local obfs4 bridge pool under the node data directory:

- Bridge file: `<DATA_DIR>/tor/bridges.obfs4.txt`
- Cache file: `<DATA_DIR>/tor/bridge_pool_cache.json`

Example:
- If you run `synapsed -D /tmp/synapsenet_dev --dev`, then the bridge file path is:
  `/tmp/synapsenet_dev/tor/bridges.obfs4.txt`

Bridge provider config keys (in `naan_agent_web.conf`):

```ini
agent.tor.bridge.transport=obfs4
agent.tor.bridge.source=file
agent.tor.bridge.min_pool_size=2
agent.tor.bridge.randomize_per_node=1
```

Notes:
- `agent.tor.bridge.source=remote` is supported, but remote sources may require interactive verification.
  If the remote response looks like HTML/CAPTCHA, SynapseNet returns `manual_step_required` and asks you
  to provide bridge lines via file/manual config.

See also:

- `docs/tor_shared_external_9150_runbook.md` for the shared "one Tor, many clients"
  workflow (SynapseNet + Tor Browser + CLI tools on the same external Tor `9150`).
- `docs/tor_9050_9150_conflict_runbook.md` for quick diagnosis/fix when SynapseNet is
  using `9050` but external Tor is actually running on `9150`.
- `docs/tor_managed_cleanup_safety_runbook.md` for managed-Tor shutdown ownership checks
  and safe cleanup behavior (no blind `killall tor`).
- `docs/tor_bridge_troubleshooting_runbook.md` for TLS handshake stalls at `10%`,
  bridge parse errors, and `torrc` validation workflow.
- `docs/linux_tor_obfs4_automation_runbook.md` for Linux-specific automation details
  (distro binary detection, user/systemd mode, ownership checks, safe PID cleanup).
- `docs/windows_tor_obfs4_automation_runbook.md` for Windows-specific automation details
  (Tor Browser/Expert path detection, user/service mode, tasklist/taskkill safety checks).
- Platform smoke helpers:
  - macOS: `tools/macos_tor_obfs4_helper.sh`
  - Linux: `tools/linux_tor_obfs4_smoke.sh`
  - Windows: `tools/windows_tor_obfs4_smoke.ps1`
  - Windows validator: `tools/windows_tor_validate.ps1`

## Windows obfs4 Helper (Generate torrc + Bootstrap Validation)

Use `tools/windows_tor_obfs4_smoke.ps1` to generate a valid obfs4 `torrc`, verify it
with `tor --verify-config`, and optionally run a live bootstrap + DDG probe check.

User-process mode example:

```powershell
pwsh -File tools/windows_tor_obfs4_smoke.ps1 `
  -Mode user `
  -BridgesFile C:\temp\bridges.txt `
  -BootstrapCheck `
  -WriteSynapseNetSnippet C:\temp\synapsenet_external_9150.conf
```

Service mode example:

```powershell
pwsh -File tools/windows_tor_obfs4_smoke.ps1 `
  -Mode service `
  -ServiceName tor `
  -ServiceStart `
  -BridgesFile C:\temp\bridges.txt `
  -BootstrapCheck
```

Standalone runtime validator example:

```powershell
pwsh -File tools/windows_tor_validate.ps1 `
  -SocksHost 127.0.0.1 `
  -SocksPort 9150 `
  -ControlHost 127.0.0.1 `
  -ControlPort 9151
```

## Environment Overrides

```bash
BIN_PATH=/absolute/path/to/synapsed \
TOR_PROXY=127.0.0.1:9050 \
TOR_SOCKS_HOST=127.0.0.1 \
TOR_SOCKS_PORT=9050 \
TOR_CONTROL_HOST=127.0.0.1 \
TOR_CONTROL_PORT=9051 \
TOR_PRECHECK_TIMEOUT=3 \
RPC_PORT=18332 \
P2P_PORT=18444 \
DDG_URL="https://html.duckduckgo.com/html/?q=synapsenet+validation" \
AHMIA_ONION_URL="http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/search/?q=synapsenet" \
OUTPUT_ROOT=/tmp/synapsenet-evidence \
tools/verify_tor_naan_e2e.sh --live-tor
```

## Evidence Artifacts

Each run folder includes:

- `run.log`
- `tor_bootstrap_precheck.json`
- `tor_control_protocolinfo.txt` (when ControlPort probe succeeds)
- `tor_check.body`, `tor_check.headers`, `tor_check.meta`
- `duckduckgo_clearnet.body`, `duckduckgo_clearnet.headers`, `duckduckgo_clearnet.meta`
- `ahmia_onion.body`, `ahmia_onion.headers`, `ahmia_onion.meta`
- `node_runtime.log`
- `node_status_rpc.json`
- `naan_status_rpc.json`
- `naan_status_required_fields.json`

These artifacts are intended to be attached to validation reports as objective proof.
