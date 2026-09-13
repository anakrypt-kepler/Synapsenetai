#!/usr/bin/env bash
# Lego first-run for Linux. Numbered steps. Power users. Fetch everything
# we can without root; the one thing we cannot invent is a distro C++ compiler.
# Usage:
#   ./KeplerSynapseNet/scripts/lego-linux.sh
#   ./KeplerSynapseNet/scripts/lego-linux.sh --from 6 --until 10
#   ./KeplerSynapseNet/scripts/lego-linux.sh --skip-desktop
set -euo pipefail

FROM_STEP=1
UNTIL_STEP=13
SKIP_DESKTOP=0
SKIP_TOR=0
SKIP_TESTS=0
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() {
  cat <<'EOF'
SynapseNet Linux Lego — fetch deps, build node, optional desktop.

  --from N         start at step N (default 1)
  --until N        stop after step N (default 13)
  --jobs N         compile parallelism (default: nproc)
  --skip-desktop   skip WebKit sysroot + Tauri
  --skip-tor       skip Tor expert bundle
  --skip-tests     skip ctest after the node build
  -h, --help

Steps:
  1  detect Linux + required host tools (curl, tar, a C++ compiler)
  2  cmake / ninja / pkg-config into ~/.local
  3  Rust (rustup)
  4  Node.js LTS into ~/.local
  5  libsodium + sqlite (+ openssl/ncurses if headers missing) into ~/.local
  6  Tor expert bundle into ~/.local/tor
  7  WebKitGTK/GTK .deb sysroot into ~/.local/tauri-sysroot (no root)
  8  cmake configure (Release, privacy, tests)
  9  build synapsed + libsynapsed
 10  unit tests (private transfer / RingCT / ledger)
 11  desktop: npm + cargo --release
 12  install libs + launcher into ~/.synapsenet
 13  print the remaining human steps (Tor, wallet, node)

This is not a GUI installer. It is one script so you stop hunting packages.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --from) FROM_STEP="${2:?}"; shift 2 ;;
    --until) UNTIL_STEP="${2:?}"; shift 2 ;;
    --jobs) JOBS="${2:?}"; shift 2 ;;
    --skip-desktop) SKIP_DESKTOP=1; shift ;;
    --skip-tor) SKIP_TOR=1; shift ;;
    --skip-tests) SKIP_TESTS=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage >&2; exit 2 ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
PREFIX="${SYNAPSE_PREFIX:-$HOME/.local}"
SYSROOT="${SYNAPSE_TAURI_SYSROOT:-$HOME/.local/tauri-sysroot}"
TOR_DIR="${SYNAPSE_TOR_DIR:-$HOME/.local/tor}"
DATA_DIR="${SYNAPSE_DATA_DIR:-$HOME/.synapsenet}"
SRC_CACHE="${SYNAPSE_SRC_CACHE:-$HOME/.cache/synapsenet-lego}"
NODE_VER="${SYNAPSE_NODE_VER:-v22.18.0}"
CMAKE_VER="${SYNAPSE_CMAKE_VER:-3.30.5}"
NINJA_VER="${SYNAPSE_NINJA_VER:-1.12.1}"
SODIUM_VER="${SYNAPSE_SODIUM_VER:-1.0.20}"
SQLITE_YEAR="${SYNAPSE_SQLITE_YEAR:-2024}"
SQLITE_VER="${SYNAPSE_SQLITE_VER:-3460100}"
TOR_BUNDLE_VER="${SYNAPSE_TOR_BUNDLE_VER:-14.5.8}"

need_step() {
  local n="$1"
  [[ "$n" -ge "$FROM_STEP" && "$n" -le "$UNTIL_STEP" ]]
}

step() {
  local n="$1"
  shift
  echo
  echo "======== LEGO $n/13 — $* ========"
}

have() { command -v "$1" >/dev/null 2>&1; }

fetch() {
  local url="$1" out="$2"
  if [[ -f "$out" ]]; then
    echo "have $out"
    return 0
  fi
  echo "GET $url"
  curl -fL --retry 3 --retry-delay 2 -o "$out.partial" "$url"
  mv "$out.partial" "$out"
}

rewrite_pc_prefix() {
  local sysroot="$1"
  # Point pkg-config files at the extracted sysroot instead of /usr.
  find "$sysroot" -name '*.pc' -print0 2>/dev/null | while IFS= read -r -d '' pc; do
    python3 - "$pc" "$sysroot" <<'PY'
import pathlib, sys
pc = pathlib.Path(sys.argv[1])
sysroot = sys.argv[2].rstrip("/")
text = pc.read_text(errors="replace")
lines = []
for line in text.splitlines():
    if line.startswith("prefix=/usr"):
        line = "prefix=" + sysroot + "/usr"
    lines.append(line)
pc.write_text("\n".join(lines) + ("\n" if text.endswith("\n") else ""))
PY
  done
}

export PATH="$PREFIX/bin:$HOME/.cargo/bin:$PATH"
mkdir -p "$PREFIX/bin" "$PREFIX/lib" "$PREFIX/include" "$SRC_CACHE" "$DATA_DIR/lib"

# 1 detect
if need_step 1; then
  step 1 "detect host"
  if [[ "$(uname -s)" != "Linux" ]]; then
    echo "this script is Linux-only. Windows: WSL2, or see KeplerSynapseNet/docker/windows/" >&2
    exit 1
  fi
  for bin in curl tar uname; do
    have "$bin" || { echo "need $bin on PATH" >&2; exit 1; }
  done
  if ! have g++ && ! have clang++; then
    cat >&2 <<'EOF'
Need a C++17 compiler (g++ or clang++).
This script will fetch libraries, CMake, Node, Rust, Tor, and WebKit debs
into $HOME/.local without root. It cannot invent a system compiler.
Debian/Ubuntu (you type this, on a TTY):  sudo apt-get install -y build-essential
Fedora:  sudo dnf install -y gcc-c++ make
Arch:    sudo pacman -S --needed base-devel
Then re-run this script.
EOF
    exit 1
  fi
  have python3 || { echo "need python3" >&2; exit 1; }
  echo "compiler: $(g++ --version 2>/dev/null | head -1 || clang++ --version | head -1)"
  echo "prefix:   $PREFIX"
  echo "root:     $ROOT"
fi

# 2 cmake ninja pkg-config
if need_step 2; then
  step 2 "cmake / ninja / pkg-config"
  if ! have cmake; then
    local_cmake="$SRC_CACHE/cmake-${CMAKE_VER}-linux-x86_64.tar.gz"
    fetch "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VER}/cmake-${CMAKE_VER}-linux-x86_64.tar.gz" "$local_cmake"
    tar -C "$SRC_CACHE" -xzf "$local_cmake"
    cp -a "$SRC_CACHE/cmake-${CMAKE_VER}-linux-x86_64/bin/." "$PREFIX/bin/"
    mkdir -p "$PREFIX/share"
    cp -a "$SRC_CACHE/cmake-${CMAKE_VER}-linux-x86_64/share/cmake-"* "$PREFIX/share/" 2>/dev/null || true
  fi
  cmake --version | head -1
  if ! have ninja; then
    zip="$SRC_CACHE/ninja-linux-${NINJA_VER}.zip"
    fetch "https://github.com/ninja-build/ninja/releases/download/v${NINJA_VER}/ninja-linux.zip" "$zip"
    python3 - "$zip" "$PREFIX/bin" <<'PY'
import zipfile, sys, os, stat
z = zipfile.ZipFile(sys.argv[1])
z.extract("ninja", sys.argv[2])
p = os.path.join(sys.argv[2], "ninja")
os.chmod(p, os.stat(p).st_mode | stat.S_IEXEC)
PY
  fi
  ninja --version
  if ! have pkg-config && ! have pkgconf; then
    fetch "https://distfiles.ariadne.space/pkgconf/pkgconf-2.3.0.tar.xz" "$SRC_CACHE/pkgconf-2.3.0.tar.xz" || true
    if [[ -f "$SRC_CACHE/pkgconf-2.3.0.tar.xz" ]]; then
      tar -C "$SRC_CACHE" -xf "$SRC_CACHE/pkgconf-2.3.0.tar.xz"
      ( cd "$SRC_CACHE/pkgconf-2.3.0" && ./configure --prefix="$PREFIX" --disable-static && make -j"$JOBS" && make install )
      ln -sfn "$PREFIX/bin/pkgconf" "$PREFIX/bin/pkg-config"
    else
      echo "WARN: could not fetch pkgconf; cmake may still work if pkg-config exists later" >&2
    fi
  fi
fi

# 3 rust
if need_step 3; then
  step 3 "Rust toolchain"
  if ! have rustc || ! have cargo; then
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal
    # shellcheck disable=SC1091
    source "$HOME/.cargo/env"
  fi
  rustc --version
  cargo --version
fi

# 4 node
if need_step 4; then
  step 4 "Node.js ${NODE_VER}"
  if ! have node || ! have npm; then
    arch="$(uname -m)"
    case "$arch" in
      x86_64) narch=x64 ;;
      aarch64|arm64) narch=arm64 ;;
      *) echo "unsupported arch for Node tarball: $arch" >&2; exit 1 ;;
    esac
    tarname="node-${NODE_VER}-linux-${narch}.tar.xz"
    tarball="$SRC_CACHE/$tarname"
    fetch "https://nodejs.org/dist/${NODE_VER}/$tarname" "$tarball"
    tar -C "$SRC_CACHE" -xf "$tarball"
    cp -a "$SRC_CACHE/node-${NODE_VER}-linux-${narch}/." "$PREFIX/"
  fi
  node --version
  npm --version
fi

# 5 C libraries
if need_step 5; then
  step 5 "C libraries into $PREFIX"
  export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"

  if [[ ! -f "$PREFIX/include/sodium.h" ]]; then
    tarball="$SRC_CACHE/libsodium-${SODIUM_VER}.tar.gz"
    fetch "https://download.libsodium.org/libsodium/releases/libsodium-${SODIUM_VER}.tar.gz" "$tarball"
    tar -C "$SRC_CACHE" -xzf "$tarball"
    ( cd "$SRC_CACHE/libsodium-${SODIUM_VER}" && ./configure --prefix="$PREFIX" --disable-static && make -j"$JOBS" && make install )
  fi

  if [[ ! -f "$PREFIX/include/sqlite3.h" ]]; then
    tarball="$SRC_CACHE/sqlite-autoconf-${SQLITE_VER}.tar.gz"
    fetch "https://www.sqlite.org/${SQLITE_YEAR}/sqlite-autoconf-${SQLITE_VER}.tar.gz" "$tarball" \
      || fetch "https://www.sqlite.org/2026/sqlite-autoconf-3510100.tar.gz" "$SRC_CACHE/sqlite-autoconf-3510100.tar.gz"
    sqdir="$(ls -d "$SRC_CACHE"/sqlite-autoconf-* | tail -1)"
    ( cd "$sqdir" && ./configure --prefix="$PREFIX" --disable-static && make -j"$JOBS" && make install )
  fi

  if [[ ! -f /usr/include/openssl/ssl.h && ! -f "$PREFIX/include/openssl/ssl.h" ]]; then
    echo "OpenSSL headers missing; fetching OpenSSL 3.3.2 into $PREFIX"
    tarball="$SRC_CACHE/openssl-3.3.2.tar.gz"
    fetch "https://www.openssl.org/source/openssl-3.3.2.tar.gz" "$tarball"
    tar -C "$SRC_CACHE" -xzf "$tarball"
    ( cd "$SRC_CACHE/openssl-3.3.2" && ./config --prefix="$PREFIX" --openssldir="$PREFIX/ssl" shared zlib-dynamic && make -j"$JOBS" && make install_sw )
  fi

  if [[ ! -f /usr/include/ncurses.h && ! -f /usr/include/ncurses/ncurses.h && ! -f "$PREFIX/include/ncurses.h" ]]; then
    echo "ncurses headers missing; fetching ncurses 6.5 into $PREFIX"
    tarball="$SRC_CACHE/ncurses-6.5.tar.gz"
    fetch "https://ftp.gnu.org/gnu/ncurses/ncurses-6.5.tar.gz" "$tarball"
    tar -C "$SRC_CACHE" -xzf "$tarball"
    ( cd "$SRC_CACHE/ncurses-6.5" && ./configure --prefix="$PREFIX" --with-shared --without-debug --enable-widec --enable-pc-files --with-pkg-config-libdir="$PREFIX/lib/pkgconfig" && make -j"$JOBS" && make install )
  fi

  pkg-config --modversion libsodium || true
  echo "sodium/sqlite headers ok"
fi

# 6 Tor
if need_step 6; then
  step 6 "Tor expert bundle"
  if [[ "$SKIP_TOR" -eq 1 ]]; then
    echo "skipped (--skip-tor)"
  elif have tor && [[ "$(tor --version 2>/dev/null | head -1)" == Tor* ]]; then
    echo "system tor: $(tor --version | head -1)"
  else
    arch="$(uname -m)"
    case "$arch" in
      x86_64) tarch=linux-x86_64 ;;
      aarch64|arm64) tarch=linux-aarch64 ;;
      *) echo "no expert bundle arch mapping for $arch; install tor from the distro" >&2; tarch="" ;;
    esac
    if [[ -n "$tarch" ]]; then
      tarname="tor-expert-bundle-${tarch}-${TOR_BUNDLE_VER}.tar.gz"
      tarball="$SRC_CACHE/$tarname"
      url="https://www.torproject.org/dist/torbrowser/${TOR_BUNDLE_VER}/$tarname"
      fetch "$url" "$tarball" || {
        echo "WARN: Tor bundle download failed. Install tor yourself (not Tor Browser)." >&2
      }
      if [[ -f "$tarball" ]]; then
        mkdir -p "$TOR_DIR"
        tar -C "$TOR_DIR" -xzf "$tarball"
        # Expert bundle layout: tor/tor and sometimes pluggable_transports/lyrebird
        if [[ -x "$TOR_DIR/tor/tor" ]]; then
          ln -sfn "$TOR_DIR/tor/tor" "$PREFIX/bin/tor"
        fi
        echo "tor at $PREFIX/bin/tor"
      fi
    fi
  fi
fi

# 7 WebKit sysroot
if need_step 7; then
  step 7 "WebKit/GTK sysroot (no root)"
  if [[ "$SKIP_DESKTOP" -eq 1 ]]; then
    echo "skipped (--skip-desktop)"
  elif ! have apt-get || ! have dpkg-deb; then
    echo "need apt-get and dpkg-deb for the no-root WebKit sysroot, or install WebKitGTK from the distro"
  else
    debdir="$SRC_CACHE/tauri-debs"
    mkdir -p "$debdir" "$SYSROOT"
    pkgfile="$SCRIPT_DIR/lego-linux-webkit.pkgs"
    # apt-get download does not need root. Failed names are skipped.
    (
      cd "$debdir"
      while read -r pkg; do
        [[ -z "$pkg" || "$pkg" =~ ^# ]] && continue
        if ls "${pkg}_"*.deb >/dev/null 2>&1; then
          continue
        fi
        if apt-get download "$pkg" >/dev/null 2>&1; then
          echo "deb $pkg"
        else
          echo "skip $pkg (no candidate)"
        fi
      done < "$pkgfile"
    )
    shopt -s nullglob
    for deb in "$debdir"/*.deb; do
      dpkg-deb -x "$deb" "$SYSROOT"
    done
    shopt -u nullglob
    rewrite_pc_prefix "$SYSROOT"
    echo "sysroot $SYSROOT"
  fi
fi

# Shared build env for cmake (never leak homemade sqlite into Node via LD_LIBRARY_PATH)
CMAKE_PREFIX_PATH="$PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"

# 8 configure
if need_step 8; then
  step 8 "cmake configure"
  cmake -S "$ROOT" -B "$ROOT/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DUSE_LLAMA_CPP=ON \
    -DUSE_SECP256K1=ON \
    -DBUILD_PRIVACY=ON \
    -DBUILD_TESTS=ON
fi

# 9 build
if need_step 9; then
  step 9 "build synapsed + libsynapsed"
  cmake --build "$ROOT/build" --target synapsed synapsed_lib -j"$JOBS"
fi

# 10 tests
if need_step 10; then
  step 10 "tests"
  if [[ "$SKIP_TESTS" -eq 1 ]]; then
    echo "skipped (--skip-tests)"
  else
    cmake --build "$ROOT/build" --target test_private_transfer test_transfer test_ringct test_privacy -j"$JOBS"
    ctest --test-dir "$ROOT/build" --output-on-failure -R 'TransferTests|PrivateTransferTests|RingCtLedgerTests|PrivacyTests'
  fi
fi

# 11 desktop
if need_step 11; then
  step 11 "desktop (npm + cargo --release)"
  if [[ "$SKIP_DESKTOP" -eq 1 ]]; then
    echo "skipped (--skip-desktop)"
  else
    unset LD_LIBRARY_PATH PKG_CONFIG_LIBDIR || true
    export PATH="$HOME/.cargo/bin:$PREFIX/bin:$PATH"
    pc_sys=""
    if [[ -d "$SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig" ]]; then
      pc_sys="$SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig"
    elif [[ -d "$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig" ]]; then
      pc_sys="$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig"
    fi
    export PKG_CONFIG_PATH="/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig${pc_sys:+:$pc_sys}:$PREFIX/lib/pkgconfig"
    cd "$ROOT/tauri-app"
    if [[ -f package-lock.json ]]; then
      npm ci
    else
      npm install
    fi
    npm run build
    cd "$ROOT/tauri-app/src-tauri"
    cargo build --release
  fi
fi

# 12 install
if need_step 12; then
  step 12 "install libs + launcher"
  mkdir -p "$DATA_DIR/lib"
  shopt -s nullglob
  for so in "$ROOT/build"/libsynapsed.so* "$ROOT/build/bin"/libggml*.so* "$ROOT/build/bin"/libllama.so* \
            "$ROOT/build/_deps/liboqs-build/lib"/liboqs.so* "$ROOT/build/_deps/secp256k1-build/lib"/libsecp256k1.so*; do
    [[ -e "$so" ]] && cp -a "$so" "$DATA_DIR/lib/"
  done
  shopt -u nullglob
  cat > "$DATA_DIR/run-desktop.sh" <<EOF
#!/usr/bin/env bash
export SYNAPSE_ROOT=$(printf '%q' "$ROOT")
exec $(printf '%q' "$SCRIPT_DIR/run-desktop.sh") "\$@"
EOF
  chmod +x "$DATA_DIR/run-desktop.sh"
  if [[ ! -f "$DATA_DIR/synapsenet.conf" && -f "$ROOT/synapsenet.conf.example" ]]; then
    cp "$ROOT/synapsenet.conf.example" "$DATA_DIR/synapsenet.conf"
    echo "wrote $DATA_DIR/synapsenet.conf from example (Tor required)"
  fi
  echo "libs in $DATA_DIR/lib"
fi

# 13 human steps
if need_step 13; then
  step 13 "what you still do by hand"
  cat <<EOF
Numbered remainder (this script does not create your wallet or start Tor for you):

  A. Tor — standalone daemon on 9050, not Tor Browser.
       tor -f /dev/null --SocksPort 9050
     or:  $PREFIX/bin/tor --SocksPort 9050
     Confirm: ss -ltn | grep 9050

  B. Wallet — first launch of the desktop app, or restore a mnemonic.
       $DATA_DIR/run-desktop.sh
     Stealth address is SN + 128 hex. Default send is private RingCT.
     Clearnet in settings is NOT PRIVATE. Leave it off.

  C. Node / mining — desktop holds libsynapsed in-process.
     Do not also run ./build/synapsed against the same data dir.

Honesty: stealth + MLSAG-2 + hidden amounts + range proofs + key images + Tor.
Not full Monero. No bulletproofs yet. Full-node rewards are still transparent.

Cache: $SRC_CACHE
Prefix: $PREFIX
EOF
fi
