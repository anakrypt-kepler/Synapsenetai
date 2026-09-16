## Docker

Linux image. Windows hosts run this same Linux image (Docker Desktop or WSL2).
Kepler writes native Windows notes in `docker/windows/KEPLER_WRITE_THIS.txt`.

Default compose is **Tor + privacy**. Clearnet is an explicit lab overlay.

### Build (single-arch)

```bash
cd KeplerSynapseNet
docker build -t keplersynapsenet:local .
```

### Build (multi-arch)

```bash
cd KeplerSynapseNet
docker buildx build --platform linux/amd64,linux/arm64 -t keplersynapsenet:local --load .
```

### Run (privacy / Tor)

```bash
cd KeplerSynapseNet
docker compose up --build
```

Tor is a sidecar. The node talks to it as hostname `tor:9050`. No clearnet fallback.

### Run (local lab, not private)

```bash
cd KeplerSynapseNet
docker compose -f docker-compose.yml -f docker-compose.dev.yml up --build
```

Self-bootstrap, novelty bands off, clearnet NAAN. Do not call this private.

### Windows host

- Supported: WSL2 Ubuntu, then `KeplerSynapseNet/scripts/lego-linux.sh`.
- Docker Desktop: `KeplerSynapseNet/docker/windows/up.ps1` (Linux image, Tor required).
- Compose overlay: from `KeplerSynapseNet/`, `docker compose -f docker-compose.yml -f docker-compose.windows.yml up --build`
- Native Windows notes: `KeplerSynapseNet/docker/windows/KEPLER_WRITE_THIS.txt`.

### One-off

```bash
docker run --rm -it -p 8332:8332 keplersynapsenet:local
```
