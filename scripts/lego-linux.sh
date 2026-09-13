#!/usr/bin/env bash
# Thin wrapper so the repo root has a Lego entry point.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$ROOT/KeplerSynapseNet/scripts/lego-linux.sh" "$@"
