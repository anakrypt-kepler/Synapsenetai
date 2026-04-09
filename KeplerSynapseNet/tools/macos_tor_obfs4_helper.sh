#!/usr/bin/env bash
set -eo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_FILE="${OUT_FILE:-/tmp/tor-obfs4.conf}"
SOCKS_HOST="${SOCKS_HOST:-127.0.0.1}"
SOCKS_PORT="${SOCKS_PORT:-9150}"
CONTROL_PORT="${CONTROL_PORT:-9151}"
DATA_DIR="${DATA_DIR:-/tmp/synapsenet_tor_obfs4}"
BOOTSTRAP_TIMEOUT_SEC="${BOOTSTRAP_TIMEOUT_SEC:-240}"
BOOTSTRAP_CHECK=0
KEEP_RUNNING=0
PROBE_URL="${PROBE_URL:-https://duckduckgo.com/robots.txt}"
PROBE_MAX_TIME="${PROBE_MAX_TIME:-30}"
BOOTSTRAP_ATTEMPTS="${BOOTSTRAP_ATTEMPTS:-1}"
BRIDGE_SUBSET_SIZE="${BRIDGE_SUBSET_SIZE:-0}"
TAKEOVER_PORT_OWNER=0
TOR_BIN="${TOR_BIN:-}"
OBFS4PROXY_BIN="${OBFS4PROXY_BIN:-}"
SNOWFLAKE_BIN="${SNOWFLAKE_BIN:-}"
BRIDGES_FILE=""
SNIPPET_FILE=""

declare -a BRIDGE_INPUTS=()
declare -a BRIDGE_FILES=()

usage() {
    cat <<'EOF'
Usage: tools/macos_tor_obfs4_helper.sh [options]

Options:
  --out PATH                    Tor config output file (default: /tmp/tor-obfs4.conf)
  --bridge "Bridge obfs4 ..."   Add a bridge line (repeatable). "obfs4 ..." also accepted.
  --bridges-file PATH           Read bridge lines from file (repeatable).
  --socks-host HOST             SOCKS host (default: 127.0.0.1)
  --socks-port PORT             SOCKS port (default: 9150)
  --control-port PORT           ControlPort (default: 9151, use 0 to omit)
  --data-dir PATH               DataDirectory for bootstrap check (default: /tmp/synapsenet_tor_obfs4)
  --bootstrap-check             Start Tor and wait for Bootstrapped 100%, then curl probe via SOCKS
  --bootstrap-timeout-sec N     Bootstrap timeout (default: 240)
  --bootstrap-attempts N        Retry attempts with rotated bridge subsets (default: 1)
  --bridge-subset-size N        Per-attempt bridge subset size (0=all bridges, default: 0)
  --probe-url URL               Probe URL for curl via Tor SOCKS
  --takeover-port-owner         Attempt to reclaim occupied SOCKS port (kills owner PID;
                                for Tor Browser runtime also kills owning controller PID)
  --keep-running                Keep Tor running after successful bootstrap check
  --write-synapsenet-snippet P  Write SynapseNet external-Tor config snippet to file
  --help                        Show this help

Examples:
  tools/macos_tor_obfs4_helper.sh \
    --bridges-file /tmp/bridges.txt \
    --bootstrap-check \
    --write-synapsenet-snippet /tmp/synapsenet_external_9150.conf

  tools/macos_tor_obfs4_helper.sh \
    --bridge "Bridge obfs4 1.2.3.4:443 FINGERPRINT cert=... iat-mode=0" \
    --out /tmp/tor-obfs4.conf
EOF
}

log() {
    printf '[%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

require_darwin() {
    local os
    os="$(uname -s 2>/dev/null || true)"
    [[ "$os" == "Darwin" ]] || die "this helper is for macOS (Darwin); detected: ${os:-unknown}"
}

detect_brew_bin() {
    if command -v brew >/dev/null 2>&1; then
        command -v brew
        return 0
    fi
    if [[ -x /opt/homebrew/bin/brew ]]; then
        echo /opt/homebrew/bin/brew
        return 0
    fi
    return 1
}

detect_tor_bin() {
    if [[ -n "$TOR_BIN" && -x "$TOR_BIN" ]]; then
        echo "$TOR_BIN"
        return 0
    fi
    if command -v tor >/dev/null 2>&1; then
        command -v tor
        return 0
    fi
    local brew_bin
    brew_bin="$(detect_brew_bin || true)"
    if [[ -n "$brew_bin" ]]; then
        local tor_prefix
        tor_prefix="$("$brew_bin" --prefix tor 2>/dev/null || true)"
        if [[ -x "$tor_prefix/bin/tor" ]]; then
            echo "$tor_prefix/bin/tor"
            return 0
        fi
    fi
    return 1
}

detect_obfs4proxy_bin() {
    if [[ -n "$OBFS4PROXY_BIN" && -x "$OBFS4PROXY_BIN" ]]; then
        echo "$OBFS4PROXY_BIN"
        return 0
    fi
    if command -v obfs4proxy >/dev/null 2>&1; then
        command -v obfs4proxy
        return 0
    fi
    local brew_bin
    brew_bin="$(detect_brew_bin || true)"
    if [[ -n "$brew_bin" ]]; then
        local obfs_prefix
        obfs_prefix="$("$brew_bin" --prefix obfs4proxy 2>/dev/null || true)"
        if [[ -x "$obfs_prefix/bin/obfs4proxy" ]]; then
            echo "$obfs_prefix/bin/obfs4proxy"
            return 0
        fi
    fi
    return 1
}

detect_snowflake_client_bin() {
    if [[ -n "$SNOWFLAKE_BIN" && -x "$SNOWFLAKE_BIN" ]]; then
        echo "$SNOWFLAKE_BIN"
        return 0
    fi
    if command -v snowflake-client >/dev/null 2>&1; then
        command -v snowflake-client
        return 0
    fi
    local brew_bin
    brew_bin="$(detect_brew_bin || true)"
    if [[ -n "$brew_bin" ]]; then
        local snowflake_prefix
        snowflake_prefix="$("$brew_bin" --prefix snowflake-client 2>/dev/null || true)"
        if [[ -x "$snowflake_prefix/bin/snowflake-client" ]]; then
            echo "$snowflake_prefix/bin/snowflake-client"
            return 0
        fi
    fi
    return 1
}

normalize_bridge_line() {
    local raw="$1"
    local line
    line="$(printf '%s' "$raw" | sed -e 's/\r$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
    [[ -n "$line" ]] || return 1
    [[ "${line#\#}" == "$line" ]] || return 1
    if [[ "$line" == obfs4\ * ]]; then
        line="Bridge $line"
    fi
    [[ "$line" == Bridge\ obfs4\ * ]] || die "invalid bridge line (expected 'Bridge obfs4 ...'): $line"
    printf '%s\n' "$line"
}

collect_bridges() {
    local tmp
    tmp="$(mktemp /tmp/synapsenet_obfs4_bridges.XXXXXX)"

    local item normalized
    for item in "${BRIDGE_INPUTS[@]}"; do
        normalized="$(normalize_bridge_line "$item" || true)"
        [[ -n "${normalized:-}" ]] && printf '%s\n' "$normalized" >>"$tmp"
    done

    local f line
    for f in "${BRIDGE_FILES[@]}"; do
        [[ -f "$f" ]] || die "bridges file not found: $f"
        while IFS= read -r line || [[ -n "$line" ]]; do
            normalized="$(normalize_bridge_line "$line" || true)"
            [[ -n "${normalized:-}" ]] && printf '%s\n' "$normalized" >>"$tmp"
        done <"$f"
    done

    [[ -s "$tmp" ]] || {
        rm -f "$tmp"
        die "no valid obfs4 bridge lines provided. Create a bridges file with 'Bridge obfs4 ...' lines and pass --bridges-file. Get bridges from https://bridges.torproject.org/bridges?transport=obfs4 or Tor Browser: Settings -> Connection -> Bridges."
    }
    awk '!seen[$0]++' "$tmp"
    rm -f "$tmp"
}

write_torrc() {
    local torrc="$1"
    local obfs4="$2"
    shift 2
    local bridges=("$@")
    mkdir -p "$(dirname "$torrc")"
    {
        printf 'SocksPort %s:%s\n' "$SOCKS_HOST" "$SOCKS_PORT"
        if [[ "$CONTROL_PORT" != "0" ]]; then
            printf 'ControlPort 127.0.0.1:%s\n' "$CONTROL_PORT"
        fi
        printf 'UseBridges 1\n'
        printf 'ClientTransportPlugin obfs4 exec %s\n' "$obfs4"
        printf '\n'
        local b
        for b in "${bridges[@]}"; do
            printf '%s\n' "$b"
        done
    } >"$torrc"
}

verify_torrc() {
    local tor_bin="$1"
    local torrc="$2"
    local verify_log
    verify_log="$(mktemp /tmp/synapsenet_tor_verify.XXXXXX.log)"
    if ! "$tor_bin" --verify-config -f "$torrc" >"$verify_log" 2>&1; then
        cat "$verify_log" >&2
        rm -f "$verify_log"
        die "tor config verification failed for $torrc"
    fi
    rm -f "$verify_log"
}

write_synapsenet_snippet() {
    local path="$1"
    mkdir -p "$(dirname "$path")"
    cat >"$path" <<EOF
agent.tor.required=true
agent.tor.mode=external
agent.routing.allow_clearnet_fallback=false
agent.routing.allow_p2p_clearnet_fallback=false
agent.tor.socks_host=$SOCKS_HOST
agent.tor.socks_port=$SOCKS_PORT
tor.socks.host=$SOCKS_HOST
tor.socks.port=$SOCKS_PORT
EOF
}

wait_for_bootstrap() {
    local log_file="$1"
    local deadline=$(( $(date +%s) + BOOTSTRAP_TIMEOUT_SEC ))
    while (( $(date +%s) < deadline )); do
        if [[ -f "$log_file" ]] && grep -q 'Bootstrapped 100% (done): Done' "$log_file"; then
            return 0
        fi
        sleep 1
    done
    return 1
}

wait_pid_exit() {
    local pid="$1"
    local i
    for i in $(seq 1 30); do
        if ! kill -0 "$pid" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    return 1
}

stop_pid_quiet() {
    local pid="$1"
    [[ -n "$pid" ]] || return 0
    if kill -0 "$pid" >/dev/null 2>&1; then
        kill -TERM "$pid" >/dev/null 2>&1 || true
        wait_pid_exit "$pid" || true
    fi
}

listener_owner_details() {
    local port="$1"
    if ! command -v lsof >/dev/null 2>&1; then
        return 1
    fi
    local pid
    pid="$(lsof -nP -iTCP:"$port" -sTCP:LISTEN -t 2>/dev/null | head -n 1 || true)"
    [[ -n "$pid" ]] || return 1
    local cmd
    cmd="$(ps -p "$pid" -o command= 2>/dev/null || true)"
    printf 'pid=%s cmd=%s\n' "$pid" "${cmd:-unknown}"
}

port_in_use_hint() {
    local port="$1"
    local details
    details="$(listener_owner_details "$port" || true)"
    if [[ -z "$details" ]]; then
        printf 'SOCKS port %s already in use; stop conflicting Tor or choose another port' "$port"
        return 0
    fi
    if [[ "$details" == *"Tor Browser.app"* ]]; then
        if [[ "$details" == *"DisableNetwork 1"* ]]; then
            printf 'SOCKS port %s is owned by Tor Browser runtime with DisableNetwork=1 (%s). Close Tor Browser or move it to external-proxy mode on the same external Tor, then retry' "$port" "$details"
            return 0
        fi
        printf 'SOCKS port %s is owned by Tor Browser runtime (%s). Close Tor Browser or use a different SOCKS port for managed bootstrap' "$port" "$details"
        return 0
    fi
    printf 'SOCKS port %s already in use (%s). Stop conflicting process or choose another port' "$port" "$details"
}

listener_owner_pid() {
    local port="$1"
    if ! command -v lsof >/dev/null 2>&1; then
        return 1
    fi
    lsof -nP -iTCP:"$port" -sTCP:LISTEN -t 2>/dev/null | head -n 1
}

listener_owner_cmd() {
    local pid="$1"
    ps -p "$pid" -o command= 2>/dev/null || true
}

extract_tor_browser_controller_pid() {
    local cmd="$1"
    if [[ "$cmd" =~ __OwningControllerProcess[[:space:]]+([0-9]+) ]]; then
        printf '%s\n' "${BASH_REMATCH[1]}"
        return 0
    fi
    return 1
}

wait_port_release() {
    local port="$1"
    local timeout_sec="$2"
    local deadline=$(( $(date +%s) + timeout_sec ))
    while (( $(date +%s) < deadline )); do
        if ! lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    return 1
}

try_takeover_port_owner() {
    local port="$1"
    local pid cmd controller_pid
    pid="$(listener_owner_pid "$port" || true)"
    [[ -n "$pid" ]] || return 0
    cmd="$(listener_owner_cmd "$pid")"
    log "takeover requested: attempting to reclaim port ${port} from pid=$pid"
    if [[ -n "$cmd" ]]; then
        log "takeover owner command: $cmd"
    fi

    kill -TERM "$pid" >/dev/null 2>&1 || true
    sleep 1

    if [[ "$cmd" == *"Tor Browser.app/Contents/MacOS/Tor/tor"* ]]; then
        controller_pid="$(extract_tor_browser_controller_pid "$cmd" || true)"
        if [[ -n "$controller_pid" && "$controller_pid" =~ ^[0-9]+$ ]]; then
            log "takeover: stopping Tor Browser controller pid=$controller_pid"
            kill -TERM "$controller_pid" >/dev/null 2>&1 || true
        fi
        pkill -TERM -f "/Applications/Tor Browser.app/Contents/MacOS/Tor/tor" >/dev/null 2>&1 || true
    fi

    if wait_port_release "$port" 12; then
        log "takeover: port ${port} reclaimed"
        return 0
    fi

    return 1
}

select_bridge_subset_for_attempt() {
    local attempt="$1"
    local total="${#BRIDGES[@]}"
    [[ "$total" -gt 0 ]] || return 1

    local subset_size="$BRIDGE_SUBSET_SIZE"
    if [[ "$subset_size" -le 0 || "$subset_size" -ge "$total" ]]; then
        local i
        for ((i = 0; i < total; ++i)); do
            printf '%s\n' "${BRIDGES[$i]}"
        done
        return 0
    fi

    local start=$(( ((attempt - 1) * subset_size) % total ))
    local i idx
    for ((i = 0; i < subset_size; ++i)); do
        idx=$(( (start + i) % total ))
        printf '%s\n' "${BRIDGES[$idx]}"
    done
}

bootstrap_attempt_once() {
    local tor_bin="$1"
    local torrc="$2"
    local attempt="$3"
    local pid_file="$DATA_DIR/tor.pid"
    local log_file="$DATA_DIR/tor_bootstrap_attempt${attempt}.log"
    local pid=""

    rm -f "$pid_file" "$log_file"

    "$tor_bin" -f "$torrc" \
        --DataDirectory "$DATA_DIR" \
        --PidFile "$pid_file" \
        --RunAsDaemon 1 \
        --Log "notice file $log_file"

    if [[ -f "$pid_file" ]]; then
        pid="$(tr -d '[:space:]' <"$pid_file" || true)"
    fi

    if ! wait_for_bootstrap "$log_file"; then
        [[ -n "$pid" ]] && stop_pid_quiet "$pid"
        if [[ -f "$log_file" ]]; then
            tail -n 80 "$log_file" >&2 || true
        fi
        return 1
    fi

    if ! curl --silent --show-error --location \
        --socks5-hostname "${SOCKS_HOST}:${SOCKS_PORT}" \
        --connect-timeout 20 \
        --max-time "$PROBE_MAX_TIME" \
        --user-agent "SynapseNet-macOS-obfs4-helper/1.0" \
        --output /dev/null \
        "$PROBE_URL"; then
        [[ -n "$pid" ]] && stop_pid_quiet "$pid"
        if [[ -f "$log_file" ]]; then
            tail -n 80 "$log_file" >&2 || true
        fi
        return 1
    fi

    cp "$log_file" "$DATA_DIR/tor_bootstrap.log"
    log "bootstrap check passed on attempt $attempt (Tor 100%% + curl probe via ${SOCKS_HOST}:${SOCKS_PORT})"
    log "bootstrap log: $log_file"
    if [[ -n "$pid" ]]; then
        log "tor pid: $pid"
    fi
    if [[ "$KEEP_RUNNING" != "1" && -n "$pid" ]]; then
        stop_pid_quiet "$pid"
        log "stopped temporary Tor runtime after bootstrap validation"
    elif [[ "$KEEP_RUNNING" == "1" && -n "$pid" ]]; then
        log "leaving Tor running (--keep-running)"
    fi
    return 0
}

bootstrap_check() {
    local tor_bin="$1"
    local torrc="$2"
    local attempt=0
    local ok=0

    mkdir -p "$DATA_DIR"
    rm -f "$DATA_DIR"/tor_bootstrap.log "$DATA_DIR"/tor_bootstrap_attempt*.log "$DATA_DIR"/tor.pid

    if command -v lsof >/dev/null 2>&1; then
        if lsof -nP -iTCP:"$SOCKS_PORT" -sTCP:LISTEN >/dev/null 2>&1; then
            if [[ "$TAKEOVER_PORT_OWNER" == "1" ]]; then
                if ! try_takeover_port_owner "$SOCKS_PORT"; then
                    die "failed to reclaim occupied SOCKS port $SOCKS_PORT: $(port_in_use_hint "$SOCKS_PORT")"
                fi
            else
                die "$(port_in_use_hint "$SOCKS_PORT")"
            fi
        fi
    fi

    for attempt in $(seq 1 "$BOOTSTRAP_ATTEMPTS"); do
        local subset_count
        local subset_start
        declare -a subset=()
        while IFS= read -r _line; do
            subset+=("$_line")
        done < <(select_bridge_subset_for_attempt "$attempt")
        subset_count="${#subset[@]}"
        subset_start=0
        if [[ "$BRIDGE_SUBSET_SIZE" -gt 0 && "$BRIDGE_SUBSET_SIZE" -lt "${#BRIDGES[@]}" ]]; then
            subset_start=$(( ((attempt - 1) * BRIDGE_SUBSET_SIZE) % ${#BRIDGES[@]} ))
        fi

        log "bootstrap attempt ${attempt}/${BOOTSTRAP_ATTEMPTS}: using ${subset_count}/${#BRIDGES[@]} bridges (subsetStart=${subset_start})"
        write_torrc "$torrc" "$OBFS4PROXY_BIN" "${subset[@]}"
        verify_torrc "$tor_bin" "$torrc"

        if bootstrap_attempt_once "$tor_bin" "$torrc" "$attempt"; then
            ok=1
            break
        fi

        if [[ "$attempt" -lt "$BOOTSTRAP_ATTEMPTS" ]]; then
            log "bootstrap attempt $attempt failed; rotating bridge subset"
        fi
    done

    [[ "$ok" == "1" ]] || die "Tor bootstrap/web probe failed after ${BOOTSTRAP_ATTEMPTS} attempt(s)"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out)
            shift
            [[ $# -gt 0 ]] || die "missing value for --out"
            OUT_FILE="$1"
            ;;
        --bridge)
            shift
            [[ $# -gt 0 ]] || die "missing value for --bridge"
            BRIDGE_INPUTS+=("$1")
            ;;
        --bridges-file)
            shift
            [[ $# -gt 0 ]] || die "missing value for --bridges-file"
            BRIDGE_FILES+=("$1")
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
        --bootstrap-check)
            BOOTSTRAP_CHECK=1
            ;;
        --bootstrap-timeout-sec)
            shift
            [[ $# -gt 0 ]] || die "missing value for --bootstrap-timeout-sec"
            BOOTSTRAP_TIMEOUT_SEC="$1"
            ;;
        --bootstrap-attempts)
            shift
            [[ $# -gt 0 ]] || die "missing value for --bootstrap-attempts"
            BOOTSTRAP_ATTEMPTS="$1"
            ;;
        --bridge-subset-size)
            shift
            [[ $# -gt 0 ]] || die "missing value for --bridge-subset-size"
            BRIDGE_SUBSET_SIZE="$1"
            ;;
        --probe-url)
            shift
            [[ $# -gt 0 ]] || die "missing value for --probe-url"
            PROBE_URL="$1"
            ;;
        --takeover-port-owner)
            TAKEOVER_PORT_OWNER=1
            ;;
        --keep-running)
            KEEP_RUNNING=1
            ;;
        --write-synapsenet-snippet)
            shift
            [[ $# -gt 0 ]] || die "missing value for --write-synapsenet-snippet"
            SNIPPET_FILE="$1"
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

require_darwin

TOR_BIN="$(detect_tor_bin || true)"
[[ -n "$TOR_BIN" ]] || die "tor binary not found (install with: brew install tor)"
OBFS4PROXY_BIN="$(detect_obfs4proxy_bin || true)"
[[ -n "$OBFS4PROXY_BIN" ]] || die "obfs4proxy binary not found (install with: brew install obfs4proxy)"
SNOWFLAKE_BIN="$(detect_snowflake_client_bin || true)"
command -v curl >/dev/null 2>&1 || die "curl not found"

[[ "$BOOTSTRAP_ATTEMPTS" =~ ^[0-9]+$ ]] || die "--bootstrap-attempts must be a non-negative integer"
[[ "$BRIDGE_SUBSET_SIZE" =~ ^[0-9]+$ ]] || die "--bridge-subset-size must be a non-negative integer"
if [[ "$BOOTSTRAP_ATTEMPTS" -lt 1 ]]; then
    BOOTSTRAP_ATTEMPTS=1
fi

declare -a BRIDGES=()
while IFS= read -r _bridge_line; do
    BRIDGES+=("$_bridge_line")
done < <(collect_bridges)
write_torrc "$OUT_FILE" "$OBFS4PROXY_BIN" "${BRIDGES[@]}"
verify_torrc "$TOR_BIN" "$OUT_FILE"

log "wrote torrc: $OUT_FILE"
log "tor binary: $TOR_BIN"
log "obfs4proxy binary: $OBFS4PROXY_BIN"
if [[ -n "$SNOWFLAKE_BIN" ]]; then
    log "snowflake-client binary (optional): $SNOWFLAKE_BIN"
else
    log "snowflake-client binary (optional): not found"
fi
log "bridges: ${#BRIDGES[@]}"
log "socks: ${SOCKS_HOST}:${SOCKS_PORT}"
log "bootstrap attempts: ${BOOTSTRAP_ATTEMPTS}"
if [[ "$BRIDGE_SUBSET_SIZE" -le 0 || "$BRIDGE_SUBSET_SIZE" -ge "${#BRIDGES[@]}" ]]; then
    log "bridge subset size: all (${#BRIDGES[@]})"
else
    log "bridge subset size: ${BRIDGE_SUBSET_SIZE}"
fi
log "takeover port owner: $([[ "$TAKEOVER_PORT_OWNER" == "1" ]] && echo enabled || echo disabled)"
if [[ "$CONTROL_PORT" != "0" ]]; then
    log "control: 127.0.0.1:${CONTROL_PORT}"
fi

if [[ -n "$SNIPPET_FILE" ]]; then
    write_synapsenet_snippet "$SNIPPET_FILE"
    log "wrote SynapseNet external-Tor snippet: $SNIPPET_FILE"
fi

if [[ "$BOOTSTRAP_CHECK" == "1" ]]; then
    bootstrap_check "$TOR_BIN" "$OUT_FILE"
else
    log "bootstrap check skipped (use --bootstrap-check to validate Tor bootstrap + curl probe)"
fi
