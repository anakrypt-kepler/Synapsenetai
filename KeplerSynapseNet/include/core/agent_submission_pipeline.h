#pragma once

#include "core/agent_draft_queue.h"
#include "core/poe_v1_engine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace synapse::core {

struct AgentSubmissionPipelineConfig {
    uint32_t maxBatchSize = 16;
    bool includeReviewRequired = true;
    uint32_t minDistinctReviewers = 1;
    bool requireNonAuthorReviewer = false;
    uint32_t intentMinTitleBytes = 4;
    uint32_t intentMinBodyBytes = 24;
    bool intentRequireCitationForText = false;
    uint32_t intentMinCitationsForText = 0;
    bool duplicateGateContentId = true;
    bool duplicateGateNoveltyBuckets = true;
    bool duplicateGateCitationGraph = true;
    uint32_t duplicateCitationGraphMaxHamming = 24;
    uint32_t citationSanityMaxCitations = 64;
    bool citationSanityRejectDuplicateCitations = true;
    bool citationSanityRequireKnownCitations = true;
    bool citationSanityRejectSelfReference = true;
    bool citationSanityRejectIntraSetCycles = true;
    uint32_t citationSanityMaxIntraSetEdges = 8;
    uint32_t citationSanityMinCorroboratingCitations = 0;
    uint32_t citationSanityMinDistinctCitationAuthors = 0;
};

struct AgentSubmissionDryRunResult {
    crypto::Hash256 draftId{};
    bool ok = false;
    std::string reason;
    std::string nextStatus;
    poe_v1::KnowledgeEntryV1 entry{};
};

struct AgentSubmissionBatchItem {
    crypto::Hash256 draftId{};
    bool ok = false;
    std::string action;
    std::string reason;
    crypto::Hash256 submitId{};
    crypto::Hash256 contentId{};
    uint64_t expectedAcceptanceRewardAtoms = 0;
    bool acceptanceRewardCredited = false;
};

class AgentSubmissionPipeline {
public:
    explicit AgentSubmissionPipeline(const AgentSubmissionPipelineConfig& config = AgentSubmissionPipelineConfig{});

    AgentSubmissionPipelineConfig config() const;

    bool convertDraftToEntry(const AgentDraftProposal& proposal,
                             const crypto::PrivateKey& authorKey,
                             poe_v1::KnowledgeEntryV1* outEntry,
                             std::string* reason = nullptr) const;

    AgentSubmissionDryRunResult dryRun(const AgentDraftRecord& record,
                                       const crypto::PrivateKey& authorKey,
                                       const PoeV1Engine& engine) const;

    std::vector<AgentSubmissionBatchItem> runDeterministicBatch(AgentDraftQueue& queue,
                                                                PoeV1Engine& engine,
                                                                const crypto::PrivateKey& authorKey,
                                                                uint64_t atTimestamp) const;

private:
    AgentSubmissionPipelineConfig config_;
};

}
