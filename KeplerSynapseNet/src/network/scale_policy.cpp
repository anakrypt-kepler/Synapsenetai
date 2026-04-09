#include "network/scale_policy.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cmath>
#include <limits>

namespace synapse {
namespace network {
namespace scale {

namespace {

struct AdmissionCounters {
    uint32_t total = 0;
    uint32_t inbound = 0;
    uint32_t outbound = 0;
    uint32_t sameIpTotal = 0;
    uint32_t sameIpInbound = 0;
    uint32_t sameSubnetTotal = 0;
    uint32_t sameSubnetInbound = 0;
};

AdmissionCounters countPeers(const std::vector<PeerSnapshot>& peers,
                             const std::string& ip,
                             const std::string& subnet,
                             uint8_t prefixBits) {
    AdmissionCounters out;
    for (const auto& peer : peers) {
        out.total += 1;
        if (peer.isOutbound) {
            out.outbound += 1;
        } else {
            out.inbound += 1;
        }
        if (!ip.empty() && peer.address == ip) {
            out.sameIpTotal += 1;
            if (!peer.isOutbound) out.sameIpInbound += 1;
        }
        if (!subnet.empty() && ipv4SubnetKey(peer.address, prefixBits) == subnet) {
            out.sameSubnetTotal += 1;
            if (!peer.isOutbound) out.sameSubnetInbound += 1;
        }
    }
    return out;
}

std::vector<PeerSnapshot> candidateEvictions(const std::vector<PeerSnapshot>& peers,
                                             const std::string& incomingIp,
                                             const std::string& incomingSubnet,
                                             uint8_t prefixBits) {
    std::vector<PeerSnapshot> sameIpInbound;
    std::vector<PeerSnapshot> sameSubnetInbound;
    std::vector<PeerSnapshot> allInbound;
    sameIpInbound.reserve(peers.size());
    sameSubnetInbound.reserve(peers.size());
    allInbound.reserve(peers.size());

    for (const auto& peer : peers) {
        if (peer.isOutbound) continue;
        allInbound.push_back(peer);
        if (!incomingIp.empty() && peer.address == incomingIp) {
            sameIpInbound.push_back(peer);
        }
        if (!incomingSubnet.empty() && ipv4SubnetKey(peer.address, prefixBits) == incomingSubnet) {
            sameSubnetInbound.push_back(peer);
        }
    }

    auto sortFn = [](std::vector<PeerSnapshot>& list) {
        std::sort(list.begin(), list.end(), [](const PeerSnapshot& a, const PeerSnapshot& b) {
            if (a.penaltyScore != b.penaltyScore) return a.penaltyScore > b.penaltyScore;
            if (a.lastSeen != b.lastSeen) return a.lastSeen < b.lastSeen;
            if (a.connectedAt != b.connectedAt) return a.connectedAt < b.connectedAt;
            if (a.bytesReceived != b.bytesReceived) return a.bytesReceived > b.bytesReceived;
            return a.id < b.id;
        });
    };
    sortFn(sameIpInbound);
    sortFn(sameSubnetInbound);
    sortFn(allInbound);

    if (!sameIpInbound.empty()) return sameIpInbound;
    if (!sameSubnetInbound.empty()) return sameSubnetInbound;
    return allInbound;
}

}

std::string ipv4SubnetKey(const std::string& address, uint8_t prefixBits) {
    if (address.empty()) return {};
    if (prefixBits > 32) prefixBits = 32;

    in_addr ipv4{};
    if (inet_pton(AF_INET, address.c_str(), &ipv4) != 1) {
        return address;
    }

    uint32_t hostOrder = ntohl(ipv4.s_addr);
    uint32_t mask = 0;
    if (prefixBits == 0) {
        mask = 0;
    } else if (prefixBits == 32) {
        mask = std::numeric_limits<uint32_t>::max();
    } else {
        mask = std::numeric_limits<uint32_t>::max() << (32 - prefixBits);
    }

    const uint32_t networkPart = hostOrder & mask;
    in_addr networkAddr{};
    networkAddr.s_addr = htonl(networkPart);
    char buf[INET_ADDRSTRLEN] = {};
    if (!inet_ntop(AF_INET, &networkAddr, buf, sizeof(buf))) {
        return address;
    }
    return std::string(buf) + "/" + std::to_string(prefixBits);
}

AdmissionDecision evaluateInboundAdmission(const std::vector<PeerSnapshot>& peers,
                                           const std::string& incomingAddress,
                                           const AdmissionLimits& limits) {
    AdmissionDecision out;
    const std::string incomingSubnet = ipv4SubnetKey(incomingAddress, limits.subnetPrefixBits);
    const auto counts = countPeers(peers, incomingAddress, incomingSubnet, limits.subnetPrefixBits);

    const bool overPeers = limits.maxPeers > 0 && counts.total >= limits.maxPeers;
    const bool overInbound = limits.maxInbound > 0 && counts.inbound >= limits.maxInbound;
    const bool overIp = limits.maxPeersPerIp > 0 && counts.sameIpTotal >= limits.maxPeersPerIp;
    const bool overSubnet = limits.maxPeersPerSubnet > 0 && counts.sameSubnetTotal >= limits.maxPeersPerSubnet;
    const bool overloaded = limits.overloadMode && limits.maxPeers > 0 &&
                            counts.total >= static_cast<uint32_t>(std::max<uint32_t>(1, limits.maxPeers * 85 / 100));

    if (!overPeers && !overInbound && !overIp && !overSubnet && !overloaded) {
        out.accept = true;
        out.reason = "accept";
        return out;
    }

    if (!limits.enableDeterministicEviction) {
        out.accept = false;
        if (overIp) out.reason = "reject_ip_quota";
        else if (overSubnet) out.reason = "reject_subnet_quota";
        else if (overInbound) out.reason = "reject_inbound_quota";
        else if (overPeers) out.reason = "reject_peer_quota";
        else out.reason = "reject_overload";
        return out;
    }

    auto candidates = candidateEvictions(peers, incomingAddress, incomingSubnet, limits.subnetPrefixBits);
    if (candidates.empty()) {
        out.accept = false;
        out.reason = "reject_no_evict_candidate";
        return out;
    }

    out.accept = true;
    out.evictPeerId = candidates.front().id;
    if (overIp) out.reason = "evict_for_ip_quota";
    else if (overSubnet) out.reason = "evict_for_subnet_quota";
    else if (overInbound) out.reason = "evict_for_inbound_quota";
    else if (overPeers) out.reason = "evict_for_peer_quota";
    else out.reason = "evict_for_overload";
    return out;
}

bool allowOutboundConnection(const std::vector<PeerSnapshot>& peers,
                             const std::string& targetAddress,
                             const AdmissionLimits& limits,
                             std::string* reason) {
    const std::string subnet = ipv4SubnetKey(targetAddress, limits.subnetPrefixBits);
    const auto counts = countPeers(peers, targetAddress, subnet, limits.subnetPrefixBits);

    if (limits.maxPeers > 0 && counts.total >= limits.maxPeers) {
        if (reason) *reason = "peer_quota";
        return false;
    }
    if (limits.maxOutbound > 0 && counts.outbound >= limits.maxOutbound) {
        if (reason) *reason = "outbound_quota";
        return false;
    }
    if (limits.maxPeersPerIp > 0 && counts.sameIpTotal >= limits.maxPeersPerIp) {
        if (reason) *reason = "ip_quota";
        return false;
    }
    if (limits.maxPeersPerSubnet > 0 && counts.sameSubnetTotal >= limits.maxPeersPerSubnet) {
        if (reason) *reason = "subnet_quota";
        return false;
    }

    if (reason) *reason = "accept";
    return true;
}

bool consumeTokenBucket(TokenBucketState& state,
                        uint64_t nowMs,
                        double refillPerSecond,
                        double burstCapacity,
                        double cost) {
    if (burstCapacity <= 0.0 || refillPerSecond < 0.0 || cost <= 0.0) return false;

    if (state.lastRefillMs == 0) {
        state.tokens = burstCapacity;
        state.lastRefillMs = nowMs;
    }

    if (nowMs > state.lastRefillMs) {
        const double elapsedSec = static_cast<double>(nowMs - state.lastRefillMs) / 1000.0;
        state.tokens = std::min(burstCapacity, state.tokens + elapsedSec * refillPerSecond);
        state.lastRefillMs = nowMs;
    }

    if (state.tokens + 1e-9 < cost) {
        return false;
    }

    state.tokens -= cost;
    if (state.tokens < 0.0) state.tokens = 0.0;
    return true;
}

double decayPenalty(double score, uint64_t elapsedSec, uint32_t halfLifeSeconds) {
    if (score <= 0.0) return 0.0;
    if (halfLifeSeconds == 0 || elapsedSec == 0) return score;
    const double exponent = -static_cast<double>(elapsedSec) / static_cast<double>(halfLifeSeconds);
    const double factor = std::exp2(exponent);
    const double decayed = score * factor;
    return decayed < 0.0 ? 0.0 : decayed;
}

double addPenalty(PenaltyState& state, uint64_t nowSec, double delta, uint32_t halfLifeSeconds) {
    if (nowSec == 0) nowSec = 1;
    if (state.lastUpdateSec != 0 && nowSec > state.lastUpdateSec) {
        state.score = decayPenalty(state.score, nowSec - state.lastUpdateSec, halfLifeSeconds);
    }
    state.lastUpdateSec = nowSec;

    if (delta > 0.0) {
        state.score += delta;
    }
    if (state.score < 0.0) state.score = 0.0;
    if (state.score > 10000.0) state.score = 10000.0;
    return state.score;
}

uint32_t banSecondsFromPenalty(double score, uint32_t baseBanSeconds, uint32_t maxBanSeconds) {
    if (score <= 0.0) return 0;
    if (baseBanSeconds == 0) return 0;
    if (maxBanSeconds < baseBanSeconds) maxBanSeconds = baseBanSeconds;

    const double multiplier = 1.0 + (score / 25.0);
    const double scaled = static_cast<double>(baseBanSeconds) * multiplier;
    uint64_t secs = static_cast<uint64_t>(std::llround(scaled));
    if (secs < baseBanSeconds) secs = baseBanSeconds;
    if (secs > maxBanSeconds) secs = maxBanSeconds;
    return static_cast<uint32_t>(secs);
}

}
}
}
