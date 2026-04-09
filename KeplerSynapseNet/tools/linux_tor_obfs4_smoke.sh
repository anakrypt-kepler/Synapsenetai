#!/usr/bin/env bash
set -euo pipefail

OUT_FILE="${OUT_FILE:-}"
MODE="${MODE:-user}"
SYSTEMD_UNIT="${SYSTEMD_UNIT:-tor}"
SYSTEMD_START=0
OWNER_CHECK=1
SOCKS_HOST="${SOCKS_HOST:-127.0.0.1}"
SOCKS_PORT="${SOCKS_PORT:-9150}"
CONTROL_PORT="${CONTROL_PORT:-9151}"
DATA_DIR="${DATA_DIR:-/tmp/synapsenet_tor_obfs4_linux}"
BOOTSTRAP_TIMEOUT_SEC="${BOOTSTRAP_TIMEOUT_SEC:-240}"
BOOTSTRAP_CHECK=0
KEEP_RUNNING=0
PROBE_URL="${PROBE_URL:-https://duckduckgo.com/robots.txt}"
PROBE_MAX_TIME="${PROBE_MAX_TIME:-30}"
TOR_BIN="${TOR_BIN:-}"
OBFS4PROXY_BIN="${OBFS4PROXY_BIN:-}"
SNOWFLAKE_BIN="${SNOWFLAKE_BIN:-}"
SNIPPET_FILE=""

DISTRO_ID="unknown"
DISTRO_VERSION="unknown"

declare -a BRIDGE_INPUTS=()
declare -a BRIDGE_FILES=()

usage() {
    cat <<'USAGE'
Usage: tools/linux_tor_obfs4_smoke.sh [options]

Options:
  --mode MODE                   Runtime mode: user | systemd (default: user)
  --systemd-unit NAME           systemd unit for Tor mode=systemd (default: tor)
  --systemd-start               Restart/start systemd unit before bootstrap check
  --skip-owner-check            Skip runtime directory ownership check (not recommended)
  --out PATH                    Tor config output path
                                default mode=user:    /tmp/tor-obfs4-linux.conf
                                default mode=systemd: /etc/tor/torrc.d/synapsenet-obfs4.conf
  --bridge "Bridge obfs4 ..."   Add a bridge line (repeatable). "obfs4 ..." also accepted.
  --bridges-file PATH           Read bridge lines from file (repeatable).
  --socks-host HOST             SOCKS host (default: 127.0.0.1)
  --socks-port PORT             SOCKS port (default: 9150)
  --control-port PORT           ControlPort (default: 9151, use 0 to omit)
  --data-dir PATH               DataDirectory for user mode bootstrap checks
                                (default: /tmp/synapsenet_tor_obfs4_linux)
  --bootstrap-check             Validate bootstrap + web readiness probe
  --bootstrap-timeout-sec N     Bootstrap timeout (default: 240)
  --probe-url URL               Probe URL for curl via Tor SOCKS
  --keep-running                Keep temporary user-mode Tor running after successful check
  --write-synapsenet-snippet P  Write SynapseNet external-Tor snippet to file
  --tor-bin PATH                Override tor binary path
  --obfs4proxy-bin PATH         Override obfs4proxy binary path
  --snowflake-bin PATH          Override snowflake-client binary path (optional detection)
  --help                        Show this help

Examples:
  tools/linux_tor_obfs4_smoke.sh \
    --mode user \
    --bridges-file /tmp/bridges.txt \
    --bootstrap-check \
    --write-synapsenet-snippet /tmp/synapsenet_external_9150.conf

  tools/linux_tor_obfs4_smoke.sh \
    --mode systemd \
    --systemd-unit tor \
    --systemd-start \
    --bridge "Bridge obfs4 1.2.3.4:443 FINGERPRINT cert=... iat-mode=0"
USAGE
}

log() {
    printf '[%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

require_linux() {
    local os
    os="$(uname -s 2>/dev/null || true)"
    [[ "$os" == "Linux" ]] || die "this helper is for Linux; detected: ${os:-unknown}"
}

detect_distro() {
    if [[ -f /etc/os-release ]]; then
        # shellcheck disable=SC1091
        source /etc/os-release || true
        DISTRO_ID="${ID:-unknown}"
        DISTRO_VERSION="${VERSION_ID:-unknown}"
    fi
}

package_hint() {
    case "$DISTRO_ID" in
        ubuntu|debian)
            echo "apt-get install tor obfs4proxy snowflake-client"
            ;;
        fedora|rhel|centos|rocky|almalinux)
            echo "dnf install tor obfs4proxy snowflake"
            ;;
        arch|manjaro)
            echo "pacman -S tor obfs4proxy snowflake"
            ;;
        opensuse*|sles)
            echo "zypper install tor obfs4proxy snowflake-client"
            ;;
        *)
            echo "install packages: tor + obfs4proxy (+ snowflake-client optional)"
            ;;
    esac
}

validate_mode() {
    case "$MODE" in
        user|systemd) ;;
        *) die "invalid --mode: $MODE (expected user|systemd)" ;;
    esac
}

resolve_out_file_default() {
    if [[ -n "$OUT_FILE" ]]; then
        return 0
    fi
    if [[ "$MODE" == "systemd" ]]; then
        OUT_FILE="/etc/tor/torrc.d/synapsenet-obfs4.conf"
    else
        OUT_FILE="/tmp/tor-obfs4-linux.conf"
    fi
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
    local c
    for c in /usr/bin/tor /usr/sbin/tor /usr/local/bin/tor /usr/lib/tor/tor /snap/bin/tor; do
        [[ -x "$c" ]] && { echo "$c"; return 0; }
    done
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
    local c
    for c in /usr/bin/obfs4proxy /usr/local/bin/obfs4proxy /usr/libexec/obfs4proxy /usr/lib/tor/obfs4proxy; do
        [[ -x "$c" ]] && { echo "$c"; return 0; }
    done
    return 1
}

detect_snowflake_bin() {
    if [[ -n "$SNOWFLAKE_BIN" && -x "$SNOWFLAKE_BIN" ]]; then
        echo "$SNOWFLAKE_BIN"
        return 0
    fi
    if command -v snowflake-client >/dev/null 2>&1; then
        command -v snowflake-client
        return 0
    fi
    local c
    for c in /usr/bin/snowflake-client /usr/local/bin/snowflake-client /usr/libexec/snowflake-client; do
        [[ -x "$c" ]] && { echo "$c"; return 0; }
    done
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
    tmp="$(mktemp /tmp/synapsenet_linux_obfs4_bridges.XXXXXX)"

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

    [[ -s "$tmp" ]] || { rm -f "$tmp"; die "no valid obfs4 bridge lines provided"; }
    awk '!seen[$0]++' "$tmp"
    rm -f "$tmp"
}

ensure_parent_writable() {
    local path="$1"
    local parent
    parent="$(dirname "$path")"
    mkdir -p "$parent" 2>/dev/null || true
    [[ -d "$parent" ]] || die "cannot create parent directory for $path"
    [[ -w "$parent" ]] || die "parent directory not writable: $parent"
}

ensure_user_runtime_dir_ownership() {
    [[ "$MODE" == "user" ]] || return 0
    [[ "$OWNER_CHECK" == "1" ]] || return 0

    mkdir -p "$DATA_DIR"
    chmod 700 "$DATA_DIR" 2>/dev/null || true

    local owner_uid current_uid
    owner_uid="$(stat -c '%u' "$DATA_DIR" 2>/dev/null || true)"
    current_uid="$(id -u)"
    [[ -n "$owner_uid" ]] || die "failed to determine owner for data dir: $DATA_DIR"
    [[ "$owner_uid" == "$current_uid" ]] || die "data dir owner mismatch ($DATA_DIR owner=$owner_uid current=$current_uid). Use a per-user data dir or fix ownership."
}

write_torrc() {
    local torrc="$1"
    local obfs4="$2"
    shift 2
    local bridges=("$@")

    ensure_parent_writable "$torrc"

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
    verify_log="$(mktemp /tmp/synapsenet_linux_tor_verify.XXXXXX.log)"
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
    cat >"$path" <<SNIPPET
agent.tor.required=true
agent.tor.mode=external
agent.routing.allow_clearnet_fallback=false
agent.routing.allow_p2p_clearnet_fallback=false
agent.tor.socks_host=$SOCKS_HOST
agent.tor.socks_port=$SOCKS_PORT
tor.socks.host=$SOCKS_HOST
tor.socks.port=$SOCKS_PORT
SNIPPET
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

pid_cmdline() {
    local pid="$1"
    [[ -r "/proc/$pid/cmdline" ]] || return 1
    tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null || return 1
}

is_owned_pid() {
    local pid="$1"
    local torrc="$2"
    local data_dir="$3"
    local cmd
    kill -0 "$pid" >/dev/null 2>&1 || return 1
    cmd="$(pid_cmdline "$pid" || true)"
    [[ -n "$cmd" ]] || return 1
    [[ "$cmd" == *"$torrc"* ]] || return 1
    [[ "$cmd" == *"$data_dir"* ]] || return 1
    return 0
}

stop_pid_owned_quiet() {
    local pid="$1"
    local torrc="$2"
    local data_dir="$3"
    [[ -n "$pid" ]] || return 0
    if is_owned_pid "$pid" "$torrc" "$data_dir"; then
        kill -TERM "$pid" >/dev/null 2>&1 || true
        wait_pid_exit "$pid" || true
        return 0
    fi
    log "skip killing pid $pid (fingerprint mismatch or not owned by this helper)"
    return 0
}

port_in_use() {
    local port="$1"
    if command -v lsof >/dev/null 2>&1; then
        lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1
        return $?
    fi
    if command -v ss >/dev/null 2>&1; then
        ss -ltn "( sport = :$port )" 2>/dev/null | grep -q ":$port"
        return $?
    fi
    return 1
}

probe_via_socks_once() {
    curl --silent --show-error --location \
        --socks5-hostname "${SOCKS_HOST}:${SOCKS_PORT}" \
        --connect-timeout 20 \
        --max-time "$PROBE_MAX_TIME" \
        --user-agent "SynapseNet-linux-obfs4-smoke/1.0" \
        --output /dev/null \
        "$PROBE_URL"
}

wait_for_probe_ready() {
    local deadline=$(( $(date +%s) + BOOTSTRAP_TIMEOUT_SEC ))
    while (( $(date +%s) < deadline )); do
        if probe_via_socks_once >/dev/null 2>&1; then
            return 0
        fi
        sleep 2
    done
    return 1
}

bootstrap_check_user_mode() {
    local tor_bin="$1"
    local torrc="$2"
    local log_file="$DATA_DIR/tor_bootstrap.log"
    local pid_file="$DATA_DIR/tor.pid"
    local pid=""

    ensure_user_runtime_dir_ownership
    rm -f "$pid_file" "$log_file"

    if port_in_use "$SOCKS_PORT"; then
        die "SOCKS port $SOCKS_PORT already in use; stop conflicting Tor or choose another port"
    fi

    "$tor_bin" -f "$torrc" \
        --DataDirectory "$DATA_DIR" \
        --PidFile "$pid_file" \
        --RunAsDaemon 1 \
        --Log "notice file $log_file"

    if [[ -f "$pid_file" ]]; then
        pid="$(tr -d '[:space:]' <"$pid_file" || true)"
    fi

    if ! wait_for_bootstrap "$log_file"; then
        [[ -n "$pid" ]] && stop_pid_owned_quiet "$pid" "$torrc" "$DATA_DIR"
        [[ -f "$log_file" ]] && tail -n 120 "$log_file" >&2 || true
        die "Tor bootstrap did not reach 100% within ${BOOTSTRAP_TIMEOUT_SEC}s"
    fi

    if ! probe_via_socks_once; then
        [[ -n "$pid" ]] && stop_pid_owned_quiet "$pid" "$torrc" "$DATA_DIR"
        die "curl probe via Tor SOCKS failed: $PROBE_URL"
    fi

    log "bootstrap check passed (Tor 100% + curl probe via ${SOCKS_HOST}:${SOCKS_PORT})"
    log "bootstrap log: $log_file"
    [[ -n "$pid" ]] && log "tor pid: $pid"
    if [[ "$KEEP_RUNNING" != "1" && -n "$pid" ]]; then
        stop_pid_owned_quiet "$pid" "$torrc" "$DATA_DIR"
        log "stopped temporary Tor runtime after bootstrap validation"
    elif [[ "$KEEP_RUNNING" == "1" && -n "$pid" ]]; then
        log "leaving Tor running (--keep-running)"
    fi
}

bootstrap_check_systemd_mode() {
    command -v systemctl >/dev/null 2>&1 || die "systemctl not found but --mode systemd requested"

    if [[ "$SYSTEMD_START" == "1" ]]; then
        if ! systemctl restart "$SYSTEMD_UNIT" >/dev/null 2>&1; then
            die "failed to restart systemd unit '$SYSTEMD_UNIT'"
        fi
    fi

    if ! systemctl is-active --quiet "$SYSTEMD_UNIT"; then
        die "systemd unit '$SYSTEMD_UNIT' is not active"
    fi

    if ! wait_for_probe_ready; then
        journalctl -u "$SYSTEMD_UNIT" -n 120 --no-pager 2>/dev/null || true
        die "systemd mode probe did not succeed within ${BOOTSTRAP_TIMEOUT_SEC}s"
    fi

    log "systemd mode readiness check passed (unit=$SYSTEMD_UNIT, socks=${SOCKS_HOST}:${SOCKS_PORT})"
}

bootstrap_check() {
    local tor_bin="$1"
    local torrc="$2"
    if [[ "$MODE" == "systemd" ]]; then
        bootstrap_check_systemd_mode
    else
        bootstrap_check_user_mode "$tor_bin" "$torrc"
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode) shift; [[ $# -gt 0 ]] || die "missing value for --mode"; MODE="$1" ;;
        --systemd-unit) shift; [[ $# -gt 0 ]] || die "missing value for --systemd-unit"; SYSTEMD_UNIT="$1" ;;
        --systemd-start) SYSTEMD_START=1 ;;
        --skip-owner-check) OWNER_CHECK=0 ;;
        --out) shift; [[ $# -gt 0 ]] || die "missing value for --out"; OUT_FILE="$1" ;;
        --bridge) shift; [[ $# -gt 0 ]] || die "missing value for --bridge"; BRIDGE_INPUTS+=("$1") ;;
        --bridges-file) shift; [[ $# -gt 0 ]] || die "missing value for --bridges-file"; BRIDGE_FILES+=("$1") ;;
        --socks-host) shift; [[ $# -gt 0 ]] || die "missing value for --socks-host"; SOCKS_HOST="$1" ;;
        --socks-port) shift; [[ $# -gt 0 ]] || die "missing value for --socks-port"; SOCKS_PORT="$1" ;;
        --control-port) shift; [[ $# -gt 0 ]] || die "missing value for --control-port"; CONTROL_PORT="$1" ;;
        --data-dir) shift; [[ $# -gt 0 ]] || die "missing value for --data-dir"; DATA_DIR="$1" ;;
        --bootstrap-check) BOOTSTRAP_CHECK=1 ;;
        --bootstrap-timeout-sec) shift; [[ $# -gt 0 ]] || die "missing value for --bootstrap-timeout-sec"; BOOTSTRAP_TIMEOUT_SEC="$1" ;;
        --probe-url) shift; [[ $# -gt 0 ]] || die "missing value for --probe-url"; PROBE_URL="$1" ;;
        --keep-running) KEEP_RUNNING=1 ;;
        --write-synapsenet-snippet) shift; [[ $# -gt 0 ]] || die "missing value for --write-synapsenet-snippet"; SNIPPET_FILE="$1" ;;
        --tor-bin) shift; [[ $# -gt 0 ]] || die "missing value for --tor-bin"; TOR_BIN="$1" ;;
        --obfs4proxy-bin) shift; [[ $# -gt 0 ]] || die "missing value for --obfs4proxy-bin"; OBFS4PROXY_BIN="$1" ;;
        --snowflake-bin) shift; [[ $# -gt 0 ]] || die "missing value for --snowflake-bin"; SNOWFLAKE_BIN="$1" ;;
        --help|-h) usage; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
    shift
done

require_linux
detect_distro
validate_mode
resolve_out_file_default

TOR_BIN="$(detect_tor_bin || true)"
[[ -n "$TOR_BIN" ]] || die "tor binary not found; install Tor (${DISTRO_ID} hint: $(package_hint))"
OBFS4PROXY_BIN="$(detect_obfs4proxy_bin || true)"
[[ -n "$OBFS4PROXY_BIN" ]] || die "obfs4proxy binary not found (${DISTRO_ID} hint: $(package_hint))"
SNOWFLAKE_BIN="$(detect_snowflake_bin || true)"
command -v curl >/dev/null 2>&1 || die "curl not found"

declare -a BRIDGES=()
while IFS= read -r _bridge_line; do
    BRIDGES+=("$_bridge_line")
done < <(collect_bridges)

write_torrc "$OUT_FILE" "$OBFS4PROXY_BIN" "${BRIDGES[@]}"
verify_torrc "$TOR_BIN" "$OUT_FILE"

log "linux distro: ${DISTRO_ID} ${DISTRO_VERSION}"
log "mode: $MODE"
log "tor binary: $TOR_BIN"
log "obfs4proxy binary: $OBFS4PROXY_BIN"
if [[ -n "$SNOWFLAKE_BIN" ]]; then
    log "snowflake-client binary: $SNOWFLAKE_BIN"
else
    log "snowflake-client binary: not found (optional for obfs4 flow)"
fi
log "wrote torrc: $OUT_FILE"
log "bridges: ${#BRIDGES[@]}"
log "socks: ${SOCKS_HOST}:${SOCKS_PORT}"
[[ "$CONTROL_PORT" != "0" ]] && log "control: 127.0.0.1:${CONTROL_PORT}"

if [[ -n "$SNIPPET_FILE" ]]; then
    write_synapsenet_snippet "$SNIPPET_FILE"
    log "wrote SynapseNet external-Tor snippet: $SNIPPET_FILE"
fi

if [[ "$BOOTSTRAP_CHECK" == "1" ]]; then
    bootstrap_check "$TOR_BIN" "$OUT_FILE"
else
    log "bootstrap check skipped (use --bootstrap-check to validate readiness)"
fi
