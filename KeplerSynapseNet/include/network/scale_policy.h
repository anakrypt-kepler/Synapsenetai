#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace synapse {
namespace network {
namespace scale {

struct PeerSnapshot {
    std::string id;
    std::string address;
    bool isOutbound = false;
    uint64_t connectedAt = 0;
    uint64_t lastSeen = 0;
    uint64_t bytesReceived = 0;
    double penaltyScore = 0.0;
};

struct AdmissionLimits {
    uint32_t maxPeers = 125;
    uint32_t maxInbound = 100;
    uint32_t maxOutbound = 25;
    uint32_t maxPeersPerIp = 8;
    uint32_t maxPeersPerSubnet = 32;
    uint8_t subnetPrefixBits = 24;
    bool overloadMode = false;
    bool enableDeterministicEviction = true;
};

struct AdmissionDecision {
    bool accept = false;
    std::string evictPeerId;
    std::string reason;
};

std::string ipv4SubnetKey(const std::string& address, uint8_t prefixBits);

AdmissionDecision evaluateInboundAdmission(const std::vector<PeerSnapshot>& peers,
                                           const std::string& incomingAddress,
                                           const AdmissionLimits& limits);

bool allowOutboundConnection(const std::vector<PeerSnapshot>& peers,
                             const std::string& targetAddress,
                             const AdmissionLimits& limits,
                             std::string* reason = nullptr);

struct TokenBucketState {
    double tokens = 0.0;
    uint64_t lastRefillMs = 0;
};

bool consumeTokenBucket(TokenBucketState& state,
                        uint64_t nowMs,
                        double refillPerSecond,
                        double burstCapacity,
                        double cost);

struct PenaltyState {
    double score = 0.0;
    uint64_t lastUpdateSec = 0;
};

double decayPenalty(double score, uint64_t elapsedSec, uint32_t halfLifeSeconds);
double addPenalty(PenaltyState& state, uint64_t nowSec, double delta, uint32_t halfLifeSeconds);
uint32_t banSecondsFromPenalty(double score, uint32_t baseBanSeconds, uint32_t maxBanSeconds);

}
}
}
