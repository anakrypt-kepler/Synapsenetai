# Tor Bridge Troubleshooting (TLS Handshake Stalls / Bridge Parse Errors)

This runbook covers two common operator failures when bringing up Tor for SynapseNet/NAAN:

- Tor stalls at `Bootstrapped 10% (conn_done)` with repeated `TLS_ERROR`
- Tor fails to start because the bridge config (`torrc`) is malformed

This guide assumes the recommended setup: external Tor on `127.0.0.1:9150`.

## Symptom A: TLS handshake stalls at 10%

Typical Tor log pattern:

- `Bootstrapped 10% (conn_done): Connected to a relay`
- repeated `Problem bootstrapping... TLS_ERROR`
- repeated `unexpected eof while reading`

Meaning:

- Tor can open TCP connections, but relay TLS handshakes are being disrupted or blocked.
- SynapseNet may still show SOCKS connectivity while web traffic is not ready.

## Symptom B: Bridge parse/config errors

Typical messages:

- `Option 'UseBridges' used more than once`
- `Bridge line did not parse`
- `'127.0.0.1:9150' is not a k=v item`
- `Unknown option '...'`

Meaning:

- `torrc` contains duplicate keys, invalid `Bridge` lines, or pasted placeholders/snippets.

## Fast Triage (Do This In Order)

1. Confirm which port SynapseNet is using (`9050` or `9150`)
2. Confirm Tor is listening on that port
3. Confirm Tor is bootstrapped to `100%`
4. Confirm a real request works through SOCKS
5. Start SynapseNet in `external` mode

## Check Tor Listener

```bash
lsof -nP -iTCP:9150 -sTCP:LISTEN
```

Expected: Tor process listening on `127.0.0.1:9150`.

## Check Real Tor Traffic (not just open SOCKS port)

```bash
curl --socks5-hostname 127.0.0.1:9150 https://check.torproject.org/api/ip --max-time 30
```

Expected JSON includes:

- `"IsTor": true`

If this times out, Tor is not ready for web traffic yet.

## Valid obfs4 `torrc` Pattern (minimal)

```conf
SocksPort 9150
UseBridges 1
ClientTransportPlugin obfs4 exec /opt/homebrew/bin/obfs4proxy

Bridge obfs4 <IP:PORT> <FINGERPRINT> cert=<CERT> iat-mode=0
Bridge obfs4 <IP:PORT> <FINGERPRINT> cert=<CERT> iat-mode=0
```

Rules:

- `UseBridges 1` only once
- Every bridge line must begin with `Bridge obfs4`
- Do not paste placeholders like `...`
- Do not paste raw non-config lines into `torrc`

Common helper failure:

- `bridges file not found: /tmp/bridges.txt`
  - Create the file first and paste bridge lines into it:
  - Or use the SynapseNet TUI startup wizard (bridge-mode step): press `[P]` to paste bridges and save them to `<DATA_DIR>/tor/bridges.obfs4.txt`.

```bash
nano /tmp/bridges.txt
```

## Validate `torrc` Before Running Tor

```bash
/opt/homebrew/opt/tor/bin/tor --verify-config -f /tmp/tor-obfs4.conf
```

If validation fails, fix syntax before launching Tor or SynapseNet.

## If Tor Stalls at 10% (TLS_ERROR)

Recommended path:

1. Use bridges (`obfs4`) instead of direct relays
2. Run Tor externally on `9150`
3. Wait for `Bootstrapped 100% (done): Done`
4. Re-run the `curl --socks5-hostname ... check.torproject.org/api/ip` check
5. Start SynapseNet in `external` mode

## SynapseNet External Tor Config (9150)

```ini
agent.tor.mode=external
agent.tor.required=true
agent.routing.allow_clearnet_fallback=false
agent.routing.allow_p2p_clearnet_fallback=false
agent.tor.socks_host=127.0.0.1
agent.tor.socks_port=9150
tor.socks.host=127.0.0.1
tor.socks.port=9150
```

Optional:

```ini
tor.control.port=9151
```

## `9050` vs `9150` mismatch hint

If SynapseNet expects `9050` while your working bridge Tor is on `9150`, `naan.status` may show:

- `torConflictHint9050=true`

See:

- `docs/tor_9050_9150_conflict_runbook.md`

## Related runbooks

- `docs/naan_tor_validation_runbook.md`
- `docs/tor_shared_external_9150_runbook.md`
- `docs/tor_managed_cleanup_safety_runbook.md`
