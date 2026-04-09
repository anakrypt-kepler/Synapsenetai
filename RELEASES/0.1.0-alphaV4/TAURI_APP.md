# SynapseNet Desktop App — Build & Run

Phase 5 of the 0.1.0-alphaV4 migration. Tauri desktop application wrapping libsynapsed.

## Prerequisites

- Rust toolchain (stable)
- Node.js >= 18
- npm
- System libraries:
  - **Linux:** `libwebkit2gtk-4.1-dev libappindicator3-dev librsvg2-dev patchelf`
  - **macOS:** Xcode Command Line Tools
  - **Windows:** WebView2 (bundled with Windows 10+)
- libsynapsed shared library (built from C++ engine)

## Build libsynapsed

```bash
cd KeplerSynapseNet
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target synapsed_lib --parallel
```

The shared library is output as:
- Linux: `build/libsynapsed.so`
- macOS: `build/libsynapsed.dylib`
- Windows: `build/synapsed.dll`

## Build the Tauri App

```bash
cd KeplerSynapseNet/tauri-app

# Install frontend dependencies
npm ci

# Build frontend
npm run build

# Build Tauri app (release)
cd src-tauri
cargo build --release
```

The binary is output at `src-tauri/target/release/synapsenet-app`.

## Run

Set `SYNAPSED_LIB_PATH` to the directory containing the shared library, or place it alongside the binary.

```bash
# Option 1: environment variable
export SYNAPSED_LIB_PATH=/path/to/build
./src-tauri/target/release/synapsenet-app

# Option 2: copy library next to binary
cp /path/to/build/libsynapsed.so src-tauri/target/release/
./src-tauri/target/release/synapsenet-app
```

## Development Mode

```bash
cd KeplerSynapseNet/tauri-app
npm run dev          # starts Vite dev server on :5173
cd src-tauri
cargo tauri dev      # launches Tauri with hot reload
```

## Data Directory

All data is stored in `~/.synapsenet/`. The Tauri app reads configuration from `~/.synapsenet/config.toml`.

On first launch (when `config.toml` does not exist), a setup wizard guides through wallet creation, connection type, AI model selection, and resource configuration.

## Coexistence

Both interfaces run against the same engine:

| Interface | Binary | Mode |
|-----------|--------|------|
| Terminal TUI | `synapsed` | Headless / server / ncurses |
| Desktop GUI | `synapsenet-app` | Tauri window |

The terminal TUI is unchanged. Both share `~/.synapsenet/` and the same libsynapsed FFI.

## Tech Stack

- **Frontend:** TypeScript + Svelte 4 + Monaco Editor
- **Backend shell:** Rust (Tauri 2)
- **Engine:** C++ (libsynapsed via FFI)
- **Build:** CMake (C++) + Cargo (Rust) + Vite (frontend)
