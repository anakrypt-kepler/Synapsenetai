#!/usr/bin/env bash
# Same plumbing as docker/windows/up.ps1, for a Linux host without `docker compose`.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
echo "Building keplersynapsenet:local..."
docker build -t keplersynapsenet:local "$ROOT"
docker network inspect synapsenet-net >/dev/null 2>&1 || docker network create synapsenet-net
docker rm -f synapsenet-tor synapsenet-node >/dev/null 2>&1 || true
docker run -d --name synapsenet-tor --network synapsenet-net --restart unless-stopped \
  --user debian-tor --entrypoint /usr/bin/tor \
  keplersynapsenet:local \
  --SocksPort 0.0.0.0:9050 --ControlPort 127.0.0.1:9051 --DataDirectory /tmp/tor-data --Log "notice stdout"
docker run -d --name synapsenet-node --network synapsenet-net --restart unless-stopped \
  -p 8332:8332 -p 8333:8333 \
  -e SYNAPSENET_PRIVACY=true \
  -e SYNAPSENET_TOR_MODE=external \
  -e SYNAPSENET_TOR_REQUIRED=true \
  -e SYNAPSENET_TOR_HOST=synapsenet-tor \
  -e SYNAPSENET_ALLOW_CLEARNET_FALLBACK=false \
  -e SYNAPSENET_ALLOW_P2P_CLEARNET_FALLBACK=false \
  -e SYNAPSENET_FORCE_CLEARNET_NAAN=false \
  -e SYNAPSENET_NAAN_AUTO_SEARCH_MODE=tor \
  -e SYNAPSENET_NO_FORK=true \
  -v "$ROOT/docker/synapsenet-start.sh:/usr/local/bin/synapsenet-start.sh:ro" \
  --entrypoint /usr/local/bin/synapsenet-start.sh \
  keplersynapsenet:local
echo "Up. RPC 8332. docker logs -f synapsenet-node"
