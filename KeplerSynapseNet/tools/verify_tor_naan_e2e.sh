#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_PATH="${BIN_PATH:-$ROOT_DIR/build/synapsed}"
MODE="${MODE:-live-tor}"
TOR_PROXY="${TOR_PROXY:-${TOR_SOCKS_HOST:-127.0.0.1}:${TOR_SOCKS_PORT:-9050}}"
TOR_SOCKS_HOST="${TOR_SOCKS_HOST:-${TOR_PROXY%:*}}"
TOR_SOCKS_PORT="${TOR_SOCKS_PORT:-${TOR_PROXY##*:}}"
TOR_CONTROL_HOST="${TOR_CONTROL_HOST:-127.0.0.1}"
TOR_CONTROL_PORT="${TOR_CONTROL_PORT:-9051}"
TOR_PRECHECK_TIMEOUT="${TOR_PRECHECK_TIMEOUT:-3}"
DDG_URL="${DDG_URL:-https://html.duckduckgo.com/html/?q=synapsenet+tor+validation}"
AHMIA_ONION_URL="${AHMIA_ONION_URL:-http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/search/?q=synapsenet}"
OUTPUT_ROOT="${OUTPUT_ROOT:-$ROOT_DIR/build/evidence}"
RPC_PORT="${RPC_PORT:-18332}"
P2P_PORT="${P2P_PORT:-18444}"

usage() {
    cat <<'EOF'
Usage: tools/verify_tor_naan_e2e.sh [--mode live-tor|ci-offline] [--live-tor] [--ci-offline]

Modes:
  live-tor   Run full Tor network checks + naan.status checks.
  ci-offline Skip live Tor fetch checks, validate naan.status deterministically.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode)
            shift
            if [[ $# -eq 0 ]]; then
                echo "missing value for --mode" >&2
                exit 2
            fi
            MODE="$1"
            ;;
        --mode=*)
            MODE="${1#*=}"
            ;;
        --live-tor)
            MODE="live-tor"
            ;;
        --ci-offline)
            MODE="ci-offline"
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
    shift
done

if [[ "$MODE" != "live-tor" && "$MODE" != "ci-offline" ]]; then
    echo "invalid mode: $MODE" >&2
    usage
    exit 2
fi

if [[ "$TOR_PROXY" != *:* ]]; then
    TOR_SOCKS_HOST="$TOR_PROXY"
    TOR_SOCKS_PORT="${TOR_SOCKS_PORT:-9050}"
fi

RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_DIR="$OUTPUT_ROOT/tor_naan_e2e_$RUN_ID"
LOG_FILE="$RUN_DIR/run.log"
mkdir -p "$RUN_DIR"

FAILURES=0
TOR_BOOTSTRAP_OK=0
TOR_CONTROL_OK=0
TOR_NETWORK_CHECKS_SKIPPED=0
TOR_CHECK_OK=0
DDG_OK=0
AHMIA_OK=0
RPC_READY_OK=0
NAAN_STATUS_OK=0
NODE_PID=""

log() {
    local ts
    ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "[$ts] $*" | tee -a "$LOG_FILE"
}

record_failure() {
    FAILURES=$((FAILURES + 1))
    log "ERROR $*"
}

require_command() {
    local cmd="$1"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        log "ERROR missing command: $cmd"
        exit 1
    fi
}

tcp_probe() {
    local host="$1"
    local port="$2"
    local timeout="$3"
    python3 - "$host" "$port" "$timeout" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
timeout = float(sys.argv[3])
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(timeout)
try:
    s.connect((host, port))
except Exception:
    raise SystemExit(1)
finally:
    s.close()
raise SystemExit(0)
PY
}

tor_control_probe() {
    local host="$1"
    local port="$2"
    local timeout="$3"
    local out="$4"
    python3 - "$host" "$port" "$timeout" "$out" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
timeout = float(sys.argv[3])
out_path = sys.argv[4]
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(timeout)
try:
    s.connect((host, port))
    s.sendall(b"PROTOCOLINFO 1\r\nQUIT\r\n")
    data = s.recv(8192)
except Exception:
    raise SystemExit(1)
finally:
    s.close()
with open(out_path, "wb") as handle:
    handle.write(data)
if b"250-PROTOCOLINFO" not in data and b"250 PROTOCOLINFO" not in data:
    raise SystemExit(1)
raise SystemExit(0)
PY
}

tor_bootstrap_precheck() {
    local evidence="$RUN_DIR/tor_bootstrap_precheck.json"
    local socks_ok=0
    local control_ok=0
    local control_query_ok=0

    if tcp_probe "$TOR_SOCKS_HOST" "$TOR_SOCKS_PORT" "$TOR_PRECHECK_TIMEOUT"; then
        socks_ok=1
    fi

    if [[ "$socks_ok" -eq 0 ]]; then
        cat >"$evidence" <<EOF
{"socks_reachable":false,"control_reachable":false,"control_protocolinfo":false,"socks_host":"$TOR_SOCKS_HOST","socks_port":$TOR_SOCKS_PORT,"control_host":"$TOR_CONTROL_HOST","control_port":$TOR_CONTROL_PORT}
EOF
        record_failure "tor bootstrap precheck failed: SOCKS $TOR_SOCKS_HOST:$TOR_SOCKS_PORT unreachable"
        return 1
    fi

    if tcp_probe "$TOR_CONTROL_HOST" "$TOR_CONTROL_PORT" "$TOR_PRECHECK_TIMEOUT"; then
        control_ok=1
        if tor_control_probe "$TOR_CONTROL_HOST" "$TOR_CONTROL_PORT" "$TOR_PRECHECK_TIMEOUT" "$RUN_DIR/tor_control_protocolinfo.txt"; then
            control_query_ok=1
            TOR_CONTROL_OK=1
        else
            log "WARN tor control precheck reachable but PROTOCOLINFO query failed"
        fi
    else
        log "WARN tor control precheck skipped: $TOR_CONTROL_HOST:$TOR_CONTROL_PORT unreachable"
    fi

    TOR_BOOTSTRAP_OK=1
    cat >"$evidence" <<EOF
{"socks_reachable":true,"control_reachable":$( [[ "$control_ok" -eq 1 ]] && echo true || echo false ),"control_protocolinfo":$( [[ "$control_query_ok" -eq 1 ]] && echo true || echo false ),"socks_host":"$TOR_SOCKS_HOST","socks_port":$TOR_SOCKS_PORT,"control_host":"$TOR_CONTROL_HOST","control_port":$TOR_CONTROL_PORT}
EOF
    log "tor bootstrap precheck ok socks=$TOR_SOCKS_HOST:$TOR_SOCKS_PORT control_reachable=$control_ok control_protocolinfo=$control_query_ok"
    return 0
}

fetch_via_tor() {
    local name="$1"
    local url="$2"
    local body="$RUN_DIR/${name}.body"
    local headers="$RUN_DIR/${name}.headers"
    local meta="$RUN_DIR/${name}.meta"
    local curl_exit=0
    local info
    info="$(curl --silent --show-error --location \
        --socks5-hostname "$TOR_PROXY" \
        --connect-timeout 60 \
        --max-time 180 \
        --user-agent "SynapseNet-Tor-E2E/1.0" \
        --dump-header "$headers" \
        --output "$body" \
        --write-out $'http_code=%{http_code}\nsize_download=%{size_download}\nurl_effective=%{url_effective}\n' \
        "$url")" || curl_exit=$?
    printf "%s\n" "$info" >"$meta"
    if [[ "$curl_exit" -ne 0 ]]; then
        log "ERROR $name curl_exit=$curl_exit url=$url"
        return 1
    fi
    local http_code
    http_code="$(awk -F= '/^http_code=/{print $2}' "$meta" | tail -n1)"
    local body_size
    body_size="$(wc -c <"$body" | tr -d ' ')"
    local body_sha
    body_sha="$(shasum -a 256 "$body" | awk '{print $1}')"
    log "$name http_code=$http_code bytes=$body_size sha256=$body_sha"
    if [[ -z "$http_code" ]]; then
        log "ERROR $name missing http_code"
        return 1
    fi
    if [[ "$http_code" -lt 200 || "$http_code" -ge 400 ]]; then
        log "ERROR $name unexpected http_code=$http_code"
        return 1
    fi
    if [[ "$body_size" -eq 0 ]]; then
        log "ERROR $name empty body"
        return 1
    fi
    return 0
}

wait_for_rpc() {
    local out="$RUN_DIR/node_status_rpc.json"
    for _ in $(seq 1 60); do
        if curl --silent --show-error \
            --max-time 2 \
            --header "Content-Type: application/json" \
            --data '{"jsonrpc":"2.0","id":1,"method":"node.status","params":{}}' \
            "http://127.0.0.1:${RPC_PORT}/" >"$out"; then
            if python3 - "$out" <<'PY'
import json
import sys
path = sys.argv[1]
with open(path, "r", encoding="utf-8") as handle:
    doc = json.load(handle)
if doc.get("error"):
    raise SystemExit(1)
if "result" not in doc:
    raise SystemExit(1)
raise SystemExit(0)
PY
            then
                return 0
            fi
        fi
        sleep 1
    done
    return 1
}

cleanup() {
    if [[ -n "${NODE_PID}" ]] && kill -0 "$NODE_PID" >/dev/null 2>&1; then
        kill "$NODE_PID" >/dev/null 2>&1 || true
        wait "$NODE_PID" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

require_command curl
require_command python3
require_command shasum

if [[ ! -x "$BIN_PATH" ]]; then
    log "ERROR binary not found or not executable: $BIN_PATH"
    exit 1
fi

log "run_id=$RUN_ID"
log "run_dir=$RUN_DIR"
log "mode=$MODE"
log "tor_proxy=$TOR_PROXY"
log "tor_socks=$TOR_SOCKS_HOST:$TOR_SOCKS_PORT"
log "tor_control=$TOR_CONTROL_HOST:$TOR_CONTROL_PORT"
log "bin_path=$BIN_PATH"

if [[ "$MODE" == "live-tor" ]]; then
    if tor_bootstrap_precheck; then
        if fetch_via_tor "tor_check" "https://check.torproject.org/api/ip"; then
            if TOR_IP="$(python3 - "$RUN_DIR/tor_check.body" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as handle:
    doc = json.load(handle)
if not bool(doc.get("IsTor", False)):
    raise SystemExit(1)
ip = str(doc.get("IP", ""))
if not ip:
    raise SystemExit(1)
print(ip)
PY
)"; then
                TOR_CHECK_OK=1
                log "tor_check IsTor=true ip=$TOR_IP"
            else
                record_failure "tor_check response did not confirm IsTor=true"
            fi
        else
            record_failure "tor_check request failed"
        fi

        if fetch_via_tor "duckduckgo_clearnet" "$DDG_URL"; then
            if grep -Eiq 'duckduckgo|<!doctype html|<html' "$RUN_DIR/duckduckgo_clearnet.body"; then
                DDG_OK=1
            else
                record_failure "duckduckgo response does not look like HTML/search output"
            fi
        else
            record_failure "duckduckgo request failed"
        fi

        if fetch_via_tor "ahmia_onion" "$AHMIA_ONION_URL"; then
            if grep -Eiq 'ahmia|<!doctype html|<html' "$RUN_DIR/ahmia_onion.body"; then
                AHMIA_OK=1
            else
                record_failure "ahmia onion response does not look like HTML/search output"
            fi
        else
            record_failure "ahmia onion request failed"
        fi
    else
        TOR_NETWORK_CHECKS_SKIPPED=1
        log "tor network checks skipped due to bootstrap precheck failure"
    fi
else
    TOR_NETWORK_CHECKS_SKIPPED=1
    log "mode=ci-offline, skipping live tor fetch checks"
fi

DATADIR="$RUN_DIR/node_data"
NODE_LOG="$RUN_DIR/node_runtime.log"
mkdir -p "$DATADIR"
"$BIN_PATH" --regtest --datadir "$DATADIR" --rpcport "$RPC_PORT" --port "$P2P_PORT" >"$NODE_LOG" 2>&1 &
NODE_PID=$!

if wait_for_rpc; then
    RPC_READY_OK=1
    log "rpc_ready port=$RPC_PORT"
else
    record_failure "node RPC did not become ready on port $RPC_PORT"
fi

if [[ "$RPC_READY_OK" -eq 1 ]]; then
    NAAN_RPC="$RUN_DIR/naan_status_rpc.json"
    if curl --silent --show-error \
        --max-time 6 \
        --header "Content-Type: application/json" \
        --data '{"jsonrpc":"2.0","id":1,"method":"naan.status","params":{}}' \
        "http://127.0.0.1:${RPC_PORT}/" >"$NAAN_RPC"; then
        if python3 - "$NAAN_RPC" "$RUN_DIR/naan_status_required_fields.json" <<'PY'
import json
import sys

rpc_path = sys.argv[1]
out_path = sys.argv[2]

with open(rpc_path, "r", encoding="utf-8") as handle:
    rpc = json.load(handle)

if rpc.get("error"):
    raise SystemExit(1)

result = rpc.get("result")
if not isinstance(result, dict):
    raise SystemExit(1)

required = [
    ("activeEngines",),
    ("torRoute",),
    ("policyBlocks",),
    ("fetchCounts",),
    ("connectorStatus", "activeEngines"),
    ("connectorStatus", "torRoute"),
    ("connectorStatus", "policyBlocks"),
    ("connectorStatus", "fetchCounts"),
]

missing = []
for path in required:
    cur = result
    ok = True
    for key in path:
        if not isinstance(cur, dict) or key not in cur:
            ok = False
            break
        cur = cur[key]
    if not ok:
        missing.append(".".join(path))

if missing:
    raise SystemExit(1)

summary = {
    "routeMode": result.get("routeMode"),
    "torRoute": result.get("torRoute"),
    "activeEngines": result.get("activeEngines"),
    "policyBlocks": result.get("policyBlocks"),
    "fetchCounts": result.get("fetchCounts"),
}

with open(out_path, "w", encoding="utf-8") as handle:
    json.dump(summary, handle, indent=2, sort_keys=True)
PY
        then
            NAAN_STATUS_OK=1
            log "naan.status required fields are present"
        else
            record_failure "naan.status missing required fields or returned an error"
        fi
    else
        record_failure "naan.status RPC call failed"
    fi
fi

SUMMARY_FILE="$RUN_DIR/summary.txt"
{
    echo "run_id=$RUN_ID"
    echo "run_dir=$RUN_DIR"
    echo "mode=$MODE"
    echo "tor_proxy=$TOR_PROXY"
    echo "tor_socks_host=$TOR_SOCKS_HOST"
    echo "tor_socks_port=$TOR_SOCKS_PORT"
    echo "tor_control_host=$TOR_CONTROL_HOST"
    echo "tor_control_port=$TOR_CONTROL_PORT"
    echo "tor_bootstrap_ok=$TOR_BOOTSTRAP_OK"
    echo "tor_control_ok=$TOR_CONTROL_OK"
    echo "tor_network_checks_skipped=$TOR_NETWORK_CHECKS_SKIPPED"
    echo "tor_check_ok=$TOR_CHECK_OK"
    echo "duckduckgo_ok=$DDG_OK"
    echo "ahmia_ok=$AHMIA_OK"
    echo "rpc_ready_ok=$RPC_READY_OK"
    echo "naan_status_ok=$NAAN_STATUS_OK"
    echo "failures=$FAILURES"
} >"$SUMMARY_FILE"

if [[ "$FAILURES" -eq 0 ]]; then
    log "PASS all checks completed"
else
    log "FAIL failures=$FAILURES"
fi

log "evidence ready: $RUN_DIR"
printf "%s\n" "$RUN_DIR"

if [[ "$FAILURES" -eq 0 ]]; then
    exit 0
fi
exit 1
