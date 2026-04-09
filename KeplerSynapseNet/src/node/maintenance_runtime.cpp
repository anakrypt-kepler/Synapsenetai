#include "node/maintenance_runtime.h"

namespace synapse::node {

std::vector<PoeRetryRequest> collectPoeSyncRetryRequests(
    std::unordered_map<std::string, PoePeerSyncState>& poeSync,
    std::mutex& poeSyncMtx,
    uint64_t now,
    uint32_t limitEpochs) {
    std::vector<PoeRetryRequest> retries;
    std::lock_guard<std::mutex> lock(poeSyncMtx);
    for (auto& [peerId, st] : poeSync) {
        if (st.entries.active && st.votes.active && st.entries.done && st.votes.done && !st.epochs.active) {
            st.epochs.active = true;
            st.epochs.inFlight = true;
            st.epochs.done = false;
            st.epochs.after = crypto::Hash256{};
            st.epochs.limit = limitEpochs;
            st.epochs.lastRequestAt = now;
            retries.push_back({peerId, PoeInvKind::EPOCH, st.epochs.after, st.epochs.limit});
        }
        if (st.entries.active && st.entries.inFlight && !st.entries.done &&
            now > st.entries.lastRequestAt + 3) {
            retries.push_back({peerId, PoeInvKind::ENTRY, st.entries.after, st.entries.limit});
            st.entries.lastRequestAt = now;
        }
        if (st.votes.active && st.votes.inFlight && !st.votes.done &&
            now > st.votes.lastRequestAt + 3) {
            retries.push_back({peerId, PoeInvKind::VOTE, st.votes.after, st.votes.limit});
            st.votes.lastRequestAt = now;
        }
        if (st.epochs.active && st.epochs.inFlight && !st.epochs.done &&
            now > st.epochs.lastRequestAt + 3) {
            retries.push_back({peerId, PoeInvKind::EPOCH, st.epochs.after, st.epochs.limit});
            st.epochs.lastRequestAt = now;
        }
    }
    return retries;
}

bool shouldBuildPendingBlock(const BlockBuildTickInputs& inputs) {
    if (!inputs.ledger || inputs.ledger->getPendingEventCount() == 0) {
        return false;
    }
    if (!inputs.torRequired) {
        return true;
    }
    return inputs.route.allowP2PDiscovery && inputs.torWebReady;
}

std::vector<std::pair<std::string, ProviderSession>> collectExpiredProviderSessions(
    std::unordered_map<std::string, ProviderSession>& providerSessions,
    std::mutex& remoteProvMtx,
    uint64_t now) {
    std::vector<std::pair<std::string, ProviderSession>> expired;
    std::lock_guard<std::mutex> lock(remoteProvMtx);
    for (const auto& [sid, session] : providerSessions) {
        if (session.expiresAt != 0 && session.expiresAt < now) {
            expired.push_back({sid, session});
        }
    }
    for (const auto& item : expired) {
        providerSessions.erase(item.first);
    }
    return expired;
}

bool shouldBroadcastLocalOffer(
    const network::Network* network,
    const model::ModelAccess* modelAccess,
    const model::ModelLoader* modelLoader,
    uint64_t remotePricePerRequestAtoms) {
    return network != nullptr &&
           modelAccess != nullptr &&
           modelLoader != nullptr &&
           remotePricePerRequestAtoms != 0 &&
           modelLoader->isLoaded() &&
           modelAccess->getMode() != model::AccessMode::PRIVATE;
}

} // namespace synapse::node
