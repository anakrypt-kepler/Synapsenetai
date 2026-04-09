#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_PATH="${BIN_PATH:-$ROOT_DIR/build/synapsed}"
OUTPUT_ROOT="${OUTPUT_ROOT:-$ROOT_DIR/build/evidence}"
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_DIR="$OUTPUT_ROOT/linux_tor_validate_$RUN_ID"

EXTERNAL_SOCKS_HOST="${EXTERNAL_SOCKS_HOST:-127.0.0.1}"
EXTERNAL_SOCKS_PORT="${EXTERNAL_SOCKS_PORT:-9150}"
EXTERNAL_CONTROL_PORT="${EXTERNAL_CONTROL_PORT:-9151}"
EXTERNAL_RPC_PORT="${EXTERNAL_RPC_PORT:-18432}"
EXTERNAL_P2P_PORT="${EXTERNAL_P2P_PORT:-19432}"

MANAGED_SOCKS_HOST="${MANAGED_SOCKS_HOST:-127.0.0.1}"
MANAGED_SOCKS_PORT="${MANAGED_SOCKS_PORT:-19050}"
MANAGED_CONTROL_PORT="${MANAGED_CONTROL_PORT:-19051}"
MANAGED_RPC_PORT="${MANAGED_RPC_PORT:-18442}"
MANAGED_P2P_PORT="${MANAGED_P2P_PORT:-19442}"

STATUS_WAIT_SEC="${STATUS_WAIT_SEC:-120}"
PROBE_MAX_TIME="${PROBE_MAX_TIME:-20}"
REQUIRE_EXTERNAL_WEB_READY=1
SKIP_EXTERNAL=0
SKIP_MANAGED=0

EXT_NODE_PID=0
MNG_NODE_PID=0
FAILURES=0

usage() {
    cat <<'EOF'
Usage: tools/linux_tor_validate.sh [options]

Validate SynapseNet Tor runtime behavior on Linux:
  1) external mode against an already running external Tor SOCKS endpoint
  2) managed mode startup/shutdown safety and diagnostics

Options:
  --bin PATH                       synapsed binary path
  --output-root PATH               output evidence root
  --external-socks-host HOST       external Tor SOCKS host (default: 127.0.0.1)
  --external-socks-port PORT       external Tor SOCKS port (default: 9150)
  --external-control-port PORT     external Tor ControlPort (default: 9151)
  --external-rpc-port PORT         RPC port for external-mode node
  --external-p2p-port PORT         P2P port for external-mode node
  --managed-socks-host HOST        managed Tor SOCKS host (default: 127.0.0.1)
  --managed-socks-port PORT        managed Tor SOCKS port (default: 19050)
  --managed-control-port PORT      managed Tor ControlPort (default: 19051)
  --managed-rpc-port PORT          RPC port for managed-mode node
  --managed-p2p-port PORT          P2P port for managed-mode node
  --status-wait-sec N              seconds to wait for node.status readiness
  --probe-max-time N               curl max-time for external SOCKS probe
  --no-require-external-web-ready  do not fail external mode when torReadyForWeb=false
  --skip-external                  skip external-mode validation
  --skip-managed                   skip managed-mode validation
  --help                           show help
EOF
}

log() {
    printf '[%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

record_failure() {
    FAILURES=$((FAILURES + 1))
    log "FAIL: $*"
}

require_linux() {
    local os
    os="$(uname -s 2>/dev/null || true)"
    [[ "$os" == "Linux" ]] || die "this validator is Linux-only (detected: ${os:-unknown})"
}

ensure_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

node_alive() {
    local pid="$1"
    [[ "$pid" -gt 0 ]] || return 1
    kill -0 "$pid" >/dev/null 2>&1
}

stop_node_pid() {
    local pid="$1"
    [[ "$pid" -gt 0 ]] || return 0
    if ! node_alive "$pid"; then
        return 0
    fi
    kill -TERM "$pid" >/dev/null 2>&1 || true
    for _ in $(seq 1 40); do
        if ! node_alive "$pid"; then
            return 0
        fi
        sleep 0.25
    done
    kill -KILL "$pid" >/dev/null 2>&1 || true
}

cleanup() {
    stop_node_pid "$EXT_NODE_PID"
    stop_node_pid "$MNG_NODE_PID"
}

wait_node_status() {
    local port="$1"
    local out_file="$2"
    local log_file="$3"
    for _ in $(seq 1 "$STATUS_WAIT_SEC"); do
        if curl --silent --show-error \
            --max-time 5 \
            --header "Content-Type: application/json" \
            --data '{"jsonrpc":"2.0","id":1,"method":"node.status","params":{}}' \
            "http://127.0.0.1:${port}/" >"$out_file" 2>/dev/null; then
            if python3 - "$out_file" <<'PY'
import json
import sys
path = sys.argv[1]
with open(path, "r", encoding="utf-8") as f:
    doc = json.load(f)
if not isinstance(doc, dict):
    raise SystemExit(1)
if "result" not in doc:
    raise SystemExit(1)
if doc.get("error"):
    raise SystemExit(1)
raise SystemExit(0)
PY
            then
                return 0
            fi
        fi
        if [[ -f "$log_file" ]] && grep -q "Failed to initialize node" "$log_file"; then
            return 1
        fi
        sleep 1
    done
    return 1
}

write_node_config() {
    local cfg_path="$1"
    local mode="$2"
    local socks_host="$3"
    local socks_port="$4"
    local control_port="$5"
    mkdir -p "$(dirname "$cfg_path")"
    cat >"$cfg_path" <<EOF
agent.tor.required=true
agent.tor.mode=$mode
agent.routing.allow_clearnet_fallback=false
agent.routing.allow_p2p_clearnet_fallback=false
tor.socks.host=$socks_host
tor.socks.port=$socks_port
tor.control.port=$control_port
agent.tor.web_probe_url=https://duckduckgo.com/robots.txt
agent.tor.web_probe_timeout_seconds=6
agent.tor.web_probe_interval_seconds=5
EOF
}

start_node() {
    local datadir="$1"
    local cfg_path="$2"
    local rpc_port="$3"
    local p2p_port="$4"
    local log_path="$5"
    local lock_file="$datadir/synapsed.lock"
    local launcher_pid=0

    mkdir -p "$datadir"
    "$BIN_PATH" \
        -d \
        --dev \
        --regtest \
        --amnesia \
        -D "$datadir" \
        -c "$cfg_path" \
        -r "$rpc_port" \
        -p "$p2p_port" \
        --loglevel info >"$log_path" 2>&1 &
    launcher_pid=$!

    for _ in $(seq 1 120); do
        if [[ -f "$lock_file" ]]; then
            local lock_pid
            lock_pid="$(head -n 1 "$lock_file" | tr -d '[:space:]' || true)"
            if [[ "$lock_pid" =~ ^[0-9]+$ ]] && kill -0 "$lock_pid" >/dev/null 2>&1; then
                echo "$lock_pid"
                return 0
            fi
        fi
        if ! kill -0 "$launcher_pid" >/dev/null 2>&1; then
            break
        fi
        sleep 0.25
    done

    echo "$launcher_pid"
}

extract_status_field() {
    local status_file="$1"
    local key="$2"
    python3 - "$status_file" "$key" <<'PY'
import json
import sys
path, key = sys.argv[1], sys.argv[2]
with open(path, "r", encoding="utf-8") as f:
    doc = json.load(f)
result = doc.get("result", {})
value = result.get(key)
if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("")
else:
    print(value)
PY
}

snapshot_result_block() {
    local name="$1"
    local status_file="$2"
    local out_file="$3"
    python3 - "$name" "$status_file" "$out_file" <<'PY'
import json
import sys
name, status_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
with open(status_path, "r", encoding="utf-8") as f:
    doc = json.load(f)
result = doc.get("result", {})
out = {
    "name": name,
    "torRuntimeMode": result.get("torRuntimeMode"),
    "torSocksHost": result.get("torSocksHost"),
    "torSocksPort": result.get("torSocksPort"),
    "torControlPort": result.get("torControlPort"),
    "torSocksReachable": result.get("torSocksReachable"),
    "torReadyForWeb": result.get("torReadyForWeb"),
    "torReadyForOnionService": result.get("torReadyForOnionService"),
    "torOnionServiceState": result.get("torOnionServiceState"),
    "torManaged": result.get("torManaged"),
    "torManagedPid": result.get("torManagedPid"),
    "torBootstrapState": result.get("torBootstrapState"),
    "torBootstrapReasonCode": result.get("torBootstrapReasonCode"),
}
with open(out_path, "w", encoding="utf-8") as f:
    json.dump(out, f, indent=2)
    f.write("\n")
PY
}

validate_external_mode() {
    local datadir="$RUN_DIR/external_node"
    local cfg="$datadir/synapsenet.conf"
    local status="$RUN_DIR/external_node_status.json"
    local summary="$RUN_DIR/external_validation.json"
    local log_path="$RUN_DIR/external_node.log"

    write_node_config "$cfg" "external" "$EXTERNAL_SOCKS_HOST" "$EXTERNAL_SOCKS_PORT" "$EXTERNAL_CONTROL_PORT"
    EXT_NODE_PID="$(start_node "$datadir" "$cfg" "$EXTERNAL_RPC_PORT" "$EXTERNAL_P2P_PORT" "$log_path")"
    log "external node pid=$EXT_NODE_PID rpc=$EXTERNAL_RPC_PORT p2p=$EXTERNAL_P2P_PORT socks=${EXTERNAL_SOCKS_HOST}:${EXTERNAL_SOCKS_PORT}"

    if ! wait_node_status "$EXTERNAL_RPC_PORT" "$status" "$log_path"; then
        record_failure "external mode: node.status did not become ready"
        return
    fi
    snapshot_result_block "external" "$status" "$summary"

    local mode socks_port web_ready socks_reachable managed_pid
    mode="$(extract_status_field "$status" "torRuntimeMode")"
    socks_port="$(extract_status_field "$status" "torSocksPort")"
    web_ready="$(extract_status_field "$status" "torReadyForWeb")"
    socks_reachable="$(extract_status_field "$status" "torSocksReachable")"
    managed_pid="$(extract_status_field "$status" "torManagedPid")"

    [[ "$mode" == "external" ]] || record_failure "external mode: expected torRuntimeMode=external got '$mode'"
    [[ "$socks_port" == "$EXTERNAL_SOCKS_PORT" ]] || record_failure "external mode: expected torSocksPort=$EXTERNAL_SOCKS_PORT got '$socks_port'"
    [[ "$socks_reachable" == "true" ]] || record_failure "external mode: expected torSocksReachable=true got '$socks_reachable'"
    if [[ "$managed_pid" != "0" && -n "$managed_pid" ]]; then
        record_failure "external mode: expected torManagedPid=0 got '$managed_pid'"
    fi
    if [[ "$REQUIRE_EXTERNAL_WEB_READY" == "1" && "$web_ready" != "true" ]]; then
        record_failure "external mode: expected torReadyForWeb=true got '$web_ready'"
    fi

    local probe_file="$RUN_DIR/external_socks_probe.json"
    if ! curl --silent --show-error \
        --socks5-hostname "${EXTERNAL_SOCKS_HOST}:${EXTERNAL_SOCKS_PORT}" \
        --connect-timeout 15 \
        --max-time "$PROBE_MAX_TIME" \
        "https://check.torproject.org/api/ip" >"$probe_file"; then
        record_failure "external mode: SOCKS probe failed via ${EXTERNAL_SOCKS_HOST}:${EXTERNAL_SOCKS_PORT}"
    fi
}

validate_managed_mode() {
    local datadir="$RUN_DIR/managed_node"
    local cfg="$datadir/synapsenet.conf"
    local status="$RUN_DIR/managed_node_status.json"
    local summary="$RUN_DIR/managed_validation.json"
    local log_path="$RUN_DIR/managed_node.log"

    write_node_config "$cfg" "managed" "$MANAGED_SOCKS_HOST" "$MANAGED_SOCKS_PORT" "$MANAGED_CONTROL_PORT"
    MNG_NODE_PID="$(start_node "$datadir" "$cfg" "$MANAGED_RPC_PORT" "$MANAGED_P2P_PORT" "$log_path")"
    log "managed node pid=$MNG_NODE_PID rpc=$MANAGED_RPC_PORT p2p=$MANAGED_P2P_PORT socks=${MANAGED_SOCKS_HOST}:${MANAGED_SOCKS_PORT}"

    if ! wait_node_status "$MANAGED_RPC_PORT" "$status" "$log_path"; then
        record_failure "managed mode: node.status did not become ready"
        return
    fi
    snapshot_result_block "managed" "$status" "$summary"

    local mode managed_flag managed_pid socks_port
    mode="$(extract_status_field "$status" "torRuntimeMode")"
    managed_flag="$(extract_status_field "$status" "torManaged")"
    managed_pid="$(extract_status_field "$status" "torManagedPid")"
    socks_port="$(extract_status_field "$status" "torSocksPort")"

    [[ "$mode" == "managed" ]] || record_failure "managed mode: expected torRuntimeMode=managed got '$mode'"
    [[ "$socks_port" == "$MANAGED_SOCKS_PORT" ]] || record_failure "managed mode: expected torSocksPort=$MANAGED_SOCKS_PORT got '$socks_port'"
    [[ "$managed_flag" == "true" ]] || record_failure "managed mode: expected torManaged=true got '$managed_flag'"
    if [[ -z "$managed_pid" || "$managed_pid" == "0" ]]; then
        record_failure "managed mode: expected non-zero torManagedPid got '$managed_pid'"
    fi

    stop_node_pid "$MNG_NODE_PID"
    MNG_NODE_PID=0

    if [[ -n "$managed_pid" && "$managed_pid" != "0" ]]; then
        if kill -0 "$managed_pid" >/dev/null 2>&1; then
            record_failure "managed mode: managed Tor pid still alive after node shutdown ($managed_pid)"
        fi
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bin)
            shift
            [[ $# -gt 0 ]] || die "missing value for --bin"
            BIN_PATH="$1"
            ;;
        --output-root)
            shift
            [[ $# -gt 0 ]] || die "missing value for --output-root"
            OUTPUT_ROOT="$1"
            ;;
        --external-socks-host)
            shift
            [[ $# -gt 0 ]] || die "missing value for --external-socks-host"
            EXTERNAL_SOCKS_HOST="$1"
            ;;
        --external-socks-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --external-socks-port"
            EXTERNAL_SOCKS_PORT="$1"
            ;;
        --external-control-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --external-control-port"
            EXTERNAL_CONTROL_PORT="$1"
            ;;
        --external-rpc-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --external-rpc-port"
            EXTERNAL_RPC_PORT="$1"
            ;;
        --external-p2p-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --external-p2p-port"
            EXTERNAL_P2P_PORT="$1"
            ;;
        --managed-socks-host)
            shift
            [[ $# -gt 0 ]] || die "missing value for --managed-socks-host"
            MANAGED_SOCKS_HOST="$1"
            ;;
        --managed-socks-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --managed-socks-port"
            MANAGED_SOCKS_PORT="$1"
            ;;
        --managed-control-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --managed-control-port"
            MANAGED_CONTROL_PORT="$1"
            ;;
        --managed-rpc-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --managed-rpc-port"
            MANAGED_RPC_PORT="$1"
            ;;
        --managed-p2p-port)
            shift
            [[ $# -gt 0 ]] || die "missing value for --managed-p2p-port"
            MANAGED_P2P_PORT="$1"
            ;;
        --status-wait-sec)
            shift
            [[ $# -gt 0 ]] || die "missing value for --status-wait-sec"
            STATUS_WAIT_SEC="$1"
            ;;
        --probe-max-time)
            shift
            [[ $# -gt 0 ]] || die "missing value for --probe-max-time"
            PROBE_MAX_TIME="$1"
            ;;
        --no-require-external-web-ready)
            REQUIRE_EXTERNAL_WEB_READY=0
            ;;
        --skip-external)
            SKIP_EXTERNAL=1
            ;;
        --skip-managed)
            SKIP_MANAGED=1
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
ensure_command curl
ensure_command python3

[[ -x "$BIN_PATH" ]] || die "synapsed binary not found or not executable: $BIN_PATH"

mkdir -p "$RUN_DIR"
trap cleanup EXIT

log "Linux Tor validator start"
log "run_dir=$RUN_DIR"
log "synapsed=$BIN_PATH"

if [[ "$SKIP_EXTERNAL" != "1" ]]; then
    validate_external_mode
    stop_node_pid "$EXT_NODE_PID"
    EXT_NODE_PID=0
fi

if [[ "$SKIP_MANAGED" != "1" ]]; then
    validate_managed_mode
fi

SUMMARY_FILE="$RUN_DIR/summary.json"
cat >"$SUMMARY_FILE" <<EOF
{
  "ok": $([[ "$FAILURES" -eq 0 ]] && echo true || echo false),
  "failures": $FAILURES,
  "runDir": "$RUN_DIR",
  "skipExternal": $([[ "$SKIP_EXTERNAL" == "1" ]] && echo true || echo false),
  "skipManaged": $([[ "$SKIP_MANAGED" == "1" ]] && echo true || echo false),
  "requireExternalWebReady": $([[ "$REQUIRE_EXTERNAL_WEB_READY" == "1" ]] && echo true || echo false)
}
EOF

if [[ "$FAILURES" -eq 0 ]]; then
    log "validation success (summary: $SUMMARY_FILE)"
    exit 0
fi

log "validation failed (failures=$FAILURES, summary: $SUMMARY_FILE)"
exit 1
