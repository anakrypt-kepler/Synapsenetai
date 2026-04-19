# macOS Tor obfs4 Automation Runbook

This runbook validates SynapseNet Tor behavior on macOS for both:
- external Tor mode (`agent.tor.mode=external`)
- managed Tor mode (`agent.tor.mode=managed`)

## 1. Prerequisites

```bash
brew install tor obfs4proxy
```

Optional:

```bash
brew install snowflake-client
```

## 2. Generate and bootstrap external obfs4 Tor

Create a bridge file (one `Bridge obfs4 ...` line per row), then run:

```bash
cd KeplerSynapseNet
nano /tmp/bridges.txt
tools/macos_tor_obfs4_helper.sh \
  --bridges-file /tmp/bridges.txt \
  --socks-port 9150 \
  --control-port 9151 \
  --bootstrap-check \
  --bootstrap-attempts 6 \
  --bridge-subset-size 4 \
  --takeover-port-owner \
  --keep-running \
  --out /tmp/tor-obfs4-synapsenet.conf \
  --write-synapsenet-snippet /tmp/synapsenet_external_9150.conf
```

Expected result:
- Tor reaches `Bootstrapped 100%`
- SOCKS probe succeeds
- external Tor remains running on `127.0.0.1:9150`

Notes:
- Do not commit bridge lines into git. Treat them as sensitive and rotate them as needed.
- You can also paste bridges in the SynapseNet TUI startup wizard (bridge-mode step): press `[P]` to save them to `<DATA_DIR>/tor/bridges.obfs4.txt` and use that file with the helper.
- `--bootstrap-attempts` rotates bridge subsets across retries.
- `--bridge-subset-size` limits per-attempt bridge count and helps skip bad/stale bridges faster.
- `--takeover-port-owner` reclaims occupied SOCKS port (kills current listener PID);
  for Tor Browser runtime it also stops the owning controller PID extracted from
  `__OwningControllerProcess`.

## 3. Validate SynapseNet external + managed modes

Run validator:

```bash
cd KeplerSynapseNet
tools/macos_tor_validate.sh
```

The validator:
- starts a temporary `synapsed` node in external mode and checks `node.status`
- requires `torRuntimeMode=external` and (by default) `torReadyForWeb=true`
- starts a temporary `synapsed` node in managed mode and checks `node.status`
- verifies managed Tor PID cleanup after node shutdown
- writes evidence into `build/evidence/macos_tor_validate_<timestamp>/`

Useful flags:

```bash
tools/macos_tor_validate.sh --no-require-external-web-ready
tools/macos_tor_validate.sh --skip-external
tools/macos_tor_validate.sh --skip-managed
```

## 3.1 One-shot external 9150 e2e runner

Use one command to run helper bootstrap + external validator and collect one evidence bundle:

```bash
cd KeplerSynapseNet
tools/macos_tor_external_9150_e2e.sh \
  --bridges-file /tmp/bridges_primary.txt \
  --bridges-file /tmp/bridges_fallback.txt \
  --bootstrap-attempts 6 \
  --bridge-subset-size 4 \
  --takeover-port-owner
```

Output:
- evidence dir: `build/evidence/macos_tor_external_9150_e2e_<timestamp>/`
- summary file: `summary.json`
- helper logs: `helper_*.log` (one per bridge-file fallback attempt)
- validator log: `validator.log`

Notes:
- `--bridges-file` is repeatable and processed in provided order.
- The first bridge file that passes helper bootstrap is recorded in `summary.json`
  as `selectedBridgeFile`.

## 4. Shared external Tor workflow (Tor Browser + SynapseNet + CLI)

If you use one external Tor instance on `9150`:
- configure SynapseNet with `agent.tor.mode=external`
- configure Tor Browser SOCKS5 proxy to `127.0.0.1:9150`
- keep SynapseNet managed Tor disabled on that same port

See `docs/tor_shared_external_9150_runbook.md` for full shared-mode steps.

## 5. Common failures

- `SOCKS port ... already in use`
  - another Tor process owns the port; switch port or stop conflict.
  - helper now prints owner PID/command; if owner is Tor Browser with
    `DisableNetwork=1`, close Tor Browser or move it to external SOCKS-client mode.
  - if reclaim is intended, rerun with `--takeover-port-owner`.
- `Tor bootstrap did not reach 100%`
  - bridge set is stale/blocked; refresh bridge pool.
- external validator shows `torReadyForWeb=false`
  - SOCKS is up, but circuit/web probe is not ready (check bridge/bootstrap logs).
