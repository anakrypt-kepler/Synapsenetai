# Node image: synapsed CLI/daemon (not the Tauri desktop app).
FROM ubuntu:24.04 AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG CMAKE_BUILD_TYPE=Release
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
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
      -DUSE_LLAMA_CPP=ON \
      -DUSE_SECP256K1=ON \
      -DBUILD_TESTS=ON \
      -DBUILD_PRIVACY=ON \
      -DBUILD_TUI=OFF \
      -DSYNAPSE_FETCH_LIBOQS=ON \
      -DSYNAPSE_REQUIRE_REAL_PQC=ON \
  && cmake --build build --parallel \
  && ctest --test-dir build --output-on-failure -LE integration

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
COPY --from=build /src/build /tmp/build
RUN set -eux; \
    cp -a /tmp/build/bin/libggml*.so* /usr/local/lib/; \
    cp -a /tmp/build/bin/libllama.so* /usr/local/lib/; \
    find /tmp/build -type f \( -name 'libsecp256k1.so*' -o -name 'liboqs.so*' \) -exec cp -a {} /usr/local/lib/ \; ; \
    ldconfig; \
    cp /tmp/build/synapsed /app/synapsed; \
    rm -rf /tmp/build

COPY synapsenet.conf.example /etc/synapsenet/synapsenet.conf.example
COPY docker/synapsenet-start.sh /usr/local/bin/synapsenet-start.sh
RUN chmod +x /usr/local/bin/synapsenet-start.sh \
    && mkdir -p /data

EXPOSE 8332 8333
VOLUME ["/data"]
ENTRYPOINT ["/usr/local/bin/synapsenet-start.sh"]
