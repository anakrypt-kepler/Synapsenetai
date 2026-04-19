# SynapseNet 0.1 (v0.1.0-beta)

Release date: 2026-02-04

## Highlights

- `synapsed` node core with networking, ledger/wallet scaffolding, and Proof of Emergence (PoE) primitives
- TUI dashboard + screens for mining, knowledge, models, wallet, network, and settings
- Local model plumbing (download/loader/inference pipeline) and remote model query test coverage
- Privacy subsystem scaffolding (Tor/onion routing modules, traffic analysis defenses, stealth address primitives)
- SynapseIDE surfaces bundled:
  - `ide/synapsenet-vscode` (VS Code extension)
  - `crush-main` (terminal UI / patch-mode / threads surface)
- Design docs shipped under `interfaces txt/` (NAAN + Implant AI stack + updated PoE TODOs)

## Build

Core build (macOS/Linux):

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUSE_LLAMA_CPP=OFF -DBUILD_TESTS=ON`
- `cmake --build build --parallel`
- `ctest --test-dir build --output-on-failure`

## Notes

- CI builds both "without llama.cpp" (fast) and "with llama.cpp" (slower) on Linux.
