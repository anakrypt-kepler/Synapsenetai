# Shared External Tor (9150) Runbook: SynapseNet + Tor Browser + CLI

## Goal

Run a single external Tor process (typically bridge-enabled, for example obfs4) on
`127.0.0.1:9150` and share it across:

- SynapseNet / NAAN (terminal node)
- Tor Browser (configured as a SOCKS client, not Tor owner)
- CLI tools (`curl`, `w3m`, etc.)

This avoids port conflicts and makes operator behavior predictable.

## Why Port Conflicts Happen

Tor Browser normally starts its own Tor process and tries to bind its SOCKS port
(commonly `9150`). If SynapseNet or an operator-run Tor process already owns `9150`,
Tor Browser's built-in Tor will conflict.

The correct shared setup is:

- one external Tor process owns `127.0.0.1:9150`
- SynapseNet uses `agent.tor.mode=external`
- Tor Browser uses the same Tor via SOCKS5 proxy settings

## Architecture (One Tor, Many Clients)

```
                 [ External Tor (obfs4) ]
                      127.0.0.1:9150
                             |
            +----------------+----------------+
            |                |                |
      SynapseNet/NAAN    Tor Browser      curl / w3m
```

## Quick No-Conflict Sequence (macOS)

```bash
# from repository root
cd <repo-root>/KeplerSynapseNet

# stop extra Tor owners first
pkill -f "/Applications/Tor Browser.app/Contents/MacOS/Tor/tor" || true
pkill -f "/opt/homebrew/bin/tor" || true
sleep 1

# start one external Tor on 9150
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

# verify Tor path
lsof -nP -iTCP:9150 -sTCP:LISTEN
curl --socks5-hostname 127.0.0.1:9150 https://check.torproject.org/api/ip --max-time 30

# run SynapseNet with external Tor config
TERM=xterm-256color ./build/synapsed -D /tmp/synapsenet_fresh --dev -c /tmp/synapsenet_external_9150.conf

# run Tor Browser as SOCKS client of the same Tor
TOR_PROVIDER=none TOR_SOCKS_HOST=127.0.0.1 TOR_SOCKS_PORT=9150 \
"/Applications/Tor Browser.app/Contents/MacOS/firefox" --new-instance
```

Notes:
- Do not run plain `tor` manually after this sequence.
- If helper reports `9150 already in use` and curl returns `"IsTor":true`, the shared Tor is already running; continue with SynapseNet/Tor Browser client mode.

## 1) Start the External Tor (9150)

Use your existing external Tor workflow (for example obfs4 bridges). On macOS, you can
use the helper script added to this project:

```bash
cd KeplerSynapseNet
nano /tmp/bridges.txt
tools/macos_tor_obfs4_helper.sh \
  --bridges-file /tmp/bridges.txt \
  --out /tmp/tor-obfs4.conf \
  --write-synapsenet-snippet /tmp/synapsenet_external_9150.conf \
  --bootstrap-check
```

If you already have a working external Tor process, keep it running and make sure it is
listening on `127.0.0.1:9150`.

Notes:
- The bridges file should contain one `Bridge obfs4 ...` line per row.
- SynapseNet can also help you create the bridges file: in the TUI startup wizard (bridge-mode step), press `[P]` to paste bridges and save them to `<DATA_DIR>/tor/bridges.obfs4.txt`.
- After startup, you can update bridges from the Settings screen: press `[T]` -> paste bridges.
- Do not commit bridge lines into git. Treat them as sensitive and rotate them as needed.
- If you see `Proxy server is refusing connections` in Tor Browser, it usually means no process
  is listening on `127.0.0.1:9150` or Tor is not bootstrapped yet.

## 2) Configure SynapseNet to Use External Tor (No Takeover)

Add this to the active `synapsenet.conf`:

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

Important:

- `agent.tor.mode=external` tells SynapseNet not to start or stop a managed Tor runtime
  on the same port.
- SynapseNet remains a SOCKS client only in this mode.

Bridge pool (optional, for NAAN/web connector bridge provider config):
- Bridge file: `<DATA_DIR>/tor/bridges.obfs4.txt`
- Cache file: `<DATA_DIR>/tor/bridge_pool_cache.json`

These are controlled via `naan_agent_web.conf` keys like:

```ini
agent.tor.bridge.transport=obfs4
agent.tor.bridge.source=file
```

## 3) Configure Tor Browser to Use the Same External Tor

In Tor Browser, open connection/proxy settings and configure Tor Browser as a SOCKS
client:

- `SOCKS Host`: `127.0.0.1`
- `Port`: `9150`
- `SOCKS v5`
- Enable remote DNS via SOCKS (`Proxy DNS when using SOCKS v5`) if the UI shows that option

Then restart Tor Browser.

Expected result:

- Tor Browser should stop conflicting on `9150`
- Tor Browser uses the already-running external Tor process

macOS shortcut (launch Tor Browser as an external SOCKS client, without starting its own Tor):

```bash
TOR_PROVIDER=none TOR_SOCKS_HOST=127.0.0.1 TOR_SOCKS_PORT=9150 \
"/Applications/Tor Browser.app/Contents/MacOS/firefox" --new-instance
```

## 4) Validation Checklist (Shared External Tor Workflow)

### CLI check (Tor path works)

```bash
curl --socks5-hostname 127.0.0.1:9150 https://check.torproject.org/api/ip
```

Expected result:

- JSON response with `"IsTor": true`

### Tor Browser check

Open:

- `https://check.torproject.org`

Expected result:

- Page reports: "You are using Tor"

### SynapseNet / NAAN status check

```bash
cd KeplerSynapseNet
TERM=xterm-256color ./build/synapsed -D /path/to/datadir --dev
```

Then query status (adjust RPC port if needed):

```bash
./build/synapsed -D /path/to/datadir --dev status
```

Expected `node.status` / `naan.status` fields:

- `torRuntimeMode = "external"`
- `torManaged = false`
- `torSocksPort = 9150`
- `torSocksReachable = true`
- `torReadyForWeb = true` (after a successful Tor-routed web probe)

## 5) Operational Notes

- Shared mode means SynapseNet and Tor Browser use the same external Tor path and often
  the same exit identity/IP at a given time. This is operationally convenient but may be
  undesirable if you want strict separation between browsing and NAAN traffic.
- If you need separation, run separate Tor instances on different SOCKS ports.
- Tor Browser / external Tor setups may not expose a ControlPort. In that case, SynapseNet
  web routing can still work, while onion-service FULL mode may remain unavailable.

## 6) Common Failure Modes

- `9150 already in use`:
  - Another Tor process owns the port. Decide which process is the owner and configure the
    others as clients.
- SynapseNet shows `torRuntimeMode=external` but `torReadyForWeb=false`:
  - SOCKS is listening, but Tor is not bootstrapped yet or bridge traffic is failing.
  - Re-check external Tor logs and validate with the CLI `curl` command above.
- Tor Browser still tries to start its own Tor:
  - Re-check Tor Browser proxy/connection settings and restart the browser.
