#!/usr/bin/env bash
set -euo pipefail

SYNAPSED="./KeplerSynapseNet/build/synapsed"
TOR_BIN="tor"
BASE_DIR="$HOME/.synapsenet-tor-devnet"

NODE1_DIR="$BASE_DIR/node1"
NODE2_DIR="$BASE_DIR/node2"
NODE3_DIR="$BASE_DIR/node3"

TOR1_DIR="$BASE_DIR/tor1"
TOR2_DIR="$BASE_DIR/tor2"
TOR3_DIR="$BASE_DIR/tor3"

NODE1_P2P=8333
NODE2_P2P=8334
NODE3_P2P=8336

TOR1_SOCKS=9050
TOR2_SOCKS=9052
TOR3_SOCKS=9054

TOR1_CONTROL=9051
TOR2_CONTROL=9053
TOR3_CONTROL=9055

cleanup() {
    echo ""
    echo "Shutting down..."
    kill "$TOR1_PID" "$TOR2_PID" "$TOR3_PID" 2>/dev/null || true
    kill "$NODE1_PID" "$NODE2_PID" "$NODE3_PID" 2>/dev/null || true
    wait 2>/dev/null || true
    echo "All processes stopped."
}
trap cleanup EXIT

for d in "$NODE1_DIR" "$NODE2_DIR" "$NODE3_DIR" \
         "$TOR1_DIR/hidden_service" "$TOR2_DIR/hidden_service" "$TOR3_DIR/hidden_service"; do
    mkdir -p "$d"
done
chmod 700 "$TOR1_DIR" "$TOR2_DIR" "$TOR3_DIR"

gen_torrc() {
    local tor_dir="$1" socks_port="$2" control_port="$3" p2p_port="$4"
    cat > "$tor_dir/torrc" <<EOF
SocksPort $socks_port
ControlPort $control_port
DataDirectory $tor_dir
HiddenServiceDir $tor_dir/hidden_service
HiddenServicePort $p2p_port 127.0.0.1:$p2p_port
Log notice file $tor_dir/tor.log
EOF
}

gen_torrc "$TOR1_DIR" "$TOR1_SOCKS" "$TOR1_CONTROL" "$NODE1_P2P"
gen_torrc "$TOR2_DIR" "$TOR2_SOCKS" "$TOR2_CONTROL" "$NODE2_P2P"
gen_torrc "$TOR3_DIR" "$TOR3_SOCKS" "$TOR3_CONTROL" "$NODE3_P2P"

echo "=== Phase 1: Starting Tor instances ==="

"$TOR_BIN" -f "$TOR1_DIR/torrc" &
TOR1_PID=$!
"$TOR_BIN" -f "$TOR2_DIR/torrc" &
TOR2_PID=$!
"$TOR_BIN" -f "$TOR3_DIR/torrc" &
TOR3_PID=$!

echo "Waiting for Tor to bootstrap and generate .onion addresses..."

wait_for_onion() {
    local hostname_file="$1" label="$2" max_wait=120 elapsed=0
    while [ ! -f "$hostname_file" ] || [ ! -s "$hostname_file" ]; do
        sleep 2
        elapsed=$((elapsed + 2))
        if [ "$elapsed" -ge "$max_wait" ]; then
            echo "ERROR: $label did not generate .onion address within ${max_wait}s"
            exit 1
        fi
    done
    cat "$hostname_file"
}

ONION1=$(wait_for_onion "$TOR1_DIR/hidden_service/hostname" "Tor1")
ONION2=$(wait_for_onion "$TOR2_DIR/hidden_service/hostname" "Tor2")
ONION3=$(wait_for_onion "$TOR3_DIR/hidden_service/hostname" "Tor3")

echo ""
echo "Node 1 onion: $ONION1"
echo "Node 2 onion: $ONION2"
echo "Node 3 onion: $ONION3"
echo ""

gen_node_conf() {
    local node_dir="$1" socks_port="$2" control_port="$3"
    cat > "$node_dir/synapsenet.conf" <<EOF
agent.tor.required=true
agent.tor.mode=external
tor.socks.host=127.0.0.1
tor.socks.port=$socks_port
tor.control.port=$control_port
agent.tor.socks_host=127.0.0.1
agent.tor.socks_port=$socks_port
agent.routing.allow_clearnet_fallback=false
agent.routing.allow_p2p_clearnet_fallback=false
EOF
}

gen_node_conf "$NODE1_DIR" "$TOR1_SOCKS" "$TOR1_CONTROL"
gen_node_conf "$NODE2_DIR" "$TOR2_SOCKS" "$TOR2_CONTROL"
gen_node_conf "$NODE3_DIR" "$TOR3_SOCKS" "$TOR3_CONTROL"

echo "=== Phase 2: Starting SynapseNet nodes over Tor ==="

TERM=xterm-256color "$SYNAPSED" \
    --regtest --privacy --daemon \
    --datadir="$NODE1_DIR" --port "$NODE1_P2P" --rpcport 18331 \
    --config "$NODE1_DIR/synapsenet.conf" \
    --seednode "${ONION2%.}:$NODE2_P2P" \
    --seednode "${ONION3%.}:$NODE3_P2P" \
    > "$NODE1_DIR/node.log" 2>&1 &
NODE1_PID=$!

TERM=xterm-256color "$SYNAPSED" \
    --regtest --privacy --daemon \
    --datadir="$NODE2_DIR" --port "$NODE2_P2P" --rpcport 18332 \
    --config "$NODE2_DIR/synapsenet.conf" \
    --seednode "${ONION1%.}:$NODE1_P2P" \
    --seednode "${ONION3%.}:$NODE3_P2P" \
    > "$NODE2_DIR/node.log" 2>&1 &
NODE2_PID=$!

TERM=xterm-256color "$SYNAPSED" \
    --regtest --privacy --daemon \
    --datadir="$NODE3_DIR" --port "$NODE3_P2P" --rpcport 18333 \
    --config "$NODE3_DIR/synapsenet.conf" \
    --seednode "${ONION1%.}:$NODE1_P2P" \
    --seednode "${ONION2%.}:$NODE2_P2P" \
    > "$NODE3_DIR/node.log" 2>&1 &
NODE3_PID=$!

echo "Node 1 PID: $NODE1_PID (port $NODE1_P2P, RPC 18331, SOCKS $TOR1_SOCKS)"
echo "Node 2 PID: $NODE2_PID (port $NODE2_P2P, RPC 18332, SOCKS $TOR2_SOCKS)"
echo "Node 3 PID: $NODE3_PID (port $NODE3_P2P, RPC 18333, SOCKS $TOR3_SOCKS)"
echo ""

echo "Waiting for nodes to discover each other over Tor..."
sleep 30

check_peers() {
    local rpc_port="$1" label="$2"
    local result
    result=$(curl -s --max-time 5 "http://127.0.0.1:$rpc_port" \
        -H "Content-Type: application/json" \
        -d '{"method":"getpeerinfo","params":[],"id":1}' 2>/dev/null || echo "{}")
    local count
    count=$(echo "$result" | grep -o '"state":"CONNECTED"' | wc -l)
    echo "$label: $count connected peers"
    echo "$result" | python3 -m json.tool 2>/dev/null || echo "$result"
}

echo ""
echo "=== Peer Status ==="
check_peers 18331 "Node 1"
echo ""
check_peers 18332 "Node 2"
echo ""
check_peers 18333 "Node 3"

echo ""
echo "Tor devnet is running. Press Ctrl+C to stop all processes."
wait
