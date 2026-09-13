#!/usr/bin/env bash
# Portable desktop launcher. Installed to ~/.synapsenet/run-desktop.sh by lego-linux.sh.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# When this file lives in the repo: KeplerSynapseNet/scripts/run-desktop.sh
# When installed: ~/.synapsenet/run-desktop.sh and ROOT is discovered from sibling marker.
if [[ -f "$SCRIPT_DIR/../CMakeLists.txt" ]]; then
  ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
elif [[ -n "${SYNAPSE_ROOT:-}" ]]; then
  ROOT="$SYNAPSE_ROOT"
else
  ROOT="${SYNAPSE_ROOT:-}"
  if [[ -z "$ROOT" || ! -f "$ROOT/CMakeLists.txt" ]]; then
    echo "set SYNAPSE_ROOT to KeplerSynapseNet (the folder with CMakeLists.txt)" >&2
    exit 1
  fi
fi
export DISPLAY="${DISPLAY:-:0}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WEBKIT_DISABLE_DMABUF_RENDERER="${WEBKIT_DISABLE_DMABUF_RENDERER:-1}"
export LD_LIBRARY_PATH="${HOME}/.synapsenet/lib:${HOME}/.local/lib:${ROOT}/build/bin:${ROOT}/build/_deps/liboqs-build/lib:${ROOT}/build/_deps/secp256k1-build/lib:${LD_LIBRARY_PATH:-}"
export SYNAPSED_LIB_PATH="${HOME}/.synapsenet/lib"
export PATH="${HOME}/.local/bin:${HOME}/.cargo/bin:${PATH}"
# SIGKILL'd GUIs leave session Tor on one DataDirectory and freeze the mesh.
# Match tor's argv only — never pkill -f the launcher command line.
mapfile -t _tors < <(pgrep -f '^/usr/sbin/tor -f .*/\.synapsenet/session-tor/torrc' || true)
if ((${#_tors[@]})); then
  kill "${_tors[@]}" 2>/dev/null || true
  sleep 0.3
fi
pkill -f "$ROOT/build/synapsed" 2>/dev/null || true
sleep 0.4
BIN="$ROOT/tauri-app/src-tauri/target/release/synapsenet-app"
if [[ ! -x "$BIN" ]]; then
  BIN="$ROOT/tauri-app/src-tauri/target/debug/synapsenet-app"
fi
if [[ ! -x "$BIN" ]]; then
  echo "desktop binary missing. run KeplerSynapseNet/scripts/lego-linux.sh --from 11" >&2
  exit 1
fi
exec "$BIN" "$@"
