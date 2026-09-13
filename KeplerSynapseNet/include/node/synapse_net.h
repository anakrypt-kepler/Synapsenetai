#pragma once

// Node process facade used by main.cpp.
// Real work lives in synapse_net.cpp: config, DB, crypto, P2P, PoE, NAAN,
// privacy, RPC, then the run loop (TUI or daemon).
// g_running / g_shutdownSignal are the global stop flags (SIGINT/SIGTERM).

#include "node/node_config.h"
#include <memory>
#include <string>
#include <vector>
#include <atomic>

namespace synapse {

extern std::atomic<bool> g_running;
extern std::atomic<bool> g_reloadConfig;
extern std::atomic<bool> g_daemonMode;
extern std::atomic<int> g_shutdownSignal;

// Forward declaration - full definition in synapse_net.cpp
class SynapseNet;

// Custom deleter for SynapseNet unique_ptr (type is incomplete in main.cpp)
struct SynapseNetDeleter {
    void operator()(SynapseNet* p) const;
};
using SynapseNetPtr = std::unique_ptr<SynapseNet, SynapseNetDeleter>;

// Factory / lifecycle helpers (allow main.cpp to use SynapseNet without
// pulling in the full class definition and its transitive includes).
SynapseNetPtr createSynapseNet();
bool initializeSynapseNet(SynapseNet& node, const NodeConfig& config);
int  runSynapseNetCommand(SynapseNet& node, const std::vector<std::string>& args);
int  runSynapseNet(SynapseNet& node);
void shutdownSynapseNet(SynapseNet& node);

std::string formatUptime(uint64_t seconds);
std::string formatBytes(uint64_t bytes);
void signalHandler(int signal);
void printBanner();

} // namespace synapse
