# Windows Tor obfs4 Automation Runbook

This runbook describes the Windows helpers:

- `tools/windows_tor_obfs4_smoke.ps1`
- `tools/windows_tor_validate.ps1`

It is intended for SynapseNet external Tor validation and supports both:

- local user-process mode
- Windows service mode

## What the helper validates

1. Windows Tor path detection for:
   - Tor Browser layout
   - Tor Expert/custom layout
   - explicit custom path (`-TorRoot`, `-TorBin`, `-Obfs4ProxyBin`)
2. `torrc` generation and `tor --verify-config`
3. Safe process lifecycle in user mode using:
   - `tasklist` for PID existence checks
   - `taskkill` for shutdown
   - ownership/fingerprint checks before termination
4. SOCKS readiness and DDG probe through Tor
5. Explicit `user` vs `service` runtime mode selection

## User mode example

```powershell
pwsh -File tools/windows_tor_obfs4_smoke.ps1 \
  -Mode user \
  -BridgesFile C:\\temp\\bridges.txt \
  -BootstrapCheck \
  -WriteSynapseNetSnippet C:\\temp\\synapsenet_external_9150.conf
```

Notes:

- The bridges file should contain one `Bridge obfs4 ...` line per row.
- Do not commit bridge lines into git. Treat them as sensitive and rotate them as needed.
- You can also paste bridges in the SynapseNet TUI startup wizard (bridge-mode step): press `[P]` to save them to `<DATA_DIR>/tor/bridges.obfs4.txt` and copy that file to Windows (or paste again) for the helper.
- default user-mode `torrc` path: `%TEMP%\\tor-obfs4-windows.conf`
- default user-mode `DataDirectory`: `%TEMP%\\synapsenet_tor_obfs4_windows`
- helper stops only Tor process it started when PID ownership/fingerprint matches

## Service mode example

```powershell
pwsh -File tools/windows_tor_obfs4_smoke.ps1 \
  -Mode service \
  -ServiceName tor \
  -ServiceStart \
  -BridgesFile C:\\temp\\bridges.txt \
  -BootstrapCheck
```

Notes:

- default service-mode `torrc` output path: `%ProgramData%\\tor\\torrc.d\\synapsenet-obfs4.conf`
- `-ServiceStart` restarts/starts the selected service before readiness checks
- service mode validates readiness with SOCKS DDG probe

## Standalone validator

Use validator when Tor is already running and you only need runtime proof:

```powershell
pwsh -File tools/windows_tor_validate.ps1 \
  -SocksHost 127.0.0.1 \
  -SocksPort 9150 \
  -ControlHost 127.0.0.1 \
  -ControlPort 9151
```

Validator checks:

- SOCKS port reachable
- bootstrap path check (ControlPort if available)
- DDG probe through Tor SOCKS

## One-shot external 9150 e2e runner

Run helper bootstrap + validator and collect one evidence bundle:

```powershell
pwsh -File tools/windows_tor_external_9150_e2e.ps1 `
  -BridgesFile C:\temp\bridges_primary.txt,C:\temp\bridges_fallback.txt
```

Evidence output:

- `build/evidence/windows_tor_external_9150_e2e_<timestamp>/summary.json`
- `helper_*.log`, `validator.log`, copied Tor bootstrap logs

Notes:
- `-BridgesFile` supports multiple files (array) and uses provided order as fallback.
- The first bridge file that passes helper bootstrap is recorded in `summary.json`
  as `selectedBridgeFile`.

## Common failure hints

- `tor.exe not found`:
  set `-TorBin` or `-TorRoot`, or install Tor Browser / Tor Expert Bundle.
- `obfs4 transport binary not found`:
  set `-Obfs4ProxyBin` or install `obfs4proxy`/`lyrebird` in Tor runtime.
- `SOCKS port already in use` in user mode:
  stop conflicting Tor runtime or choose another SOCKS port.
- service mode probe timeout:
  verify service state, bridge validity, and outbound connectivity.
