#pragma once

#include "core/poe_v1_engine.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <optional>
#include <string>
#include <vector>

namespace synapse::rpc {

struct RpcPoeCodeListItem {
    crypto::Hash256 submitId{};
    crypto::Hash256 contentId{};
    uint64_t timestamp = 0;
    std::string title;
    bool finalized = false;
};

struct RpcPoeEpochAllocationView {
    crypto::Hash256 submitId{};
    crypto::Hash256 contentId{};
    std::string author;
    uint64_t score = 0;
    uint64_t amountAtoms = 0;
    double amount = 0.0;
    bool credited = false;
};

std::string buildRpcPoeValidatorsResponse(
    const std::vector<crypto::PublicKey>& validators);

std::string buildRpcPoeSubmitResponse(
    const core::PoeSubmitResult& submitResult,
    core::poe_v1::ContentType contentType,
    uint64_t expectedAcceptanceRewardAtoms,
    double expectedAcceptanceReward,
    uint64_t creditedAtoms,
    double credited,
    uint32_t voteCount,
    uint32_t requiredVotes,
    bool rewardCredited);

std::string buildRpcPoeCodeListResponse(
    const std::vector<RpcPoeCodeListItem>& items);

std::string buildRpcPoeCodeFetchResponse(
    const crypto::Hash256& submitId,
    const core::poe_v1::KnowledgeEntryV1& entry,
    bool finalized,
    uint64_t acceptanceRewardAtoms,
    double acceptanceReward);

std::string buildRpcPoeVoteResponse(
    bool added,
    const crypto::Hash256& voteId,
    uint64_t creditedAtoms,
    double credited);

std::string buildRpcPoeFinalizeResponse(
    const std::optional<core::poe_v1::FinalizationRecordV1>& finalization,
    uint64_t creditedAtoms,
    double credited);

std::string buildRpcPoeEpochResponse(
    uint64_t epochId,
    const crypto::Hash256& allocationHash,
    uint64_t mintedAtoms,
    double minted,
    uint64_t mintedEntries,
    uint64_t mintedSelfAtoms,
    double mintedSelf,
    const std::vector<RpcPoeEpochAllocationView>& allocations);

std::string buildRpcPoeExportResponse(
    const std::vector<std::string>& paths);

std::string buildRpcPoeImportResponse(
    const std::string& path);

} // namespace synapse::rpc
