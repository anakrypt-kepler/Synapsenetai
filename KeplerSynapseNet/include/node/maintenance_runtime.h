#pragma once

#include "core/ledger.h"
#include "core/tor_route_policy.h"
#include "crypto/crypto.h"
#include "model/model_access.h"
#include "model/model_loader.h"
#include "network/network.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace synapse {

enum class PoeInvKind : uint8_t { ENTRY = 1, VOTE = 2, EPOCH = 3 };

struct PoeSyncState {
    bool active = false;
    bool done = false;
    bool inFlight = false;
    crypto::Hash256 after{};
    uint32_t limit = 0;
    uint64_t lastRequestAt = 0;
    uint64_t pages = 0;
};

struct PoePeerSyncState {
    PoeSyncState entries;
    PoeSyncState votes;
    PoeSyncState epochs;
};

struct ProviderSession {
    std::string renterId;
    uint64_t expiresAt = 0;
    uint64_t pricePerRequestAtoms = 0;
};

namespace node {

struct PoeRetryRequest {
    std::string peerId;
    PoeInvKind kind = PoeInvKind::ENTRY;
    crypto::Hash256 after{};
    uint32_t limit = 0;
};

std::vector<PoeRetryRequest> collectPoeSyncRetryRequests(
    std::unordered_map<std::string, PoePeerSyncState>& poeSync,
    std::mutex& poeSyncMtx,
    uint64_t now,
    uint32_t limitEpochs);

struct BlockBuildTickInputs {
    const core::Ledger* ledger = nullptr;
    bool torRequired = false;
    bool torWebReady = false;
    core::TorRoutePolicyDecision route{};
};

bool shouldBuildPendingBlock(const BlockBuildTickInputs& inputs);

std::vector<std::pair<std::string, ProviderSession>> collectExpiredProviderSessions(
    std::unordered_map<std::string, ProviderSession>& providerSessions,
    std::mutex& remoteProvMtx,
    uint64_t now);

bool shouldBroadcastLocalOffer(
    const network::Network* network,
    const model::ModelAccess* modelAccess,
    const model::ModelLoader* modelLoader,
    uint64_t remotePricePerRequestAtoms);

} // namespace node
} // namespace synapse
