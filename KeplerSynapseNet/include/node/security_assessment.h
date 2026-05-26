#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace synapse::node {

struct SecurityAssessmentResult {
    uint32_t overallScore = 0;
    bool torConnected = false;
    bool torBootstrapped = false;
    bool quantumEnabled = false;
    bool rpcAuthEnabled = false;
    uint32_t connectedPeers = 0;
    uint32_t knowledgeEntries = 0;
    bool configPermissionsOk = false;
    std::vector<std::string> warnings;
    std::vector<std::string> recommendations;
    uint64_t assessedAt = 0;
};

struct SecurityAssessmentInputs {
    const std::atomic<bool>* torReachable = nullptr;
    const std::atomic<bool>* torWebReady = nullptr;
    std::function<bool()> probeTorControl;
    std::function<bool()> isOnionServiceActive;
    bool quantumSecurityEnabled = false;
    bool rpcAuthRequired = false;
    std::string rpcUser;
    std::string rpcPassword;
    uint16_t rpcPort = 0;
    uint16_t bindPort = 0;
    std::string configFilePath;
    uint32_t maxPeers = 0;
    std::function<uint32_t()> getConnectedPeerCount;
    std::function<uint32_t()> getKnowledgeEntryCount;
    bool privacyMode = false;
    std::string securityLevel;
};

SecurityAssessmentResult runSecurityAssessment(const SecurityAssessmentInputs& inputs);

} // namespace synapse::node
