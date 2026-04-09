## Docker build (fallback, cross-host)

This produces the **Linux** binary (`synapsed`) inside a container.
It is meant as a reproducible fallback for **Windows/macOS/Linux** users.

### Build (single-arch)

```bash
cd KeplerSynapseNet
docker build -t keplersynapsenet:local .
```

### Build (multi-arch, recommended)

```bash
cd KeplerSynapseNet
docker buildx build --platform linux/amd64,linux/arm64 -t keplersynapsenet:local --load .
```

### Run

```bash
docker run --rm -it -p 8332:8332 keplersynapsenet:local
```

### Windows (official)

- Use **WSL2** for native builds (Linux toolchain).
- If WSL2 build fails, use Docker as fallback.

