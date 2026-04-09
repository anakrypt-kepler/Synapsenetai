#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HELPER="${HELPER:-$ROOT_DIR/tools/linux_tor_obfs4_smoke.sh}"
VALIDATOR="${VALIDATOR:-$ROOT_DIR/tools/linux_tor_validate.sh}"
OUTPUT_ROOT="${OUTPUT_ROOT:-$ROOT_DIR/build/evidence}"
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_DIR="$OUTPUT_ROOT/linux_tor_external_9150_e2e_$RUN_ID"

declare -a BRIDGES_FILES=()
SOCKS_HOST="${SOCKS_HOST:-127.0.0.1}"
SOCKS_PORT="${SOCKS_PORT:-9150}"
CONTROL_PORT="${CONTROL_PORT:-9151}"
DATA_DIR="${DATA_DIR:-/tmp/synapsenet_tor_obfs4_linux}"
BOOTSTRAP_TIMEOUT_SEC="${BOOTSTRAP_TIMEOUT_SEC:-360}"
PROBE_MAX_TIME="${PROBE_MAX_TIME:-30}"
STATUS_WAIT_SEC="${STATUS_WAIT_SEC:-120}"
KEEP_EXTERNAL_RUNNING=0
REQUIRE_EXTERNAL_WEB_READY=1

HELPER_OK=0
VALIDATOR_OK=0
VALIDATOR_RUN_DIR=""
SELECTED_BRIDGES_FILE=""
HELPER_LOG=""
declare -a HELPER_LOGS=()

usage() {
    cat <<'EOF'
Usage: tools/linux_tor_external_9150_e2e.sh --bridges-file PATH [--bridges-file PATH2 ...] [options]

End-to-end Linux external-9150 validation:
  1) run linux_tor_obfs4_smoke bootstrap on 9150
  2) run linux_tor_validate external-mode checks
  3) write unified evidence bundle under build/evidence

Options:
  --bridges-file PATH             obfs4 bridges file (required, repeatable; fallback order)
  --output-root PATH              evidence root (default: build/evidence)
  --socks-host HOST               SOCKS host (default: 127.0.0.1)
  --socks-port PORT               SOCKS port (default: 9150)
  --control-port PORT             ControlPort (default: 9151)
  --data-dir PATH                 tor DataDirectory (default: /tmp/synapsenet_tor_obfs4_linux)
  --bootstrap-timeout-sec N       helper bootstrap timeout (default: 360)
  --probe-max-time N              validator SOCKS probe max-time (default: 30)
  --status-wait-sec N             validator status wait (default: 120)
  --keep-external-running         keep external Tor running after validation
  --no-require-external-web-ready do not fail on torReadyForWeb=false
  --help                          show help
EOF
}

log() {
    printf '[%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

ensure_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

require_linux() {
    local os
    os="$(uname -s 2>/dev/null || true)"
    [[ "$os" == "Linux" ]] || die "this script is Linux-only (detected: ${os:-unknown})"
}

stop_external_tor_if_needed() {
    if [[ "$KEEP_EXTERNAL_RUNNING" == "1" ]]; then
        return 0
    fi
    local pid_file="$DATA_DIR/tor.pid"
    if [[ ! -f "$pid_file" ]]; then
        return 0
    fi
    local pid
    pid="$(tr -d '[:space:]' <"$pid_file" || true)"
    if [[ "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" >/dev/null 2>&1; then
        kill -TERM "$pid" >/dev/null 2>&1 || true
        for _ in $(seq 1 30); do
            if ! kill -0 "$pid" >/dev/null 2>&1; then
                break
            fi
            sleep 1
        done
    fi
}

copy_bootstrap_logs() {
    if [[ ! -d "$DATA_DIR" ]]; then
        return 0
    fi
    local f
    shopt -s nullglob
    for f in "$DATA_DIR"/tor_bootstrap*.log "$DATA_DIR"/tor.pid; do
        cp "$f" "$RUN_DIR/" 2>/dev/null || true
    done
    shopt -u nullglob
}

sanitize_name() {
    local raw="$1"
    local safe
    safe="$(printf '%s' "$raw" | tr -c '[:alnum:]._-' '_')"
    safe="${safe:0:64}"
    [[ -n "$safe" ]] || safe="bridges"
    printf '%s\n' "$safe"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bridges-file)
            shift
            [[ $# -gt 0 ]] || die "missing value for --bridges-file"
            BRIDGES_FILES+=("$1")
            ;;
        --output-root)
            shift
            [[ $# -gt 0 ]] || die "missing value for --output-root"
            OUTPUT_ROOT="$1"
            RUN_DIR="$OUTPUT_ROOT/linux_tor_external_9150_e2e_$RUN_ID"
            ;;
        --socks-host)
            shift
            [[ $# -gt 0 ]] || die "missing value for --socks-host"
            SOCKS_HOST="$1"
            ;;
        --socks-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --socks-port"
            SOCKS_PORT="$1"
            ;;
        --control-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --control-port"
            CONTROL_PORT="$1"
            ;;
        --data-dir)
            shift
            [[ $# -gt 0 ]] || die "missing value for --data-dir"
            DATA_DIR="$1"
            ;;
        --bootstrap-timeout-sec)
            shift
            [[ $# -gt 0 ]] || die "missing value for --bootstrap-timeout-sec"
            BOOTSTRAP_TIMEOUT_SEC="$1"
            ;;
        --probe-max-time)
            shift
            [[ $# -gt 0 ]] || die "missing value for --probe-max-time"
            PROBE_MAX_TIME="$1"
            ;;
        --status-wait-sec)
            shift
            [[ $# -gt 0 ]] || die "missing value for --status-wait-sec"
            STATUS_WAIT_SEC="$1"
            ;;
        --keep-external-running)
            KEEP_EXTERNAL_RUNNING=1
            ;;
        --no-require-external-web-ready)
            REQUIRE_EXTERNAL_WEB_READY=0
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
    shift
done

require_linux
ensure_command bash
ensure_command python3
ensure_command curl

[[ "${#BRIDGES_FILES[@]}" -gt 0 ]] || die "--bridges-file is required"
for _bridges_file in "${BRIDGES_FILES[@]}"; do
    [[ -f "$_bridges_file" ]] || die "bridges file not found: $_bridges_file"
done
[[ -x "$HELPER" ]] || die "helper not found/executable: $HELPER"
[[ -x "$VALIDATOR" ]] || die "validator not found/executable: $VALIDATOR"

mkdir -p "$RUN_DIR"
trap stop_external_tor_if_needed EXIT

log "Linux external-9150 e2e start"
log "run_dir=$RUN_DIR"
for _bridges_file in "${BRIDGES_FILES[@]}"; do
    log "bridges_file=$_bridges_file"
done

VALIDATOR_LOG="$RUN_DIR/validator.log"

for bridges_file in "${BRIDGES_FILES[@]}"; do
    safe_name="$(sanitize_name "$bridges_file")"
    HELPER_LOG="$RUN_DIR/helper_${#HELPER_LOGS[@]}_${safe_name}.log"
    HELPER_LOGS+=("$HELPER_LOG")

    helper_cmd=(
        "$HELPER"
        --mode user
        --bridges-file "$bridges_file"
        --socks-host "$SOCKS_HOST"
        --socks-port "$SOCKS_PORT"
        --control-port "$CONTROL_PORT"
        --data-dir "$DATA_DIR"
        --bootstrap-check
        --bootstrap-timeout-sec "$BOOTSTRAP_TIMEOUT_SEC"
        --keep-running
        --out "$RUN_DIR/tor-obfs4.conf"
        --write-synapsenet-snippet "$RUN_DIR/synapsenet_external.conf"
    )

    if "${helper_cmd[@]}" >"$HELPER_LOG" 2>&1; then
        HELPER_OK=1
        SELECTED_BRIDGES_FILE="$bridges_file"
        log "helper bootstrap succeeded with bridges_file=$bridges_file"
        break
    fi
    log "helper bootstrap failed with bridges_file=$bridges_file (next fallback if available)"
done

copy_bootstrap_logs

if [[ "$HELPER_OK" == "1" ]]; then
    validator_cmd=(
        "$VALIDATOR"
        --skip-managed
        --external-socks-host "$SOCKS_HOST"
        --external-socks-port "$SOCKS_PORT"
        --external-control-port "$CONTROL_PORT"
        --status-wait-sec "$STATUS_WAIT_SEC"
        --probe-max-time "$PROBE_MAX_TIME"
    )
    if [[ "$REQUIRE_EXTERNAL_WEB_READY" != "1" ]]; then
        validator_cmd+=(--no-require-external-web-ready)
    fi
    if "${validator_cmd[@]}" >"$VALIDATOR_LOG" 2>&1; then
        VALIDATOR_OK=1
    fi
    VALIDATOR_RUN_DIR="$(awk -F= '/run_dir=/{print $2; exit}' "$VALIDATOR_LOG" || true)"
fi

SUMMARY_FILE="$RUN_DIR/summary.json"
HELPER_LOGS_FILE="$RUN_DIR/helper_logs.txt"
BRIDGES_FILES_FILE="$RUN_DIR/bridges_files.txt"
printf '%s\n' "${HELPER_LOGS[@]}" >"$HELPER_LOGS_FILE"
printf '%s\n' "${BRIDGES_FILES[@]}" >"$BRIDGES_FILES_FILE"

python3 - "$SUMMARY_FILE" "$RUN_DIR" "$HELPER_LOG" "$VALIDATOR_LOG" "$VALIDATOR_RUN_DIR" "$HELPER_OK" "$VALIDATOR_OK" "$KEEP_EXTERNAL_RUNNING" "$SELECTED_BRIDGES_FILE" "$HELPER_LOGS_FILE" "$BRIDGES_FILES_FILE" <<'PY'
import json
import sys
summary_path, run_dir, helper_log, validator_log, validator_run_dir, helper_ok, validator_ok, keep_running, selected_bridges_file, helper_logs_file, bridges_files_file = sys.argv[1:]
with open(helper_logs_file, "r", encoding="utf-8") as f:
    helper_logs = [line.strip() for line in f if line.strip()]
with open(bridges_files_file, "r", encoding="utf-8") as f:
    bridges_files = [line.strip() for line in f if line.strip()]
doc = {
    "ok": helper_ok == "1" and validator_ok == "1",
    "helperOk": helper_ok == "1",
    "validatorOk": validator_ok == "1",
    "runDir": run_dir,
    "helperLog": helper_log,
    "helperLogs": helper_logs,
    "validatorLog": validator_log,
    "validatorRunDir": validator_run_dir,
    "bridgeFiles": bridges_files,
    "selectedBridgeFile": selected_bridges_file,
    "keepExternalRunning": keep_running == "1",
}
with open(summary_path, "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2)
    f.write("\n")
PY

if [[ "$HELPER_OK" == "1" && "$VALIDATOR_OK" == "1" ]]; then
    log "e2e success (summary: $SUMMARY_FILE)"
    exit 0
fi

log "e2e failed (summary: $SUMMARY_FILE)"
exit 1
