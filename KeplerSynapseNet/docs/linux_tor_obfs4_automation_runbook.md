# Linux Tor obfs4 Automation Runbook

This runbook describes the Linux helper:

- `tools/linux_tor_obfs4_smoke.sh`
- `tools/linux_tor_validate.sh`

It is intended for SynapseNet external Tor validation and supports both:

- local user-process mode
- systemd service mode

## What the helper now validates

1. Distro/package layout detection for:
   - `tor`
   - `obfs4proxy`
   - `snowflake-client` (optional)
2. `torrc` generation and `tor --verify-config`
3. Runtime ownership checks in user mode (`DataDirectory` owner must match current UID)
4. Safe process cleanup using PID file + commandline fingerprint
5. Bootstrap/web readiness verification through SOCKS
6. Runtime external/managed mode validation through `node.status` with evidence snapshots (`linux_tor_validate.sh`)

## User mode example

```bash
nano /tmp/bridges.txt
tools/linux_tor_obfs4_smoke.sh \
  --mode user \
  --bridges-file /tmp/bridges.txt \
  --bootstrap-check \
  --write-synapsenet-snippet /tmp/synapsenet_external_9150.conf
```

Notes:

- Do not commit bridge lines into git. Treat them as sensitive and rotate them as needed.
- You can also paste bridges in the SynapseNet TUI startup wizard (bridge-mode step): press `[P]` to save them to `<DATA_DIR>/tor/bridges.obfs4.txt` and use that file with the helper.
- default user-mode torrc path: `/tmp/tor-obfs4-linux.conf`
- default user-mode `DataDirectory`: `/tmp/synapsenet_tor_obfs4_linux`

## systemd mode example

```bash
tools/linux_tor_obfs4_smoke.sh \
  --mode systemd \
  --systemd-unit tor \
  --systemd-start \
  --bridges-file /tmp/bridges.txt \
  --bootstrap-check
```

Notes:

- default systemd-mode torrc path: `/etc/tor/torrc.d/synapsenet-obfs4.conf`
- `--systemd-start` restarts/starts the configured unit before readiness checks
- readiness is validated by SOCKS web probe, not by blind port-open checks

## Runtime validator example

```bash
tools/linux_tor_validate.sh
```

Useful flags:

```bash
tools/linux_tor_validate.sh --skip-external
tools/linux_tor_validate.sh --skip-managed
tools/linux_tor_validate.sh --no-require-external-web-ready
```

The validator writes evidence to:

- `build/evidence/linux_tor_validate_<timestamp>/`

## One-shot external 9150 e2e runner

Run helper bootstrap + external validator in one command:

```bash
tools/linux_tor_external_9150_e2e.sh \
  --bridges-file /tmp/bridges_primary.txt \
  --bridges-file /tmp/bridges_fallback.txt
```

Evidence output:

- `build/evidence/linux_tor_external_9150_e2e_<timestamp>/summary.json`
- `helper_*.log`, `validator.log`, copied Tor bootstrap logs

Notes:
- `--bridges-file` is repeatable and processed in provided order.
- The first bridge file that passes helper bootstrap is recorded in `summary.json`
  as `selectedBridgeFile`.

## Safety behavior

- The helper does not blindly kill Tor processes.
- In user mode, cleanup only terminates a PID when commandline fingerprint matches:
  - generated `-f <torrc>`
  - selected `--DataDirectory <path>`

If fingerprint does not match, process termination is skipped.

## Common failure hints

- `tor binary not found`:
  install Tor from distro packages.
- `obfs4proxy binary not found`:
  install `obfs4proxy` package.
- `data dir owner mismatch`:
  use a per-user runtime dir or fix ownership.
- `systemd unit is not active`:
  verify unit name (`--systemd-unit`) and service state.
