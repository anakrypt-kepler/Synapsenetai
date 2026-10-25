# Node image: synapsed daemon and the PoE mesh validator.
# Not the Tauri desktop app. Published tag: ghcr.io/anakrypt-kepler/synapsenetai:0.2.0-alpha
FROM ubuntu:24.04 AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG CMAKE_BUILD_TYPE=Release
ARG USE_LLAMA_CPP=ON
ARG BUILD_TESTS=ON
ARG TARGETARCH
ARG BUILDPLATFORM
ARG TARGETPLATFORM

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    git \
    cmake \
    ninja-build \
    pkg-config \
    build-essential \
    python3 \
    libssl-dev \
    libncurses-dev \
    libsqlite3-dev \
    libsodium-dev \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# liboqs and secp256k1 are fetched when absent from third_party/.
# GGML_NATIVE stays off so the published image runs on more than the build CPU.
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
      -DUSE_LLAMA_CPP="${USE_LLAMA_CPP}" \
      -DUSE_SECP256K1=ON \
      -DBUILD_TESTS="${BUILD_TESTS}" \
      -DBUILD_PRIVACY=ON \
      -DBUILD_TUI=OFF \
      -DSYNAPSE_FETCH_LIBOQS=ON \
      -DSYNAPSE_REQUIRE_REAL_PQC=ON \
      -DGGML_NATIVE=OFF \
  && cmake --build build --parallel 2 \
  && if [ "${BUILD_TESTS}" = "ON" ]; then ctest --test-dir build --output-on-failure -LE integration; fi \
  && mkdir -p /opt/export/bin /opt/export/lib \
  && if [ -x build/synapsed ]; then syn=build/synapsed; else syn=$(find build -type f -name synapsed -executable | head -n 1); fi \
  && if [ -x build/synapsed-poe-mesh ]; then mesh=build/synapsed-poe-mesh; else mesh=$(find build -type f -name synapsed-poe-mesh -executable | head -n 1); fi \
  && test -n "$syn" \
  && cp -a "$syn" /opt/export/bin/synapsed \
  && if [ -n "$mesh" ]; then cp -a "$mesh" /opt/export/bin/synapsed-poe-mesh; fi \
  && find build -type f -name '*.so*' -exec cp -a {} /opt/export/lib/ \;

FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
ARG TARGETARCH
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libgcc-s1 \
    libgomp1 \
    libncurses6 \
    libsqlite3-0 \
    libssl3 \
    libstdc++6 \
    libsodium23 \
    tor \
    tor-geoipdb \
    obfs4proxy \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /opt/export /opt/export
RUN set -eux; \
    if [ -n "$(ls -A /opt/export/lib 2>/dev/null)" ]; then \
      cp -a /opt/export/lib/. /usr/local/lib/; \
      ldconfig; \
    fi; \
    cp /opt/export/bin/synapsed /app/synapsed; \
    if [ -f /opt/export/bin/synapsed-poe-mesh ]; then \
      cp /opt/export/bin/synapsed-poe-mesh /app/synapsed-poe-mesh; \
    fi; \
    rm -rf /opt/export

LABEL org.opencontainers.image.source="https://github.com/anakrypt-kepler/Synapsenetai" \
      org.opencontainers.image.title="synapsenetai" \
      org.opencontainers.image.description="SynapseNet headless node and PoE mesh validator" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.version="0.2.0-alpha"

COPY synapsenet.conf.example /etc/synapsenet/synapsenet.conf.example
COPY docker/synapsenet-start.sh /usr/local/bin/synapsenet-start.sh
RUN chmod +x /usr/local/bin/synapsenet-start.sh \
    && mkdir -p /data

EXPOSE 8332 8333
VOLUME ["/data"]
ENTRYPOINT ["/usr/local/bin/synapsenet-start.sh"]
