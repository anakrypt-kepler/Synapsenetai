#!/bin/bash

echo "Testing SynapseNet TUI..."
echo "Terminal: $TERM"
echo "Columns: $(tput cols)"
echo "Lines: $(tput lines)"

# Set proper terminal environment
export TERM=xterm-256color

# Test if terminal is interactive
if [ -t 0 ] && [ -t 1 ]; then
    echo "Terminal is interactive"
else
    echo "Terminal is not interactive - TUI may not work"
fi

echo "Starting SynapseNet..."
timeout 10s ./KeplerSynapseNet/build/synapsed || echo "Program exited or timed out"