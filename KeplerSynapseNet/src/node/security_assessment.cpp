#include "node/security_assessment.h"

#include <chrono>
#include <sys/stat.h>

namespace synapse::node {

namespace {

uint64_t nowUnixSeconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

bool checkConfigFilePermissions(const std::string& path) {
    if (path.empty()) return false;
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) return false;
    return (st.st_mode & S_IRWXO) == 0;
}

} // namespace

SecurityAssessmentResult runSecurityAssessment(const SecurityAssessmentInputs& inputs) {
    SecurityAssessmentResult result;
    result.assessedAt = nowUnixSeconds();
    uint32_t score = 0;
    uint32_t maxScore = 0;

    maxScore += 15;
    if (inputs.torReachable && inputs.torReachable->load()) {
        result.torConnected = true;
        score += 10;
    } else {
        result.warnings.push_back("Tor SOCKS proxy is not reachable");
        result.recommendations.push_back("Ensure Tor is running and SOCKS proxy is accessible");
    }
    if (inputs.torWebReady && inputs.torWebReady->load()) {
        result.torBootstrapped = true;
        score += 5;
    } else {
        result.warnings.push_back("Tor circuit is not fully bootstrapped");
        result.recommendations.push_back("Wait for Tor bootstrap or check bridge configuration");
    }

    bool torControlOk = false;
    if (inputs.probeTorControl) {
        torControlOk = inputs.probeTorControl();
    }
    maxScore += 5;
    if (torControlOk) {
        score += 5;
    } else {
        result.warnings.push_back("Tor control port probe failed");
        result.recommendations.push_back("Verify Tor control port settings and authentication");
    }

    maxScore += 15;
    if (inputs.quantumSecurityEnabled) {
        result.quantumEnabled = true;
        score += 15;
    } else {
        result.warnings.push_back("Quantum-safe cryptography is disabled");
        result.recommendations.push_back("Enable quantum security for post-quantum resistance");
    }

    maxScore += 15;
    if (inputs.rpcAuthRequired) {
        bool credentialsConfigured =
            !inputs.rpcUser.empty() && !inputs.rpcPassword.empty();
        if (credentialsConfigured) {
            result.rpcAuthEnabled = true;
            score += 15;
        } else {
            result.rpcAuthEnabled = true;
            score += 10;
            result.warnings.push_back("RPC auth enabled but credentials may rely on cookie file only");
            result.recommendations.push_back("Set explicit RPC username and password for defense in depth");
        }
    } else {
        result.warnings.push_back("RPC authentication is disabled");
        result.recommendations.push_back("Enable RPC authentication to prevent unauthorized access");
    }

    maxScore += 10;
    result.configPermissionsOk = checkConfigFilePermissions(inputs.configFilePath);
    if (result.configPermissionsOk) {
        score += 10;
    } else {
        result.warnings.push_back("Config file permissions allow world access or file not found");
        result.recommendations.push_back("Set config file permissions to 600 or 640");
    }

    maxScore += 10;
    if (inputs.bindPort != 0 && inputs.bindPort < 1024) {
        result.warnings.push_back("Node bind port is in the privileged range (<1024)");
        result.recommendations.push_back("Use an unprivileged port (>=1024) to reduce attack surface");
        score += 5;
    } else {
        score += 10;
    }
    if (inputs.rpcPort != 0 && inputs.rpcPort == inputs.bindPort) {
        result.warnings.push_back("RPC port collides with the network bind port");
        result.recommendations.push_back("Use separate ports for RPC and network traffic");
    }

    maxScore += 15;
    if (inputs.getConnectedPeerCount) {
        result.connectedPeers = inputs.getConnectedPeerCount();
    }
    if (result.connectedPeers == 0) {
        result.warnings.push_back("No connected peers detected");
        result.recommendations.push_back("Check network configuration and firewall rules");
    } else if (inputs.maxPeers > 0 &&
               result.connectedPeers < inputs.maxPeers / 4) {
        score += 7;
        result.warnings.push_back("Low peer count relative to maximum configured peers");
        result.recommendations.push_back("Verify discovery settings and seed node connectivity");
    } else {
        score += 15;
    }

    maxScore += 15;
    if (inputs.getKnowledgeEntryCount) {
        result.knowledgeEntries = inputs.getKnowledgeEntryCount();
    }
    if (result.knowledgeEntries == 0) {
        result.warnings.push_back("Knowledge base is empty");
        result.recommendations.push_back("Run an initial knowledge sync or crawl to populate the knowledge base");
        score += 5;
    } else {
        score += 15;
    }

    if (!inputs.privacyMode) {
        result.warnings.push_back("Privacy mode is disabled");
        result.recommendations.push_back("Enable privacy mode to minimize metadata leakage");
    }

    if (inputs.securityLevel == "standard") {
        result.recommendations.push_back("Consider raising security level to 'high' or 'paranoid'");
    }

    bool onionActive = false;
    if (inputs.isOnionServiceActive) {
        onionActive = inputs.isOnionServiceActive();
    }
    if (result.torConnected && !onionActive) {
        result.recommendations.push_back("Enable onion service to accept inbound Tor connections");
    }

    result.overallScore =
        maxScore > 0 ? static_cast<uint32_t>(score * 100ULL / maxScore) : 0;

    return result;
}

} // namespace synapse::node
