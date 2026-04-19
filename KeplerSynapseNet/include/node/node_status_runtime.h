#pragma once

#include "network/network.h"
#include "quantum/quantum_security.h"
#include "rpc/rpc_node_views.h"

#include <atomic>
#include <functional>
#include <string>

namespace synapse::node {

std::string quantumSecurityLevelToString(quantum::SecurityLevel level);
std::string quantumAlgorithmToString(quantum::CryptoAlgorithm algo);
bool quantumLiboqsEnabled();
std::string quantumKyberImplementationMode();
std::string quantumDilithiumImplementationMode();
std::string quantumSphincsImplementationMode();
std::string quantumCapabilityMode();

struct NodeQuantumStatusRuntimeInputs {
    bool enabled = false;
    std::string requestedLevel = "standard";
    quantum::QuantumManager* quantumManager = nullptr;
};

struct NodeNetworkHealthRuntimeInputs {
    network::Network* network = nullptr;
    uint32_t maxPeers = 0;
    uint32_t maxInbound = 0;
    uint32_t maxOutbound = 0;
    uint64_t ledgerHeight = 0;
    const std::atomic<uint64_t>* networkHeight = nullptr;
    const std::atomic<uint64_t>* invBackpressureDrops = nullptr;
    const std::atomic<uint64_t>* getDataBackpressureDrops = nullptr;
    const std::atomic<uint64_t>* gossipSuppressed = nullptr;
    const std::atomic<uint64_t>* gossipSubsetRouted = nullptr;
};

rpc::RpcNodeQuantumInputs collectNodeQuantumInputs(
    const NodeQuantumStatusRuntimeInputs& runtime);

rpc::RpcNodeNetworkHealthInputs collectNodeNetworkHealthInputs(
    const NodeNetworkHealthRuntimeInputs& runtime);

} // namespace synapse::node
