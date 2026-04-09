#include "rpc/rpc_poe_views.h"

namespace synapse::rpc {

namespace {

using json = nlohmann::json;

} // namespace

std::string buildRpcPoeValidatorsResponse(
    const std::vector<crypto::PublicKey>& validators) {
    json out = json::array();
    for (const auto& validator : validators) {
        out.push_back(crypto::toHex(validator));
    }
    return out.dump();
}

std::string buildRpcPoeSubmitResponse(
    const core::PoeSubmitResult& submitResult,
    core::poe_v1::ContentType contentType,
    uint64_t expectedAcceptanceRewardAtoms,
    double expectedAcceptanceReward,
    uint64_t creditedAtoms,
    double credited,
    uint32_t voteCount,
    uint32_t requiredVotes,
    bool rewardCredited) {
    json out;
    out["status"] = "ok";
    out["submitId"] = crypto::toHex(submitResult.submitId);
    out["contentId"] = crypto::toHex(submitResult.contentId);
    out["contentType"] = static_cast<int>(contentType);
    out["finalized"] = submitResult.finalized;
    out["acceptanceRewardAtoms"] = submitResult.acceptanceReward;
    out["acceptanceReward"] = submitResult.acceptanceReward == 0 ? 0.0 : submitResult.acceptanceReward / 100000000.0;
    out["expectedAcceptanceRewardAtoms"] = expectedAcceptanceRewardAtoms;
    out["expectedAcceptanceReward"] = expectedAcceptanceReward;
    out["creditedAtoms"] = creditedAtoms;
    out["credited"] = credited;
    out["voteCount"] = voteCount;
    out["requiredVotes"] = requiredVotes;
    out["rewardCredited"] = rewardCredited;
    return out.dump();
}

std::string buildRpcPoeCodeListResponse(
    const std::vector<RpcPoeCodeListItem>& items) {
    json out = json::array();
    for (const auto& item : items) {
        out.push_back({
            {"submitId", crypto::toHex(item.submitId)},
            {"contentId", crypto::toHex(item.contentId)},
            {"timestamp", item.timestamp},
            {"title", item.title},
            {"finalized", item.finalized}
        });
    }
    return out.dump();
}

std::string buildRpcPoeCodeFetchResponse(
    const crypto::Hash256& submitId,
    const core::poe_v1::KnowledgeEntryV1& entry,
    bool finalized,
    uint64_t acceptanceRewardAtoms,
    double acceptanceReward) {
    json citations = json::array();
    for (const auto& citation : entry.citations) {
        citations.push_back(crypto::toHex(citation));
    }

    json out;
    out["submitId"] = crypto::toHex(submitId);
    out["contentId"] = crypto::toHex(entry.contentId());
    out["timestamp"] = entry.timestamp;
    out["authorPubKey"] = crypto::toHex(entry.authorPubKey);
    out["title"] = entry.title;
    out["patch"] = entry.body;
    out["citations"] = std::move(citations);
    out["finalized"] = finalized;
    out["acceptanceRewardAtoms"] = acceptanceRewardAtoms;
    out["acceptanceReward"] = acceptanceReward;
    return out.dump();
}

std::string buildRpcPoeVoteResponse(
    bool added,
    const crypto::Hash256& voteId,
    uint64_t creditedAtoms,
    double credited) {
    json out;
    out["status"] = added ? "vote_added" : "vote_duplicate";
    out["added"] = added;
    out["voteId"] = crypto::toHex(voteId);
    out["creditedAtoms"] = creditedAtoms;
    out["credited"] = credited;
    return out.dump();
}

std::string buildRpcPoeFinalizeResponse(
    const std::optional<core::poe_v1::FinalizationRecordV1>& finalization,
    uint64_t creditedAtoms,
    double credited) {
    json out;
    if (!finalization) {
        out["status"] = "pending";
        out["finalized"] = false;
    } else {
        out["status"] = "finalized";
        out["finalized"] = true;
        out["finalizedAt"] = finalization->finalizedAt;
        out["validatorSetHash"] = crypto::toHex(finalization->validatorSetHash);
        out["voteCount"] = finalization->votes.size();
    }
    out["creditedAtoms"] = creditedAtoms;
    out["credited"] = credited;
    return out.dump();
}

std::string buildRpcPoeEpochResponse(
    uint64_t epochId,
    const crypto::Hash256& allocationHash,
    uint64_t mintedAtoms,
    double minted,
    uint64_t mintedEntries,
    uint64_t mintedSelfAtoms,
    double mintedSelf,
    const std::vector<RpcPoeEpochAllocationView>& allocations) {
    json allocationItems = json::array();
    for (const auto& allocation : allocations) {
        allocationItems.push_back({
            {"submitId", crypto::toHex(allocation.submitId)},
            {"contentId", crypto::toHex(allocation.contentId)},
            {"author", allocation.author},
            {"score", allocation.score},
            {"amountAtoms", allocation.amountAtoms},
            {"amount", allocation.amount},
            {"credited", allocation.credited}
        });
    }

    json out;
    out["status"] = "ok";
    out["epochId"] = epochId;
    out["allocationHash"] = crypto::toHex(allocationHash);
    out["mintedAtoms"] = mintedAtoms;
    out["minted"] = minted;
    out["mintedEntries"] = mintedEntries;
    out["mintedSelfAtoms"] = mintedSelfAtoms;
    out["mintedSelf"] = mintedSelf;
    out["allocations"] = std::move(allocationItems);
    return out.dump();
}

std::string buildRpcPoeExportResponse(
    const std::vector<std::string>& paths) {
    json out;
    out["status"] = "exported";
    out["paths"] = paths;
    return out.dump();
}

std::string buildRpcPoeImportResponse(
    const std::string& path) {
    json out;
    out["status"] = "imported";
    out["path"] = path;
    return out.dump();
}

} // namespace synapse::rpc
