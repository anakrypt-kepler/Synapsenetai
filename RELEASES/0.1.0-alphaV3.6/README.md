<h1 align="center">SynapseNet 0.1.0-alphaV3.6</h1>

<p align="center"><strong>Modularize main.cpp -- Separation of Concerns</strong></p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV3.6-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/Refactor-main.cpp_modularization-000000?style=for-the-badge&labelColor=000000" alt="Refactor" />
  <img src="https://img.shields.io/badge/Tests-267_passed-000000?style=for-the-badge&labelColor=000000" alt="Tests" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" /></a>
  <a href="https://github.com/anakrypt/SynapseNet"><img src="https://img.shields.io/badge/Documentation-000000?style=for-the-badge&logo=gitbook&logoColor=white" alt="Docs" /></a>
  <a href="https://github.com/anakrypt/SynapseNet/blob/main/SynapseNet_Whitepaper.pdf"><img src="https://img.shields.io/badge/Whitepaper-000000?style=for-the-badge&logo=adobeacrobatreader&logoColor=white" alt="Whitepaper" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV3.5"><img src="https://img.shields.io/badge/←_0.1.0--alphaV3.5-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="V3.5" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV3.7"><img src="https://img.shields.io/badge/0.1.0--alphaV3.7_→-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V3.7" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV4"><img src="https://img.shields.io/badge/0.1.0--alphaV4_→-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V4" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES"><img src="https://img.shields.io/badge/All_Releases-000000?style=for-the-badge&logo=github&logoColor=white" alt="All Releases" /></a>
</p>

---

> V3.6 is a zero-behavior-change refactoring release. The 4,809-line `main.cpp` -- which contained the entire SynapseNet class definition, all runtime logic, RPC handlers, Tor management, NAAN coordination, PoE handlers, and message routing -- has been split into a dedicated translation unit. `main.cpp` is now 117 lines: signal setup, banner, and the `main()` entry point. Every feature, every code path, every test passes identically.

---

## The Problem

`main.cpp` was 4,809 lines. It contained:

- The complete `SynapseNet` class definition (dozens of member variables, hundreds of methods)
- Four `.inc` files textually included inside the class body (`tor_management.inc`, `naan_coordination.inc`, `poe_handlers.inc`, `net_message_handlers.inc`)
- All global state (`g_running`, `g_reloadConfig`, `g_daemonMode`, `g_shutdownSignal`)
- Signal handler and ASCII banner
- The `main()` function itself

This made `main.cpp` one of the largest single-file translation units in the project. Any change to orchestration, networking, Tor control, RPC dispatch, or consensus handling required editing the same file. IDE indexing was slow. Merge conflicts were frequent. Code review on any node-level change meant scrolling through thousands of unrelated lines.

## The Fix

Three new/modified files replace the monolith:

1. **`include/node/synapse_net.h`** -- Minimal public header. Forward-declares `SynapseNet`, exposes factory functions and lifecycle helpers. No transitive includes beyond `node_config.h` and standard library headers.

2. **`src/node/synapse_net.cpp`** -- Contains the full `SynapseNet` class definition and all method implementations (including the four `.inc` files). This is where the bulk of the code lives now.

3. **`src/main.cpp`** -- Reduced to 117 lines. Signal handler, banner, argument parsing, and `main()`. Interacts with `SynapseNet` exclusively through the factory API.

## What Changed

### `main.cpp` -- From 4,809 lines to 117

Before:
```
main.cpp:
  lines 1-113     system includes
  lines 114-170   global state, signal handler, banner
  lines 171-4700  class SynapseNet { ... } (entire class + .inc files)
  lines 4700-4809 main() function
```

After:
```
main.cpp:
  lines 1-11      includes (6 headers)
  lines 14-25     signalHandler()
  lines 27-39     printBanner()
  lines 43-116    main()
```

`main.cpp` no longer knows the internal structure of `SynapseNet`. It creates, initializes, runs, and shuts down the node through five free functions declared in `synapse_net.h`.

### `include/node/synapse_net.h` -- Opaque Interface

```cpp
namespace synapse {

extern std::atomic<bool> g_running;
extern std::atomic<bool> g_reloadConfig;
extern std::atomic<bool> g_daemonMode;
extern std::atomic<int>  g_shutdownSignal;

class SynapseNet;  // forward declaration only

struct SynapseNetDeleter {
    void operator()(SynapseNet* p) const;
};
using SynapseNetPtr = std::unique_ptr<SynapseNet, SynapseNetDeleter>;

SynapseNetPtr createSynapseNet();
bool initializeSynapseNet(SynapseNet& node, const NodeConfig& config);
int  runSynapseNetCommand(SynapseNet& node, const std::vector<std::string>& args);
int  runSynapseNet(SynapseNet& node);
void shutdownSynapseNet(SynapseNet& node);

} // namespace synapse
```

The custom deleter (`SynapseNetDeleter`) is required because `std::unique_ptr`'s default deleter needs a complete type at the point where the destructor is instantiated. Since `main.cpp` only has a forward declaration, the deleter implementation lives in `synapse_net.cpp` where the full class is visible.

### `src/node/synapse_net.cpp` -- The Class

All ~4,700 lines of the `SynapseNet` class definition moved here unchanged, plus factory function implementations:

```cpp
void SynapseNetDeleter::operator()(SynapseNet* p) const { delete p; }

SynapseNetPtr createSynapseNet() {
    return SynapseNetPtr(new SynapseNet());
}

bool initializeSynapseNet(SynapseNet& node, const NodeConfig& config) {
    return node.initialize(config);
}

int runSynapseNetCommand(SynapseNet& node, const std::vector<std::string>& args) {
    return node.runCommand(args);
}

int runSynapseNet(SynapseNet& node) {
    return node.run();
}

void shutdownSynapseNet(SynapseNet& node) {
    node.shutdown();
}
```

The four `.inc` files (`tor_management.inc`, `naan_coordination.inc`, `poe_handlers.inc`, `net_message_handlers.inc`) remain textually included inside the class body, exactly as before. Their content is unchanged.

### `CMakeLists.txt` -- One Line Added

```cmake
set(NODE_SOURCES
    src/node/maintenance_runtime.cpp
    src/node/node_init.cpp
    src/node/poe_runtime.cpp
    src/node/node_status_runtime.cpp
    src/node/node_runtime.cpp
    src/node/synapse_net.cpp          # <-- new
    src/node/tor_control_runtime.cpp
    src/node/tor_status_runtime.cpp
)
```

Global variables changed from `static` to non-static to support `extern` linkage across translation units. No other build changes.

## Validation

Built from source and ran the full test suite. All 267 tests passed with 0 failures.

```
Build:
  cmake -S KeplerSynapseNet -B KeplerSynapseNet/build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DUSE_LLAMA_CPP=ON -DUSE_SECP256K1=ON
  cmake --build KeplerSynapseNet/build --parallel $(nproc)

Tests:
  ctest --test-dir KeplerSynapseNet/build --output-on-failure
  267 tests, 267 passed, 0 failed
```

## Files Changed

| File | Change | What |
|------|--------|------|
| `include/node/synapse_net.h` | Added | Minimal header with forward decl, factory functions, extern globals |
| `src/node/synapse_net.cpp` | Added | Full SynapseNet class + factory implementations (~4,730 lines) |
| `src/main.cpp` | Modified | Reduced from 4,809 to 117 lines (entry point only) |
| `CMakeLists.txt` | Modified | Added `synapse_net.cpp` to NODE_SOURCES |

## Why This Matters

- **Faster builds**: Changing `main.cpp` (signal handling, argument parsing) no longer recompiles the entire SynapseNet class. Changing `SynapseNet` internals no longer recompiles `main()`.
- **Cleaner boundaries**: `main.cpp` depends on five function signatures, not thousands of lines of class internals.
- **Easier code review**: Changes to node orchestration, RPC, Tor, or consensus are in `synapse_net.cpp`. Changes to startup flow are in `main.cpp`. No more scrolling past 4,000 unrelated lines.
- **Foundation for further decomposition**: The `.inc` files can now be converted to standalone `.cpp` files one at a time without touching `main.cpp` again.

---

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Built_by_Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Kepler" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
</p>

<p align="center">
  If you find this project worth watching -- even if you can't contribute code -- you can help keep it going.<br>
  Donations go directly toward VPS hosting for seed nodes, build infrastructure, and development time.
</p>

<p align="center">
  <a href="https://www.blockchain.com/btc/address/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4"><img src="https://img.shields.io/badge/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="BTC" /></a>
</p>
