# Tor 9050 vs 9150 Conflict Runbook (SynapseNet / Tor Browser / External Tor)

## Problem Summary

A common operator failure mode is:

- SynapseNet expects Tor on `127.0.0.1:9050` (managed/default path)
- An external Tor (or Tor Browser-related Tor path) is already running on `127.0.0.1:9150`
- Result: SynapseNet keeps trying the wrong port and Tor web readiness never becomes healthy

Typical symptoms:

- `torRequired=true`
- `torReachable=false` or repeated bootstrap retries
- `torReadyForWeb=false`
- external Tor on `9150` actually works from `curl`

## Quick Diagnosis

### Check what is listening

```bash
lsof -nP -iTCP:9050 -sTCP:LISTEN
lsof -nP -iTCP:9150 -sTCP:LISTEN
```

### Check external Tor on 9150 directly

```bash
curl --socks5-hostname 127.0.0.1:9150 https://check.torproject.org/api/ip
```

If the response contains `"IsTor": true`, your external Tor on `9150` is fine.

## Fastest Fix (Use External Tor on 9150)

Configure SynapseNet as a SOCKS client of the already-running external Tor:

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

Then restart SynapseNet.

Expected result:

- `torRuntimeMode = "external"`
- `torSocksPort = 9150`
- `torManaged = false`
- `torReadyForWeb = true` (after probe succeeds)

## When You Should Keep 9050 Instead

Use `9050` only if SynapseNet should own/manage Tor itself (managed/default path) and no
external Tor is intended to be shared.

If you do this:

- stop/disable the external Tor that occupies `9150` (or ignore it)
- let SynapseNet manage Tor on `9050`

## TUI Hints (What SynapseNet Now Shows)

SynapseNet TUI now provides operator hints for this case:

- shared mode hint when using external `9150`
- conflict hint when SynapseNet is on `9050` but an external Tor appears reachable on `9150`

## Related Runbooks

- `docs/tor_shared_external_9150_runbook.md` (one Tor, many clients)
- `docs/naan_tor_validation_runbook.md` (status fields and validation flows)
