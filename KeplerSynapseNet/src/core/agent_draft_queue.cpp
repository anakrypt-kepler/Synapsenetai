#include "core/agent_draft_queue.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace synapse::core {

namespace {

void writeU8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void writeU64LE(std::vector<uint8_t>& out, uint64_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
}

void writeVarBytes(std::vector<uint8_t>& out, const std::string& s) {
    writeU32LE(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

std::string idKey(const crypto::Hash256& h) {
    return crypto::toHex(h);
}

} // namespace

std::vector<uint8_t> AgentDraftProposal::canonicalBytes() const {
    std::vector<uint8_t> out;
    out.reserve(128 + title.size() + body.size() + citations.size() * 32);

    writeU8(out, version);
    writeU64LE(out, createdAt);
    out.insert(out.end(), author.begin(), author.end());
    writeVarBytes(out, title);
    writeVarBytes(out, body);
    writeU32LE(out, static_cast<uint32_t>(citations.size()));
    for (const auto& citation : citations) {
        out.insert(out.end(), citation.begin(), citation.end());
    }
    writeU32LE(out, powBits);
    return out;
}

crypto::Hash256 AgentDraftProposal::contentHash() const {
    auto bytes = canonicalBytes();
    return crypto::sha256(bytes.data(), bytes.size());
}

crypto::Hash256 AgentDraftProposal::draftId() const {
    crypto::Hash256 content = contentHash();
    std::vector<uint8_t> buf;
    buf.reserve(content.size() + sizeof(uint64_t));
    buf.insert(buf.end(), content.begin(), content.end());
    writeU64LE(buf, powNonce);
    return crypto::sha256(buf.data(), buf.size());
}

crypto::Hash256 AgentDraftProposal::signatureHash() const {
    auto bytes = canonicalBytes();
    writeU64LE(bytes, powNonce);
    return crypto::sha256(bytes.data(), bytes.size());
}

bool AgentDraftProposal::verifySignature(std::string* reason) const {
    if (!crypto::verify(signatureHash(), signature, author)) {
        if (reason) *reason = "bad_signature";
        return false;
    }
    return true;
}

AgentDraftQueue::AgentDraftQueue(const AgentDraftLimits& limits)
    : limits_(limits) {}

bool AgentDraftQueue::validateProposal(const AgentDraftProposal& proposal, std::string* reason) const {
    if (proposal.version != 1) {
        if (reason) *reason = "unsupported_version";
        return false;
    }
    if (proposal.createdAt == 0) {
        if (reason) *reason = "bad_timestamp";
        return false;
    }
    if (proposal.author == crypto::PublicKey{}) {
        if (reason) *reason = "empty_author";
        return false;
    }
    if (proposal.title.empty() || proposal.body.empty()) {
        if (reason) *reason = "empty_fields";
        return false;
    }
    if (proposal.title.size() > limits_.maxTitleBytes || proposal.body.size() > limits_.maxBodyBytes) {
        if (reason) *reason = "too_large";
        return false;
    }
    if (proposal.citations.size() > limits_.maxCitations) {
        if (reason) *reason = "too_many_citations";
        return false;
    }
    return proposal.verifySignature(reason);
}

bool AgentDraftQueue::enqueue(const AgentDraftProposal& proposal, std::string* reason) {
    std::string validateReason;
    if (!validateProposal(proposal, &validateReason)) {
        if (reason) *reason = validateReason;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (drafts_.size() >= limits_.maxQueueSize) {
        if (reason) *reason = "queue_full";
        return false;
    }

    std::string id = idKey(proposal.draftId());
    if (byId_.count(id) > 0) {
        if (reason) *reason = "duplicate_id";
        return false;
    }

    std::string content = idKey(proposal.contentHash());
    if (byContentHash_.count(content) > 0) {
        if (reason) *reason = "duplicate_content";
        return false;
    }

    AgentDraftRecord rec;
    rec.proposal = proposal;
    rec.status = DraftStatus::QUEUED;
    rec.updatedAt = proposal.createdAt;

    byId_[id] = drafts_.size();
    byContentHash_[content] = id;
    drafts_.push_back(rec);
    if (reason) *reason = "accepted";
    return true;
}

bool AgentDraftQueue::isTransitionAllowed(DraftStatus from, DraftStatus to) const {
    if (from == to) return true;
    switch (from) {
        case DraftStatus::QUEUED:
            return to == DraftStatus::REVIEW_REQUIRED || to == DraftStatus::APPROVED || to == DraftStatus::REJECTED;
        case DraftStatus::REVIEW_REQUIRED:
            return to == DraftStatus::APPROVED || to == DraftStatus::REJECTED;
        case DraftStatus::APPROVED:
            return to == DraftStatus::SUBMITTED || to == DraftStatus::REJECTED;
        case DraftStatus::REJECTED:
            return to == DraftStatus::REVIEW_REQUIRED;
        case DraftStatus::SUBMITTED:
            return false;
    }
    return false;
}

bool AgentDraftQueue::setStatus(const crypto::Hash256& draftId,
                                DraftStatus status,
                                uint64_t atTimestamp,
                                const std::string& reason,
                                std::string* outReason) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = byId_.find(idKey(draftId));
    if (it == byId_.end()) {
        if (outReason) *outReason = "not_found";
        return false;
    }

    AgentDraftRecord& rec = drafts_[it->second];
    if (!isTransitionAllowed(rec.status, status)) {
        if (outReason) *outReason = "invalid_transition";
        return false;
    }

    rec.status = status;
    rec.updatedAt = atTimestamp;
    if (status == DraftStatus::REJECTED) {
        rec.rejectReason = reason.empty() ? "rejected" : reason;
    } else if (!reason.empty()) {
        rec.rejectReason = reason;
    } else if (status != DraftStatus::REJECTED) {
        rec.rejectReason.clear();
    }
    if (outReason) *outReason = "ok";
    return true;
}

bool AgentDraftQueue::upsertReview(const crypto::Hash256& draftId,
                                   const crypto::PublicKey& reviewer,
                                   bool approved,
                                   uint64_t atTimestamp,
                                   const std::string& reason,
                                   std::string* outReason) {
    if (reviewer == crypto::PublicKey{}) {
        if (outReason) *outReason = "invalid_reviewer";
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = byId_.find(idKey(draftId));
    if (it == byId_.end()) {
        if (outReason) *outReason = "not_found";
        return false;
    }

    AgentDraftRecord& rec = drafts_[it->second];
    if (rec.status == DraftStatus::SUBMITTED) {
        if (outReason) *outReason = "already_submitted";
        return false;
    }

    auto rit = std::find_if(rec.reviews.begin(), rec.reviews.end(),
                            [&](const AgentDraftReview& review) {
                                return review.reviewer == reviewer;
                            });
    if (rit == rec.reviews.end()) {
        AgentDraftReview review;
        review.reviewer = reviewer;
        review.approved = approved;
        review.reviewedAt = atTimestamp;
        review.reason = reason;
        rec.reviews.push_back(std::move(review));
    } else {
        rit->approved = approved;
        rit->reviewedAt = atTimestamp;
        rit->reason = reason;
    }

    std::sort(rec.reviews.begin(), rec.reviews.end(), [](const AgentDraftReview& a, const AgentDraftReview& b) {
        if (a.reviewer != b.reviewer) return a.reviewer < b.reviewer;
        if (a.reviewedAt != b.reviewedAt) return a.reviewedAt < b.reviewedAt;
        if (a.approved != b.approved) return a.approved < b.approved;
        return a.reason < b.reason;
    });

    rec.updatedAt = atTimestamp;
    if (outReason) *outReason = "ok";
    return true;
}

bool AgentDraftQueue::markSubmitted(const crypto::Hash256& draftId, uint64_t atTimestamp, std::string* reason) {
    return setStatus(draftId, DraftStatus::SUBMITTED, atTimestamp, "", reason);
}

std::optional<AgentDraftRecord> AgentDraftQueue::get(const crypto::Hash256& draftId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = byId_.find(idKey(draftId));
    if (it == byId_.end()) return std::nullopt;
    return drafts_[it->second];
}

std::vector<AgentDraftRecord> AgentDraftQueue::list(uint32_t limit, bool includeRejected) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (limit == 0) return {};

    std::vector<AgentDraftRecord> out;
    out.reserve(std::min<size_t>(drafts_.size(), limit));
    for (auto it = drafts_.rbegin(); it != drafts_.rend() && out.size() < limit; ++it) {
        if (!includeRejected && it->status == DraftStatus::REJECTED) continue;
        out.push_back(*it);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<AgentDraftRecord> AgentDraftQueue::listByStatus(DraftStatus status, uint32_t limit) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (limit == 0) return {};

    std::vector<AgentDraftRecord> out;
    out.reserve(std::min<size_t>(drafts_.size(), limit));
    for (auto it = drafts_.rbegin(); it != drafts_.rend() && out.size() < limit; ++it) {
        if (it->status == status) {
            out.push_back(*it);
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

uint64_t AgentDraftQueue::pruneSubmitted(uint32_t maxKeep) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (maxKeep == 0) return 0;

    uint64_t submitted = 0;
    for (const auto& rec : drafts_) {
        if (rec.status == DraftStatus::SUBMITTED) {
            submitted += 1;
        }
    }
    if (submitted <= maxKeep) return 0;

    uint64_t toDrop = submitted - maxKeep;
    uint64_t dropped = 0;

    std::vector<AgentDraftRecord> kept;
    kept.reserve(drafts_.size() - static_cast<size_t>(toDrop));
    for (const auto& rec : drafts_) {
        if (rec.status == DraftStatus::SUBMITTED && dropped < toDrop) {
            dropped += 1;
            continue;
        }
        kept.push_back(rec);
    }

    drafts_.swap(kept);
    byId_.clear();
    byContentHash_.clear();
    for (size_t i = 0; i < drafts_.size(); ++i) {
        const std::string id = idKey(drafts_[i].proposal.draftId());
        const std::string content = idKey(drafts_[i].proposal.contentHash());
        byId_[id] = i;
        byContentHash_[content] = id;
    }
    return dropped;
}

bool AgentDraftQueue::verifyAndRepairIndexes(AgentDraftQueueRecoveryStats* stats,
                                             std::string* reason) {
    std::lock_guard<std::mutex> lock(mtx_);

    AgentDraftQueueRecoveryStats out;
    out.records = drafts_.size();
    out.idIndexEntries = byId_.size();
    out.contentIndexEntries = byContentHash_.size();

    std::set<size_t> dropIndices;
    std::unordered_map<std::string, size_t> nextById;
    std::unordered_map<std::string, std::string> nextByContent;
    nextById.reserve(drafts_.size());
    nextByContent.reserve(drafts_.size());

    for (size_t i = 0; i < drafts_.size(); ++i) {
        const std::string id = idKey(drafts_[i].proposal.draftId());
        const std::string content = idKey(drafts_[i].proposal.contentHash());

        if (nextById.count(id) > 0 || nextByContent.count(content) > 0) {
            dropIndices.insert(i);
            continue;
        }

        nextById[id] = i - dropIndices.size();
        nextByContent[content] = id;
    }

    if (!dropIndices.empty()) {
        std::vector<AgentDraftRecord> kept;
        kept.reserve(drafts_.size() - dropIndices.size());
        for (size_t i = 0; i < drafts_.size(); ++i) {
            if (dropIndices.count(i) == 0) kept.push_back(drafts_[i]);
        }
        drafts_.swap(kept);

        nextById.clear();
        nextByContent.clear();
        for (size_t i = 0; i < drafts_.size(); ++i) {
            const std::string id = idKey(drafts_[i].proposal.draftId());
            const std::string content = idKey(drafts_[i].proposal.contentHash());
            nextById[id] = i;
            nextByContent[content] = id;
        }
    }

    const bool idChanged = (byId_ != nextById);
    const bool contentChanged = (byContentHash_ != nextByContent);
    byId_ = std::move(nextById);
    byContentHash_ = std::move(nextByContent);

    out.droppedDuplicateRecords = dropIndices.size();
    out.records = drafts_.size();
    out.idIndexEntries = byId_.size();
    out.contentIndexEntries = byContentHash_.size();
    out.rebuiltIndexEntries = out.idIndexEntries + out.contentIndexEntries;

    if (stats) *stats = out;
    if (reason) {
        if (dropIndices.empty() && !idChanged && !contentChanged) {
            *reason = "ok";
        } else {
            *reason = "repaired";
        }
    }
    return true;
}

uint64_t AgentDraftQueue::size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return drafts_.size();
}

std::string draftStatusToString(DraftStatus status) {
    switch (status) {
        case DraftStatus::QUEUED: return "queued";
        case DraftStatus::REVIEW_REQUIRED: return "review_required";
        case DraftStatus::APPROVED: return "approved";
        case DraftStatus::REJECTED: return "rejected";
        case DraftStatus::SUBMITTED: return "submitted";
    }
    return "queued";
}

DraftStatus stringToDraftStatus(const std::string& status) {
    if (status == "queued") return DraftStatus::QUEUED;
    if (status == "review_required") return DraftStatus::REVIEW_REQUIRED;
    if (status == "approved") return DraftStatus::APPROVED;
    if (status == "rejected") return DraftStatus::REJECTED;
    if (status == "submitted") return DraftStatus::SUBMITTED;
    return DraftStatus::QUEUED;
}

} // namespace synapse::core
