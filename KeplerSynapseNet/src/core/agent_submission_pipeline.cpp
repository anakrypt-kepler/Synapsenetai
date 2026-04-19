#include "core/agent_submission_pipeline.h"
#include "core/poe_v1.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace synapse::core {

namespace {

poe_v1::ContentType inferContentType(const AgentDraftProposal& proposal) {
    if (proposal.title.rfind("CODE:", 0) == 0 || proposal.title.rfind("code:", 0) == 0) {
        return poe_v1::ContentType::CODE;
    }
    if (proposal.body.find("\nQ:") != std::string::npos && proposal.body.find("\nA:") != std::string::npos) {
        return poe_v1::ContentType::QA;
    }
    if (proposal.body.rfind("```", 0) == 0) {
        return poe_v1::ContentType::CODE;
    }
    return poe_v1::ContentType::TEXT;
}

std::vector<AgentDraftRecord> collectCandidates(const AgentDraftQueue& queue, bool includeReviewRequired) {
    std::vector<AgentDraftRecord> out = queue.listByStatus(DraftStatus::QUEUED, 1000000);
    if (includeReviewRequired) {
        auto review = queue.listByStatus(DraftStatus::REVIEW_REQUIRED, 1000000);
        out.insert(out.end(), review.begin(), review.end());
    }
    auto approved = queue.listByStatus(DraftStatus::APPROVED, 1000000);
    out.insert(out.end(), approved.begin(), approved.end());

    std::sort(out.begin(), out.end(), [](const AgentDraftRecord& a, const AgentDraftRecord& b) {
        const std::string ah = crypto::toHex(a.proposal.draftId());
        const std::string bh = crypto::toHex(b.proposal.draftId());
        if (ah != bh) return ah < bh;
        if (a.proposal.createdAt != b.proposal.createdAt) return a.proposal.createdAt < b.proposal.createdAt;
        return a.proposal.title < b.proposal.title;
    });
    return out;
}

bool hash256Less(const crypto::Hash256& a, const crypto::Hash256& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

std::vector<crypto::Hash256> normalizeCitations(const std::vector<crypto::Hash256>& citations, bool* hadDuplicates = nullptr) {
    std::vector<crypto::Hash256> out = citations;
    std::sort(out.begin(), out.end(), hash256Less);
    auto it = std::unique(out.begin(), out.end());
    const bool duplicates = it != out.end();
    out.erase(it, out.end());
    if (hadDuplicates) *hadDuplicates = duplicates;
    return out;
}

bool sharesNoveltyBucket(uint64_t a, uint64_t b, uint32_t bands) {
    for (uint32_t band = 0; band < bands; ++band) {
        const uint8_t aa = static_cast<uint8_t>((a >> (band * 4)) & 0x0F);
        const uint8_t bb = static_cast<uint8_t>((b >> (band * 4)) & 0x0F);
        if (aa == bb) return true;
    }
    return false;
}

bool passesNoveltyBucketDuplicateGate(const poe_v1::KnowledgeEntryV1& entry,
                                      const PoeV1Engine& engine,
                                      std::string* reason) {
    PoeV1Config cfg = engine.getConfig();
    uint32_t bands = cfg.noveltyBands;
    if (bands == 0) return true;
    if (bands > 16) bands = 16;

    uint32_t maxH = cfg.noveltyMaxHamming;
    if (maxH > 64) maxH = 64;

    const uint64_t candidateSimhash = entry.contentSimhash64();
    auto entryIds = engine.listEntryIds(0);
    for (const auto& submitId : entryIds) {
        auto existing = engine.getEntry(submitId);
        if (!existing.has_value()) continue;

        const uint64_t existingSimhash = existing->contentSimhash64();
        if (!sharesNoveltyBucket(candidateSimhash, existingSimhash, bands)) continue;

        if (poe_v1::hammingDistance64(candidateSimhash, existingSimhash) <= maxH) {
            if (reason) *reason = "duplicate_gate_novelty";
            return false;
        }
    }
    return true;
}

bool passesCitationGraphDuplicateGate(const poe_v1::KnowledgeEntryV1& entry,
                                      const AgentSubmissionPipelineConfig& config,
                                      const PoeV1Engine& engine,
                                      std::string* reason) {
    const auto candidateCitations = normalizeCitations(entry.citations);
    if (candidateCitations.empty()) return true;

    uint32_t maxH = config.duplicateCitationGraphMaxHamming;
    if (maxH > 64) maxH = 64;
    const uint64_t candidateSimhash = entry.contentSimhash64();

    auto entryIds = engine.listEntryIds(0);
    for (const auto& submitId : entryIds) {
        auto existing = engine.getEntry(submitId);
        if (!existing.has_value()) continue;
        if (normalizeCitations(existing->citations) != candidateCitations) continue;

        if (poe_v1::hammingDistance64(candidateSimhash, existing->contentSimhash64()) <= maxH) {
            if (reason) *reason = "duplicate_gate_citation_graph";
            return false;
        }
    }
    return true;
}

bool hasIntraSetCitationCycle(const std::map<std::string, poe_v1::KnowledgeEntryV1>& entriesByCitationId,
                              uint32_t* outIntraSetEdges = nullptr) {
    std::map<std::string, std::vector<std::string>> adjacency;
    uint32_t edgeCount = 0;
    for (const auto& [cidHex, entry] : entriesByCitationId) {
        auto& edges = adjacency[cidHex];
        for (const auto& cited : entry.citations) {
            const std::string citedHex = crypto::toHex(cited);
            if (citedHex == cidHex) continue;
            if (entriesByCitationId.find(citedHex) == entriesByCitationId.end()) continue;
            edges.push_back(citedHex);
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        edgeCount += static_cast<uint32_t>(edges.size());
    }

    if (outIntraSetEdges) *outIntraSetEdges = edgeCount;

    std::map<std::string, uint8_t> state;
    for (const auto& [cidHex, _] : entriesByCitationId) {
        state[cidHex] = 0;
    }

    std::function<bool(const std::string&)> dfs = [&](const std::string& node) {
        state[node] = 1;
        auto it = adjacency.find(node);
        if (it != adjacency.end()) {
            for (const auto& next : it->second) {
                const auto stIt = state.find(next);
                if (stIt == state.end()) continue;
                if (stIt->second == 1) return true;
                if (stIt->second == 0 && dfs(next)) return true;
            }
        }
        state[node] = 2;
        return false;
    };

    for (const auto& [cidHex, _] : entriesByCitationId) {
        if (state[cidHex] == 0 && dfs(cidHex)) return true;
    }
    return false;
}

bool passesCitationSanityGate(const poe_v1::KnowledgeEntryV1& entry,
                              const AgentSubmissionPipelineConfig& config,
                              const PoeV1Engine& engine,
                              std::string* reason) {
    if (entry.citations.size() > config.citationSanityMaxCitations) {
        if (reason) *reason = "citation_cap_exceeded";
        return false;
    }

    bool hadDuplicates = false;
    const auto normalized = normalizeCitations(entry.citations, &hadDuplicates);
    if (config.citationSanityRejectDuplicateCitations && hadDuplicates) {
        if (reason) *reason = "citation_duplicate";
        return false;
    }

    if (config.citationSanityRejectSelfReference) {
        const auto contentId = entry.contentId();
        if (std::binary_search(normalized.begin(), normalized.end(), contentId, hash256Less)) {
            if (reason) *reason = "citation_self_reference";
            return false;
        }
    }

    uint32_t knownCitations = 0;
    std::set<crypto::PublicKey> distinctAuthors;
    std::map<std::string, poe_v1::KnowledgeEntryV1> citedEntries;
    for (const auto& citation : normalized) {
        const std::string citationHex = crypto::toHex(citation);
        auto cited = engine.getEntryByContentId(citation);
        if (!cited.has_value()) {
            if (config.citationSanityRequireKnownCitations) {
                if (reason) *reason = "citation_unknown";
                return false;
            }
            continue;
        }

        knownCitations += 1;
        distinctAuthors.insert(cited->authorPubKey);
        citedEntries[citationHex] = *cited;
    }

    if (!normalized.empty() && knownCitations < config.citationSanityMinCorroboratingCitations) {
        if (reason) *reason = "citation_min_corroboration";
        return false;
    }
    if (!normalized.empty() && distinctAuthors.size() < config.citationSanityMinDistinctCitationAuthors) {
        if (reason) *reason = "citation_min_distinct_authors";
        return false;
    }
    if (config.citationSanityRejectIntraSetCycles) {
        const auto candidateContentId = entry.contentId();
        for (const auto& [_, cited] : citedEntries) {
            if (std::find(cited.citations.begin(), cited.citations.end(), candidateContentId) != cited.citations.end()) {
                if (reason) *reason = "citation_ring_detected";
                return false;
            }
        }
        uint32_t intraSetEdges = 0;
        const bool hasCycle = citedEntries.size() > 1 && hasIntraSetCitationCycle(citedEntries, &intraSetEdges);
        if (intraSetEdges > config.citationSanityMaxIntraSetEdges) {
            if (reason) *reason = "citation_ring_detected";
            return false;
        }
        if (hasCycle) {
            if (reason) *reason = "citation_ring_detected";
            return false;
        }
    }

    return true;
}

bool passesDuplicateGates(const poe_v1::KnowledgeEntryV1& entry,
                          const AgentSubmissionPipelineConfig& config,
                          const PoeV1Engine& engine,
                          std::string* reason) {
    if (config.duplicateGateContentId && engine.getSubmitIdByContentId(entry.contentId()).has_value()) {
        if (reason) *reason = "duplicate_gate_contentid";
        return false;
    }

    if (config.duplicateGateCitationGraph &&
        !passesCitationGraphDuplicateGate(entry, config, engine, reason)) {
        return false;
    }

    if (config.duplicateGateNoveltyBuckets &&
        !passesNoveltyBucketDuplicateGate(entry, engine, reason)) {
        return false;
    }

    return true;
}

bool passesContributionIntentGate(const poe_v1::KnowledgeEntryV1& entry,
                                  const AgentSubmissionPipelineConfig& config,
                                  std::string* reason) {
    if (entry.title.size() < config.intentMinTitleBytes) {
        if (reason) *reason = "intent_title_too_small";
        return false;
    }
    if (entry.body.size() < config.intentMinBodyBytes) {
        if (reason) *reason = "intent_body_too_small";
        return false;
    }
    if (entry.contentType == poe_v1::ContentType::TEXT && config.intentRequireCitationForText) {
        if (entry.citations.size() < config.intentMinCitationsForText) {
            if (reason) *reason = "intent_missing_citations";
            return false;
        }
    }
    if (reason) *reason = "ok";
    return true;
}

uint32_t approvedDistinctReviewers(const AgentDraftRecord& record, bool requireNonAuthorReviewer) {
    std::set<crypto::PublicKey> reviewers;
    for (const auto& review : record.reviews) {
        if (!review.approved) continue;
        if (review.reviewer == crypto::PublicKey{}) continue;
        if (requireNonAuthorReviewer && review.reviewer == record.proposal.author) continue;
        reviewers.insert(review.reviewer);
    }
    return static_cast<uint32_t>(reviewers.size());
}

bool reviewerDiversitySatisfied(const AgentDraftRecord& record,
                                const AgentSubmissionPipelineConfig& config,
                                std::string* reason) {
    if (config.minDistinctReviewers == 0) {
        if (reason) *reason = "ok";
        return true;
    }

    const uint32_t distinct = approvedDistinctReviewers(record, config.requireNonAuthorReviewer);
    if (distinct < config.minDistinctReviewers) {
        if (reason) *reason = "review_diversity_insufficient";
        return false;
    }

    if (reason) *reason = "ok";
    return true;
}

} // namespace

AgentSubmissionPipeline::AgentSubmissionPipeline(const AgentSubmissionPipelineConfig& config)
    : config_(config) {}

AgentSubmissionPipelineConfig AgentSubmissionPipeline::config() const {
    return config_;
}

bool AgentSubmissionPipeline::convertDraftToEntry(const AgentDraftProposal& proposal,
                                                  const crypto::PrivateKey& authorKey,
                                                  poe_v1::KnowledgeEntryV1* outEntry,
                                                  std::string* reason) const {
    if (!outEntry) {
        if (reason) *reason = "null_output";
        return false;
    }

    const crypto::PublicKey expectedAuthor = crypto::derivePublicKey(authorKey);
    if (expectedAuthor != proposal.author) {
        if (reason) *reason = "author_mismatch";
        return false;
    }

    poe_v1::KnowledgeEntryV1 entry;
    entry.version = 1;
    entry.timestamp = proposal.createdAt;
    entry.authorPubKey = proposal.author;
    entry.contentType = inferContentType(proposal);
    entry.title = proposal.title;
    entry.body = proposal.body;
    entry.citations = proposal.citations;
    entry.powNonce = proposal.powNonce;
    entry.powBits = proposal.powBits;

    if (!poe_v1::signKnowledgeEntryV1(entry, authorKey)) {
        if (reason) *reason = "sign_failed";
        return false;
    }

    *outEntry = std::move(entry);
    if (reason) *reason = "ok";
    return true;
}

AgentSubmissionDryRunResult AgentSubmissionPipeline::dryRun(const AgentDraftRecord& record,
                                                            const crypto::PrivateKey& authorKey,
                                                            const PoeV1Engine& engine) const {
    AgentSubmissionDryRunResult out;
    out.draftId = record.proposal.draftId();

    std::string reason;
    if (!convertDraftToEntry(record.proposal, authorKey, &out.entry, &reason)) {
        out.ok = false;
        out.reason = "convert_" + reason;
        out.nextStatus = "rejected";
        return out;
    }

    if (!passesContributionIntentGate(out.entry, config_, &reason)) {
        out.ok = false;
        out.reason = reason;
        out.nextStatus = "rejected";
        return out;
    }

    if (!passesDuplicateGates(out.entry, config_, engine, &reason)) {
        out.ok = false;
        out.reason = reason;
        out.nextStatus = "rejected";
        return out;
    }

    if (!passesCitationSanityGate(out.entry, config_, engine, &reason)) {
        out.ok = false;
        out.reason = reason;
        out.nextStatus = "rejected";
        return out;
    }

    if (!engine.precheckEntry(out.entry, &reason)) {
        out.ok = false;
        out.reason = "precheck_" + reason;
        out.nextStatus = "rejected";
        return out;
    }

    out.ok = true;
    out.reason = "ok";
    if (record.status == DraftStatus::APPROVED) {
        out.nextStatus = "submitted";
        return out;
    }

    if (record.status == DraftStatus::QUEUED) {
        out.nextStatus = "review_required";
        return out;
    }

    std::string reviewReason;
    if (!reviewerDiversitySatisfied(record, config_, &reviewReason)) {
        out.reason = reviewReason;
        out.nextStatus = "review_required";
        return out;
    }

    out.nextStatus = "approved";
    return out;
}

std::vector<AgentSubmissionBatchItem> AgentSubmissionPipeline::runDeterministicBatch(AgentDraftQueue& queue,
                                                                                      PoeV1Engine& engine,
                                                                                      const crypto::PrivateKey& authorKey,
                                                                                      uint64_t atTimestamp) const {
    const auto candidates = collectCandidates(queue, config_.includeReviewRequired);
    std::vector<AgentSubmissionBatchItem> results;
    results.reserve(std::min<size_t>(candidates.size(), config_.maxBatchSize));

    for (const auto& c : candidates) {
        if (results.size() >= config_.maxBatchSize) break;

        const crypto::Hash256 draftId = c.proposal.draftId();
        auto latest = queue.get(draftId);
        if (!latest.has_value()) continue;
        if (latest->status != DraftStatus::QUEUED &&
            latest->status != DraftStatus::REVIEW_REQUIRED &&
            latest->status != DraftStatus::APPROVED) {
            continue;
        }

        AgentSubmissionBatchItem item;
        item.draftId = draftId;

        auto dry = dryRun(*latest, authorKey, engine);
        if (!dry.ok) {
            std::string setReason;
            const bool changed = queue.setStatus(draftId, DraftStatus::REJECTED, atTimestamp, dry.reason, &setReason);
            item.ok = changed;
            item.action = "rejected";
            item.reason = changed ? dry.reason : setReason;
            results.push_back(item);
            continue;
        }

        item.submitId = dry.entry.submitId();
        item.contentId = dry.entry.contentId();
        item.expectedAcceptanceRewardAtoms = engine.calculateAcceptanceReward(dry.entry);

        if (latest->status == DraftStatus::QUEUED) {
            std::string setReason;
            const bool changed = queue.setStatus(draftId, DraftStatus::REVIEW_REQUIRED, atTimestamp, "", &setReason);
            item.ok = changed;
            item.action = "review_required";
            item.reason = changed ? "ok" : setReason;
            results.push_back(item);
            continue;
        }

        if (latest->status == DraftStatus::REVIEW_REQUIRED) {
            std::string reviewReason;
            if (!reviewerDiversitySatisfied(*latest, config_, &reviewReason)) {
                item.ok = true;
                item.action = "review_pending";
                item.reason = reviewReason;
                results.push_back(item);
                continue;
            }

            std::string setReason;
            const bool changed = queue.setStatus(draftId, DraftStatus::APPROVED, atTimestamp, "", &setReason);
            item.ok = changed;
            item.action = "approved";
            item.reason = changed ? "ok" : setReason;
            results.push_back(item);
            continue;
        }

        std::string importReason;
        if (!engine.importEntry(dry.entry, &importReason)) {
            std::string setReason;
            const bool changed = queue.setStatus(draftId, DraftStatus::REJECTED, atTimestamp, "submit_" + importReason, &setReason);
            item.ok = changed;
            item.action = "rejected";
            item.reason = changed ? "submit_" + importReason : setReason;
            results.push_back(item);
            continue;
        }

        std::string submitReason;
        if (!queue.markSubmitted(draftId, atTimestamp, &submitReason)) {
            item.ok = false;
            item.action = "submit_error";
            item.reason = submitReason;
            results.push_back(item);
            continue;
        }

        item.ok = true;
        item.action = "submitted";
        item.reason = "ok";
        results.push_back(item);
    }

    return results;
}

} // namespace synapse::core
