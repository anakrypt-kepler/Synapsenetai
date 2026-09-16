#!/bin/bash
# Smoke the TUI binary. Run from anywhere.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Testing SynapseNet TUI..."
echo "Terminal: $TERM"
echo "Columns: $(tput cols)"
echo "Lines: $(tput lines)"

export TERM=xterm-256color

if [ -t 0 ] && [ -t 1 ]; then
    echo "Terminal is interactive"
else
    echo "Terminal is not interactive - TUI may not work"
fi

echo "Starting SynapseNet..."
timeout 10s "$ROOT/build/synapsed" || echo "Program exited or timed out"
