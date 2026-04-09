#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "network/scale_policy.h"

namespace synapse {
namespace test {

class TestFramework {
public:
    static void assertTrue(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << std::endl;
            failCount_ += 1;
        } else {
            passCount_ += 1;
        }
    }

    template <typename T, typename U>
    static void assertEqual(T expected, U actual, const std::string& message) {
        if (expected != actual) {
            std::cerr << "FAIL: " << message << " expected=" << expected << " actual=" << actual << std::endl;
            failCount_ += 1;
        } else {
            passCount_ += 1;
        }
    }

    static int failCount() { return failCount_; }
    static int passCount() { return passCount_; }

private:
    static int failCount_;
    static int passCount_;
};

int TestFramework::failCount_ = 0;
int TestFramework::passCount_ = 0;

struct ScenarioResult {
    size_t residentPeers = 0;
    size_t rejected = 0;
    size_t evicted = 0;
    size_t maxPeersPerIp = 0;
    size_t maxPeersPerSubnet = 0;
    uint64_t digest = 0;
};

static std::string makeStressAddress(size_t i) {
    if ((i % 10) < 7) {
        const size_t host = (i % 24) + 1;
        const size_t subnet = (i / 24) % 3;
        return "10.42." + std::to_string(subnet) + "." + std::to_string(host);
    }
    const size_t b = 16 + ((i / 97) % 32);
    const size_t c = (i / 13) % 255;
    const size_t d = (i % 253) + 1;
    return "172." + std::to_string(b) + "." + std::to_string(c) + "." + std::to_string(d);
}

static uint64_t mixDigest(uint64_t seed, const std::string& payload) {
    uint64_t v = seed;
    for (char c : payload) {
        v ^= static_cast<uint8_t>(c);
        v *= 1099511628211ULL;
    }
    return v;
}

static ScenarioResult runStressScenario(size_t attempts) {
    network::scale::AdmissionLimits limits;
    limits.maxPeers = 125;
    limits.maxInbound = 100;
    limits.maxOutbound = 25;
    limits.maxPeersPerIp = 8;
    limits.maxPeersPerSubnet = 32;
    limits.subnetPrefixBits = 24;
    limits.overloadMode = true;
    limits.enableDeterministicEviction = true;

    std::vector<network::scale::PeerSnapshot> peers;
    peers.reserve(limits.maxPeers);
    ScenarioResult result;
    result.digest = 1469598103934665603ULL;

    for (size_t i = 0; i < attempts; ++i) {
        const std::string address = makeStressAddress(i);
        auto decision = network::scale::evaluateInboundAdmission(peers, address, limits);
        result.digest = mixDigest(result.digest, decision.reason);
        if (!decision.accept) {
            result.rejected += 1;
            continue;
        }

        if (!decision.evictPeerId.empty()) {
            auto it = std::find_if(peers.begin(), peers.end(), [&](const network::scale::PeerSnapshot& peer) {
                return peer.id == decision.evictPeerId;
            });
            if (it != peers.end()) {
                peers.erase(it);
                result.evicted += 1;
            }
        }

        network::scale::PeerSnapshot incoming;
        incoming.id = "peer_" + std::to_string(i);
        incoming.address = address;
        incoming.isOutbound = false;
        incoming.connectedAt = static_cast<uint64_t>(i);
        incoming.lastSeen = static_cast<uint64_t>(i);
        incoming.bytesReceived = static_cast<uint64_t>((i * 17) % 8192);
        incoming.penaltyScore = ((i % 9) < 6) ? 8.0 : 1.5;
        peers.push_back(std::move(incoming));
    }

    std::unordered_map<std::string, size_t> ipCounts;
    std::unordered_map<std::string, size_t> subnetCounts;
    for (const auto& peer : peers) {
        ipCounts[peer.address] += 1;
        subnetCounts[network::scale::ipv4SubnetKey(peer.address, limits.subnetPrefixBits)] += 1;
        result.digest = mixDigest(result.digest, peer.id + ":" + peer.address);
    }
    for (const auto& kv : ipCounts) {
        result.maxPeersPerIp = std::max(result.maxPeersPerIp, kv.second);
    }
    for (const auto& kv : subnetCounts) {
        result.maxPeersPerSubnet = std::max(result.maxPeersPerSubnet, kv.second);
    }
    result.residentPeers = peers.size();
    return result;
}

static void testTokenBucketDeterminism() {
    network::scale::TokenBucketState bucket;
    TestFramework::assertTrue(network::scale::consumeTokenBucket(bucket, 1000, 100.0, 100.0, 50.0), "token bucket should allow first consume");
    TestFramework::assertTrue(!network::scale::consumeTokenBucket(bucket, 1000, 100.0, 100.0, 60.0), "token bucket should reject overflow consume");
    TestFramework::assertTrue(network::scale::consumeTokenBucket(bucket, 1500, 100.0, 100.0, 60.0), "token bucket should refill deterministically");
}

static void testDecayBanProgression() {
    network::scale::PenaltyState state;
    const double score1 = network::scale::addPenalty(state, 100, 20.0, 900);
    const uint32_t ban1 = network::scale::banSecondsFromPenalty(score1, 120, 3600);
    const double score2 = network::scale::addPenalty(state, 200, 40.0, 900);
    const uint32_t ban2 = network::scale::banSecondsFromPenalty(score2, 120, 3600);
    const double score3 = network::scale::addPenalty(state, 1100, 0.0, 900);
    const uint32_t ban3 = network::scale::banSecondsFromPenalty(score3, 120, 3600);

    TestFramework::assertTrue(score2 > score1, "penalty score should increase with new violations");
    TestFramework::assertTrue(ban2 >= ban1, "ban duration should grow with higher penalty");
    TestFramework::assertTrue(score3 < score2, "penalty score should decay over time");
    TestFramework::assertTrue(ban3 <= ban2, "ban duration should decay with penalty");
}

static void testDeterministicScaleScenarios() {
    const std::vector<size_t> scenarios = {1000, 5000, 10000};
    for (size_t attempts : scenarios) {
        auto first = runStressScenario(attempts);
        auto second = runStressScenario(attempts);
        const std::string tag = "scenario_" + std::to_string(attempts);
        TestFramework::assertEqual(first.digest, second.digest, tag + "_digest_determinism");
        TestFramework::assertEqual(first.residentPeers, second.residentPeers, tag + "_resident_determinism");
        TestFramework::assertTrue(first.residentPeers <= 125, tag + "_resident_budget");
        TestFramework::assertTrue(first.maxPeersPerIp <= 8, tag + "_ip_budget");
        TestFramework::assertTrue(first.maxPeersPerSubnet <= 32, tag + "_subnet_budget");
        TestFramework::assertTrue(first.evicted > 0, tag + "_eviction_engaged");
    }
}

}
}

int main() {
    using namespace synapse::test;
    testTokenBucketDeterminism();
    testDecayBanProgression();
    testDeterministicScaleScenarios();
    std::cout << "NetworkScalePolicyTests pass=" << TestFramework::passCount()
              << " fail=" << TestFramework::failCount() << std::endl;
    return TestFramework::failCount() == 0 ? 0 : 1;
}
