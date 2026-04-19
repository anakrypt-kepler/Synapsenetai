#pragma once

#include "crypto/crypto.h"
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace synapse::core {

enum class DraftStatus : uint8_t {
    QUEUED = 0,
    REVIEW_REQUIRED = 1,
    APPROVED = 2,
    REJECTED = 3,
    SUBMITTED = 4
};

struct AgentDraftLimits {
    uint32_t maxTitleBytes = 512;
    uint32_t maxBodyBytes = 65536;
    uint32_t maxCitations = 64;
    uint32_t maxQueueSize = 2048;
};

struct AgentDraftProposal {
    uint8_t version = 1;
    uint64_t createdAt = 0;
    crypto::PublicKey author{};
    std::string title;
    std::string body;
    std::vector<crypto::Hash256> citations;
    uint32_t powBits = 0;
    uint64_t powNonce = 0;
    crypto::Signature signature{};

    std::vector<uint8_t> canonicalBytes() const;
    crypto::Hash256 contentHash() const;
    crypto::Hash256 draftId() const;
    crypto::Hash256 signatureHash() const;
    bool verifySignature(std::string* reason = nullptr) const;
};

struct AgentDraftReview {
    crypto::PublicKey reviewer{};
    bool approved = false;
    uint64_t reviewedAt = 0;
    std::string reason;
};

struct AgentDraftRecord {
    AgentDraftProposal proposal;
    DraftStatus status = DraftStatus::QUEUED;
    std::string rejectReason;
    std::vector<AgentDraftReview> reviews;
    uint64_t updatedAt = 0;
};

struct AgentDraftQueueRecoveryStats {
    uint64_t records = 0;
    uint64_t idIndexEntries = 0;
    uint64_t contentIndexEntries = 0;
    uint64_t droppedDuplicateRecords = 0;
    uint64_t rebuiltIndexEntries = 0;
};

class AgentDraftQueue {
public:
    explicit AgentDraftQueue(const AgentDraftLimits& limits = AgentDraftLimits{});

    bool enqueue(const AgentDraftProposal& proposal, std::string* reason = nullptr);
    bool setStatus(const crypto::Hash256& draftId,
                   DraftStatus status,
                   uint64_t atTimestamp,
                   const std::string& reason = "",
                   std::string* outReason = nullptr);
    bool upsertReview(const crypto::Hash256& draftId,
                      const crypto::PublicKey& reviewer,
                      bool approved,
                      uint64_t atTimestamp,
                      const std::string& reason = "",
                      std::string* outReason = nullptr);
    bool markSubmitted(const crypto::Hash256& draftId, uint64_t atTimestamp, std::string* reason = nullptr);

    std::optional<AgentDraftRecord> get(const crypto::Hash256& draftId) const;
    std::vector<AgentDraftRecord> list(uint32_t limit = 100, bool includeRejected = true) const;
    std::vector<AgentDraftRecord> listByStatus(DraftStatus status, uint32_t limit = 100) const;
    uint64_t pruneSubmitted(uint32_t maxKeep);

    bool verifyAndRepairIndexes(AgentDraftQueueRecoveryStats* stats = nullptr,
                                std::string* reason = nullptr);

    uint64_t size() const;

private:
    AgentDraftLimits limits_;
    mutable std::mutex mtx_;
    std::vector<AgentDraftRecord> drafts_;
    std::unordered_map<std::string, size_t> byId_;
    std::unordered_map<std::string, std::string> byContentHash_;

    bool validateProposal(const AgentDraftProposal& proposal, std::string* reason = nullptr) const;
    bool isTransitionAllowed(DraftStatus from, DraftStatus to) const;
};

std::string draftStatusToString(DraftStatus status);
DraftStatus stringToDraftStatus(const std::string& status);

}
