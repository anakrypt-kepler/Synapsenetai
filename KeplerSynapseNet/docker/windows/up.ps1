# Builds the Linux image and starts Tor + node. Run from the repo root.
# Requires Docker Desktop (Linux containers / WSL2).
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
Set-Location $Root

Write-Host "Building keplersynapsenet:local (linux/amd64)..."
docker build -t keplersynapsenet:local .\KeplerSynapseNet
if ($LASTEXITCODE -ne 0) { throw "docker build failed" }

docker network inspect synapsenet-net 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) { docker network create synapsenet-net | Out-Null }

docker rm -f synapsenet-tor synapsenet-node 2>$null | Out-Null

docker run -d --name synapsenet-tor --network synapsenet-net --restart unless-stopped `
  --user debian-tor --entrypoint /usr/bin/tor `
  keplersynapsenet:local `
  --SocksPort 0.0.0.0:9050 --ControlPort 127.0.0.1:9051 --DataDirectory /tmp/tor-data --Log "notice stdout"

docker run -d --name synapsenet-node --network synapsenet-net --restart unless-stopped `
  -p 8332:8332 -p 8333:8333 `
  -e SYNAPSENET_PRIVACY=true `
  -e SYNAPSENET_TOR_MODE=external `
  -e SYNAPSENET_TOR_REQUIRED=true `
  -e SYNAPSENET_TOR_HOST=synapsenet-tor `
  -e SYNAPSENET_ALLOW_CLEARNET_FALLBACK=false `
  -e SYNAPSENET_ALLOW_P2P_CLEARNET_FALLBACK=false `
  -e SYNAPSENET_FORCE_CLEARNET_NAAN=false `
  -e SYNAPSENET_NAAN_AUTO_SEARCH_MODE=tor `
  -e SYNAPSENET_NO_FORK=true `
  -v "${Root}\KeplerSynapseNet\docker\synapsenet-start.sh:/usr/local/bin/synapsenet-start.sh:ro" `
  --entrypoint /usr/local/bin/synapsenet-start.sh `
  keplersynapsenet:local

Write-Host "Up. RPC 8332 / P2P 8333. Logs: docker logs -f synapsenet-node"
