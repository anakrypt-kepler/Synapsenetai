FROM --platform=$BUILDPLATFORM ubuntu:24.04 AS build

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

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
      -DUSE_LLAMA_CPP=ON \
      -DUSE_SECP256K1=ON \
      -DBUILD_TESTS=ON \
      -DBUILD_PRIVACY=OFF \
  && cmake --build build --parallel \
  && ctest --test-dir build --output-on-failure -LE integration

FROM --platform=$TARGETPLATFORM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
ARG TARGETARCH
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libgomp1 \
    libncurses6 \
    libsqlite3-0 \
    libsodium23 \
    tor \
    tor-geoipdb \
    obfs4proxy \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build /tmp/build
RUN set -eux; \
    cp /tmp/build/bin/libggml*.so* /usr/local/lib/; \
    cp /tmp/build/bin/libllama.so* /usr/local/lib/; \
    find /tmp/build -type f -name 'libsecp256k1.so*' -exec cp {} /usr/local/lib/ \; ; \
    ldconfig; \
    cp /tmp/build/synapsed /app/synapsed; \
    rm -rf /tmp/build

COPY synapsenet.conf.example /etc/synapsenet/synapsenet.conf

EXPOSE 8332 8333
ENTRYPOINT ["/app/synapsed", "--privacy", \
  "--seednode", "nv2b7cjwjzwrnwtrdaniogtnjkly6lcapg7ubkcou5pppzdcc2ki7cid.onion:8333", \
  "--seednode", "ny6duwaudeb76ym5zhtet2qtc5fmbkx7zp3pz7dlbroibj6jh5s2acqd.onion:8333"]
