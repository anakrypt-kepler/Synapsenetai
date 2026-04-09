#include "core/agent_coordination.h"
#include "core/agent_draft_queue.h"

#include <cstdint>
#include <iostream>

using namespace synapse::core;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "CHECK failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    } \
} while (0)

static AgentDraftProposal makeProposal(const AgentIdentity& id,
                                       uint64_t ts,
                                       const std::string& title,
                                       const std::string& body,
                                       uint64_t nonce) {
    AgentDraftProposal p;
    p.createdAt = ts;
    p.author = id.id;
    p.title = title;
    p.body = body;
    p.powBits = 12;
    p.powNonce = nonce;
    p.signature = synapse::crypto::sign(p.signatureHash(), id.privateKey);
    return p;
}

static bool testEnqueueAndFetch() {
    AgentDraftQueue queue;
    auto id = AgentIdentity::generate();
    auto p = makeProposal(id, 1700000000, "draft title", "draft body", 1);

    std::string reason;
    CHECK(queue.enqueue(p, &reason));
    CHECK(reason == "accepted");
    CHECK(queue.size() == 1);

    auto got = queue.get(p.draftId());
    CHECK(got.has_value());
    CHECK(got->proposal.title == "draft title");
    CHECK(got->status == DraftStatus::QUEUED);
    return true;
}

static bool testDuplicateRejects() {
    AgentDraftQueue queue;
    auto id = AgentIdentity::generate();
    auto p1 = makeProposal(id, 1700000000, "t1", "b1", 1);
    auto p2 = p1;
    p2.powNonce = 2;
    p2.signature = synapse::crypto::sign(p2.signatureHash(), id.privateKey);

    std::string reason;
    CHECK(queue.enqueue(p1, &reason));

    CHECK(!queue.enqueue(p1, &reason));
    CHECK(reason == "duplicate_id");

    CHECK(!queue.enqueue(p2, &reason));
    CHECK(reason == "duplicate_content");
    return true;
}

static bool testInvalidSignatureRejected() {
    AgentDraftQueue queue;
    auto id = AgentIdentity::generate();
    auto p = makeProposal(id, 1700000000, "signed", "body", 5);

    p.body = "tampered";
    std::string reason;
    CHECK(!queue.enqueue(p, &reason));
    CHECK(reason == "bad_signature");
    return true;
}

static bool testLimitsRejected() {
    AgentDraftLimits limits;
    limits.maxTitleBytes = 4;
    limits.maxBodyBytes = 8;
    AgentDraftQueue queue(limits);

    auto id = AgentIdentity::generate();
    auto p = makeProposal(id, 1700000000, "title_too_long", "ok", 1);
    std::string reason;
    CHECK(!queue.enqueue(p, &reason));
    CHECK(reason == "too_large");

    auto p2 = makeProposal(id, 1700000001, "ok", "body_too_long_for_limit", 2);
    CHECK(!queue.enqueue(p2, &reason));
    CHECK(reason == "too_large");
    return true;
}

static bool testStatusTransitions() {
    AgentDraftQueue queue;
    auto id = AgentIdentity::generate();
    auto p = makeProposal(id, 1700000000, "draft", "body", 3);
    std::string reason;
    CHECK(queue.enqueue(p, &reason));

    CHECK(!queue.setStatus(p.draftId(), DraftStatus::SUBMITTED, 1700000001, "", &reason));
    CHECK(reason == "invalid_transition");

    CHECK(queue.setStatus(p.draftId(), DraftStatus::REVIEW_REQUIRED, 1700000002, "", &reason));
    CHECK(queue.setStatus(p.draftId(), DraftStatus::APPROVED, 1700000003, "", &reason));
    CHECK(queue.markSubmitted(p.draftId(), 1700000004, &reason));

    auto got = queue.get(p.draftId());
    CHECK(got.has_value());
    CHECK(got->status == DraftStatus::SUBMITTED);
    CHECK(got->updatedAt == 1700000004);
    return true;
}

static bool testListByStatus() {
    AgentDraftQueue queue;
    auto id = AgentIdentity::generate();

    auto p1 = makeProposal(id, 1700000000, "d1", "b1", 1);
    auto p2 = makeProposal(id, 1700000001, "d2", "b2", 2);
    auto p3 = makeProposal(id, 1700000002, "d3", "b3", 3);

    std::string reason;
    CHECK(queue.enqueue(p1, &reason));
    CHECK(queue.enqueue(p2, &reason));
    CHECK(queue.enqueue(p3, &reason));

    CHECK(queue.setStatus(p2.draftId(), DraftStatus::REJECTED, 1700000003, "spam", &reason));
    CHECK(queue.setStatus(p3.draftId(), DraftStatus::APPROVED, 1700000004, "", &reason));

    auto approved = queue.listByStatus(DraftStatus::APPROVED, 10);
    CHECK(approved.size() == 1);
    CHECK(approved[0].proposal.title == "d3");

    auto listed = queue.list(10, false);
    CHECK(listed.size() == 2);
    return true;
}

static bool testReviewUpsert() {
    AgentDraftQueue queue;
    auto author = AgentIdentity::generate();
    auto reviewerA = AgentIdentity::generate();
    auto reviewerB = AgentIdentity::generate();

    auto p = makeProposal(author, 1700000100, "reviewed", "body", 11);
    std::string reason;
    CHECK(queue.enqueue(p, &reason));

    CHECK(queue.upsertReview(p.draftId(), reviewerB.id, true, 1700000101, "ok", &reason));
    CHECK(reason == "ok");
    CHECK(queue.upsertReview(p.draftId(), reviewerA.id, false, 1700000102, "needs_work", &reason));
    CHECK(reason == "ok");

    CHECK(queue.upsertReview(p.draftId(), reviewerB.id, false, 1700000103, "updated", &reason));
    CHECK(reason == "ok");

    auto got = queue.get(p.draftId());
    CHECK(got.has_value());
    CHECK(got->reviews.size() == 2);

    const std::string reviewer0 = synapse::crypto::toHex(got->reviews[0].reviewer);
    const std::string reviewer1 = synapse::crypto::toHex(got->reviews[1].reviewer);
    CHECK(reviewer0 < reviewer1);

    bool foundUpdated = false;
    for (const auto& review : got->reviews) {
        if (review.reviewer == reviewerB.id) {
            foundUpdated = true;
            CHECK(!review.approved);
            CHECK(review.reason == "updated");
            CHECK(review.reviewedAt == 1700000103);
        }
    }
    CHECK(foundUpdated);

    CHECK(!queue.upsertReview(p.draftId(), synapse::crypto::PublicKey{}, true, 1700000104, "", &reason));
    CHECK(reason == "invalid_reviewer");
    return true;
}

static bool testIndexRecoveryNoop() {
    AgentDraftQueue queue;
    auto author = AgentIdentity::generate();

    std::string reason;
    auto p1 = makeProposal(author, 1700000200, "idx1", "body1", 21);
    auto p2 = makeProposal(author, 1700000201, "idx2", "body2", 22);
    CHECK(queue.enqueue(p1, &reason));
    CHECK(queue.enqueue(p2, &reason));

    AgentDraftQueueRecoveryStats stats;
    CHECK(queue.verifyAndRepairIndexes(&stats, &reason));
    CHECK(reason == "ok");
    CHECK(stats.records == 2);
    CHECK(stats.idIndexEntries == 2);
    CHECK(stats.contentIndexEntries == 2);
    CHECK(stats.droppedDuplicateRecords == 0);

    auto got1 = queue.get(p1.draftId());
    auto got2 = queue.get(p2.draftId());
    CHECK(got1.has_value());
    CHECK(got2.has_value());
    return true;
}

int main() {
    std::cout << "Running agent_draft_queue tests...\n";
    if (!testEnqueueAndFetch()) return 1;
    if (!testDuplicateRejects()) return 1;
    if (!testInvalidSignatureRejected()) return 1;
    if (!testLimitsRejected()) return 1;
    if (!testStatusTransitions()) return 1;
    if (!testListByStatus()) return 1;
    if (!testReviewUpsert()) return 1;
    if (!testIndexRecoveryNoop()) return 1;
    std::cout << "All agent_draft_queue tests passed!\n";
    return 0;
}
