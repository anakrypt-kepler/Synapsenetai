#!/usr/bin/env bash
set -e

GREEN="\033[0;32m"
RED="\033[0;31m"
NC="\033[0m"

log_ok() { echo -e "${GREEN}[OK]${NC} $1"; }
log_err() { echo -e "${RED}[ERR]${NC} $1"; }

NODE_BIN=./build/synapsed
DATADIR=/tmp/synapse_test
RPC_PORT=18332

if [ ! -x "$NODE_BIN" ]; then
  log_err "Node binary not found at $NODE_BIN"
  exit 1
fi

mkdir -p "$DATADIR"

"$NODE_BIN" --regtest --datadir "$DATADIR" --rpcport $RPC_PORT &
NODE_PID=$!
trap "kill $NODE_PID 2>/dev/null || true" EXIT

wait_ready() {
  local start=$(date +%s)
  while true; do
    if curl -s -X POST http://127.0.0.1:$RPC_PORT -d '{"method":"node.status"}' | grep -q "ready"; then
      return 0
    fi
    if [ $(( $(date +%s) - start )) -gt 30 ]; then
      return 1
    fi
    sleep 1
  done
}

if ! wait_ready; then
  log_err "Node did not become ready"
  exit 1
fi
log_ok "Node ready"

# Submit PoE entry
RESP=$(curl -s -X POST http://127.0.0.1:$RPC_PORT -d '{"method":"poe.submit","params":{"title":"Test Title E2E","body":"This is a long body with at least fifty characters to satisfy validation rules in tests.","authorKey":"00"}}')
SUBMIT_ID=$(echo "$RESP" | sed -n 's/.*"submitId"\s*:\s*"\([^"]*\)".*/\1/p')
if [ -z "$SUBMIT_ID" ]; then
  log_err "poe.submit failed: $RESP"
  exit 1
fi
log_ok "poe.submit returned id $SUBMIT_ID"

# Vote
RESP=$(curl -s -X POST http://127.0.0.1:$RPC_PORT -d "{\"method\":\"poe.vote\",\"params\":{\"submitId\":\"$SUBMIT_ID\",\"approve\":true}}")
if [ $? -ne 0 ]; then
  log_err "poe.vote failed"
  exit 1
fi
log_ok "poe.vote OK"

# Finalize
RESP=$(curl -s -X POST http://127.0.0.1:$RPC_PORT -d "{\"method\":\"poe.finalize\",\"params\":{\"submitId\":\"$SUBMIT_ID\"}}")
echo "$RESP" | grep -q "\"finalized\":true" || { log_err "poe.finalize failed: $RESP"; exit 1; }

# Parse reward_ngt and ensure > 0
REWARD=$(echo "$RESP" | sed -n 's/.*"reward_ngt"\s*:\s*\([0-9]*\).*/\1/p')
if [ -z "$REWARD" ]; then
  log_err "poe.finalize missing reward_ngt: $RESP"
  exit 1
fi
if [ "$REWARD" -le 0 ]; then
  log_err "poe.finalize reward_ngt <= 0: $REWARD"
  exit 1
fi
log_ok "poe.finalize OK (reward_ngt=$REWARD)"

# Check totalFinalized > 0
RESP=$(curl -s -X POST http://127.0.0.1:$RPC_PORT -d '{"method":"node.status"}')
TOTAL=$(echo "$RESP" | sed -n 's/.*"totalFinalized"\s*:\s*\([0-9]*\).*/\1/p')
if [ -z "$TOTAL" ] || [ "$TOTAL" -le 0 ]; then
  log_err "node.status totalFinalized <= 0 or missing: $RESP"
  exit 1
fi
log_ok "node.status OK (totalFinalized=$TOTAL)"

kill $NODE_PID 2>/dev/null || true
wait $NODE_PID 2>/dev/null || true
log_ok "Golden path E2E completed successfully"
exit 0
