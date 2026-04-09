# Safe Cleanup of SynapseNet-Managed Tor Runtimes

## Goal

Stop only Tor processes that were started by SynapseNet (managed mode), without touching:

- operator-run external Tor
- Tor Browser-related Tor processes
- unrelated system/user Tor processes

## Current Safety Model (Implemented)

SynapseNet managed-Tor cleanup is constrained by ownership checks:

- PID file (`<dataDir>/tor/tor.pid`) is used first
- process command line must match SynapseNet-owned runtime characteristics:
  - contains `tor`
  - contains the SynapseNet managed `DataDirectory` path
  - contains `--RunAsDaemon 1`
- fallback scan (when PID file is missing/stale) searches `ps` output and only matches
  Tor processes with the same managed `DataDirectory` fingerprint

This avoids blind `killall tor` behavior.

## What SynapseNet Will and Will Not Stop

### Will stop

- Tor process started by the same SynapseNet data directory in managed mode
- Tor process matching the SynapseNet-managed `DataDirectory` fingerprint even if the
  `tor.pid` file is missing

### Will not stop

- external Tor on `127.0.0.1:9150` used in `agent.tor.mode=external`
- Tor Browser-owned Tor processes
- unrelated Tor daemons with different `DataDirectory`

## Operational Checks

### Inspect managed Tor PID file

```bash
ls -l /path/to/datadir/tor/tor.pid
cat /path/to/datadir/tor/tor.pid
```

### Inspect candidate Tor processes

```bash
ps -axww | grep '[t]or'
```

Look for the `--DataDirectory /path/to/datadir/tor` argument.

### Verify SynapseNet cleanup behavior in logs

On shutdown, a managed runtime stop should log:

- `Stopped managed Tor runtime (pid=...)`

If nothing owned is found, SynapseNet should skip stop without killing unrelated Tor.

## Troubleshooting

### SynapseNet shutdown did not stop managed Tor

Possible causes:

- PID file missing and fallback match failed (command line did not match expected fingerprint)
- Tor process did not exit after `SIGTERM`

Actions:

- inspect `ps -axww | grep '[t]or'`
- verify `DataDirectory` matches the SynapseNet data directory
- inspect node logs for warnings around managed Tor stop

### SynapseNet should not stop my external Tor (9150)

Use:

```ini
agent.tor.mode=external
agent.tor.socks_port=9150
```

In external mode, SynapseNet acts as a SOCKS client and does not manage/own the external Tor lifecycle.

## Related Runbooks

- `docs/tor_shared_external_9150_runbook.md`
- `docs/tor_9050_9150_conflict_runbook.md`
- `docs/naan_tor_validation_runbook.md`
