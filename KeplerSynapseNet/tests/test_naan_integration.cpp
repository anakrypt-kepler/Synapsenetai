#include "core/agent_coordination.h"
#include "core/agent_draft_queue.h"
#include "core/agent_submission_pipeline.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

using namespace synapse::core;

static synapse::crypto::PrivateKey makeSk(uint8_t tag) {
    synapse::crypto::PrivateKey sk{};
    for (size_t i = 0; i < sk.size(); ++i) sk[i] = static_cast<uint8_t>(tag + i);
    return sk;
}

static AgentDraftProposal makeDraft(const synapse::crypto::PrivateKey& sk,
                                    uint64_t ts,
                                    const std::string& title,
                                    const std::string& body,
                                    uint64_t nonce) {
    AgentDraftProposal draft;
    draft.createdAt = ts;
    draft.author = synapse::crypto::derivePublicKey(sk);
    draft.title = title;
    draft.body = body;
    draft.powBits = 0;
    draft.powNonce = nonce;
    draft.signature = synapse::crypto::sign(draft.signatureHash(), sk);
    return draft;
}

static void configurePoe(PoeV1Engine& engine, const std::filesystem::path& dbPath) {
    assert(engine.open(dbPath.string()));
    PoeV1Config cfg;
    cfg.powBits = 0;
    cfg.limits.minPowBits = 0;
    cfg.limits.maxPowBits = 28;
    engine.setConfig(cfg);
}

static void testFullLoopFetchReviewDraftSubmit() {
    const auto unique = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = std::filesystem::temp_directory_path() / ("synapsenet_naan_integration_" + unique);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    PoeV1Engine engine;
    configurePoe(engine, root / "poe.db");

    CoordinationHub hub;
    assert(hub.createRoom("tasks/main", RoomType::TASKS));
    assert(hub.createRoom("reviews/main", RoomType::REVIEWS));
    assert(hub.createRoom("alerts/main", RoomType::ALERTS));

    AgentSubmissionPipelineConfig pipelineCfg;
    pipelineCfg.maxBatchSize = 8;
    AgentSubmissionPipeline pipeline(pipelineCfg);
    AgentDraftQueue queue;

    auto runtimeIdentity = AgentIdentity::fromSeed(synapse::crypto::sha256("integration_agent"));
    assert(runtimeIdentity.valid());

    uint64_t now = 1700000000;
    assert(hub.postToRoom(
        "tasks/main",
        runtimeIdentity,
        RoomMessageType::TASK,
        "{\"event\":\"fetch\",\"source\":\"https://docs.synapsenet.org/spec\",\"summary\":\"ok\"}",
        now
    ));

    auto authorSk = makeSk(11);
    auto reviewerSk = makeSk(23);
    auto draft = makeDraft(authorSk, now + 1, "CODE: Integration draft", std::string(80, 'i'), 42);
    std::string reason;
    assert(queue.enqueue(draft, &reason));

    auto first = pipeline.runDeterministicBatch(queue, engine, authorSk, now + 2);
    assert(first.size() == 1);
    assert(first[0].action == "review_required");

    auto reviewerPk = synapse::crypto::derivePublicKey(reviewerSk);
    assert(queue.upsertReview(draft.draftId(), reviewerPk, true, now + 3, "looks good", &reason));
    assert(hub.postToRoom(
        "reviews/main",
        runtimeIdentity,
        RoomMessageType::REVIEW,
        std::string("{\"draftId\":\"") + synapse::crypto::toHex(draft.draftId()) + "\",\"decision\":\"approve\"}",
        now + 3
    ));

    auto second = pipeline.runDeterministicBatch(queue, engine, authorSk, now + 4);
    assert(second.size() == 1);
    assert(second[0].action == "approved");

    auto third = pipeline.runDeterministicBatch(queue, engine, authorSk, now + 5);
    assert(third.size() == 1);
    assert(third[0].action == "submitted");
    assert(third[0].expectedAcceptanceRewardAtoms > 0);
    assert(!third[0].acceptanceRewardCredited);

    auto record = queue.get(draft.draftId());
    assert(record.has_value());
    assert(record->status == DraftStatus::SUBMITTED);
    assert(engine.totalEntries() == 1);

    auto feed = hub.getObservatoryFeed(0, 100);
    assert(feed.size() >= 2);

    engine.close();
    std::filesystem::remove_all(root, ec);
}

int main() {
    testFullLoopFetchReviewDraftSubmit();
    return 0;
}
