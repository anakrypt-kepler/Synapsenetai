#include "core/agent_submission_pipeline.h"
#include "core/agent_coordination.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#ifdef assert
#undef assert
#endif
#define assert(cond) do { \
    if (!(cond)) { \
        std::cerr << "assert failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while (0)

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
    AgentDraftProposal d;
    d.createdAt = ts;
    d.author = synapse::crypto::derivePublicKey(sk);
    d.title = title;
    d.body = body;
    d.powBits = 0;
    d.powNonce = nonce;
    d.signature = synapse::crypto::sign(d.signatureHash(), sk);
    return d;
}

static void setupEngine(PoeV1Engine* engineOut, std::filesystem::path* tmpOut) {
    const auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path tmpDir = std::filesystem::temp_directory_path() /
                                   ("synapsenet_agent_submission_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);

    assert(engineOut != nullptr);
    assert(engineOut->open((tmpDir / "poe.db").string()));

    PoeV1Config cfg;
    cfg.powBits = 0;
    cfg.limits.minPowBits = 0;
    cfg.limits.maxPowBits = 28;
    engineOut->setConfig(cfg);

    *tmpOut = tmpDir;
}

static synapse::core::poe_v1::KnowledgeEntryV1 importDraftEntry(const AgentSubmissionPipeline& pipeline,
                                                                 PoeV1Engine& engine,
                                                                 const AgentDraftProposal& proposal,
                                                                 const synapse::crypto::PrivateKey& sk) {
    synapse::core::poe_v1::KnowledgeEntryV1 entry;
    std::string reason;
    assert(pipeline.convertDraftToEntry(proposal, sk, &entry, &reason));
    assert(reason == "ok");
    assert(engine.importEntry(entry, &reason));
    return entry;
}

static void testDeterministicConversion() {
    AgentSubmissionPipeline pipeline;
    auto sk = makeSk(7);
    auto d = makeDraft(sk, 1700000100, "CODE: deterministic", "```cpp\nint x = 1;\n```", 10);

    synapse::core::poe_v1::KnowledgeEntryV1 e1;
    synapse::core::poe_v1::KnowledgeEntryV1 e2;
    std::string reason;
    assert(pipeline.convertDraftToEntry(d, sk, &e1, &reason));
    assert(reason == "ok");
    assert(pipeline.convertDraftToEntry(d, sk, &e2, &reason));

    assert(e1.submitId() == e2.submitId());
    assert(e1.contentId() == e2.contentId());
    assert(e1.authorPubKey == d.author);
    assert(e1.contentType == synapse::core::poe_v1::ContentType::CODE);
}

static void testDryRunRejectReasons() {
    AgentSubmissionPipelineConfig cfg;
    cfg.intentMinTitleBytes = 1;
    cfg.intentMinBodyBytes = 1;
    AgentSubmissionPipeline pipeline(cfg);
    std::filesystem::path tmpDir;
    PoeV1Engine engine;
    setupEngine(&engine, &tmpDir);

    auto skA = makeSk(1);
    auto skB = makeSk(2);

    AgentDraftRecord mismatch;
    mismatch.proposal = makeDraft(skA, 1700000200, "t", "b", 20);
    mismatch.status = DraftStatus::QUEUED;
    auto mismatchResult = pipeline.dryRun(mismatch, skB, engine);
    assert(!mismatchResult.ok);
    assert(mismatchResult.reason == "convert_author_mismatch");

    AgentDraftRecord first;
    first.proposal = makeDraft(skA, 1700000201, "duplicate_title", std::string(80, 'd'), 21);
    first.status = DraftStatus::APPROVED;
    auto firstRun = pipeline.dryRun(first, skA, engine);
    assert(firstRun.ok);
    std::string importReason;
    assert(engine.importEntry(firstRun.entry, &importReason));

    AgentDraftRecord second;
    second.proposal = makeDraft(skA, 1700000201, "duplicate_title", std::string(80, 'd'), 22);
    second.status = DraftStatus::QUEUED;
    auto duplicate = pipeline.dryRun(second, skA, engine);
    assert(!duplicate.ok);
    assert(duplicate.reason == "duplicate_gate_contentid");

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
}

static void testDeterministicBatching() {
    AgentSubmissionPipelineConfig cfg;
    cfg.maxBatchSize = 32;
    cfg.intentMinTitleBytes = 1;
    cfg.intentMinBodyBytes = 1;
    AgentSubmissionPipeline pipeline(cfg);

    std::filesystem::path tmpDir;
    PoeV1Engine engine;
    setupEngine(&engine, &tmpDir);

    AgentDraftQueue queue;
    auto skMain = makeSk(11);
    auto skOther = makeSk(19);

    auto d1 = makeDraft(skMain, 1700000300, "alpha_title", std::string(80, 'a'), 31);
    auto d2 = makeDraft(skOther, 1700000301, "other_title", std::string(80, 'o'), 32);
    auto d3 = makeDraft(skMain, 1700000302, "beta_title_1", std::string(80, 'b'), 33);

    std::string reason;
    assert(queue.enqueue(d1, &reason));
    assert(queue.enqueue(d2, &reason));
    assert(queue.enqueue(d3, &reason));

    const auto first = pipeline.runDeterministicBatch(queue, engine, skMain, 1700000400);
    assert(first.size() == 3);

    std::vector<std::string> expectedOrder = {
        synapse::crypto::toHex(d1.draftId()),
        synapse::crypto::toHex(d2.draftId()),
        synapse::crypto::toHex(d3.draftId())
    };
    std::sort(expectedOrder.begin(), expectedOrder.end());
    std::vector<std::string> actualOrder;
    for (const auto& r : first) actualOrder.push_back(synapse::crypto::toHex(r.draftId));
    assert(actualOrder == expectedOrder);

    auto d2Record = queue.get(d2.draftId());
    auto d1Record = queue.get(d1.draftId());
    auto d3Record = queue.get(d3.draftId());
    assert(d2Record.has_value());
    assert(d1Record.has_value());
    assert(d3Record.has_value());
    assert(d2Record->status == DraftStatus::REJECTED);
    assert(d1Record->status == DraftStatus::REVIEW_REQUIRED);
    assert(d3Record->status == DraftStatus::REVIEW_REQUIRED);

    std::string reviewReason;
    const auto reviewer = synapse::crypto::derivePublicKey(skMain);
    assert(queue.upsertReview(d1.draftId(), reviewer, true, 1700000450, "ok", &reviewReason));
    assert(queue.upsertReview(d3.draftId(), reviewer, true, 1700000451, "ok", &reviewReason));

    const auto second = pipeline.runDeterministicBatch(queue, engine, skMain, 1700000500);
    assert(second.size() == 2);
    d1Record = queue.get(d1.draftId());
    d3Record = queue.get(d3.draftId());
    assert(d1Record.has_value());
    assert(d3Record.has_value());
    assert(d1Record->status == DraftStatus::APPROVED);
    assert(d3Record->status == DraftStatus::APPROVED);

    const auto third = pipeline.runDeterministicBatch(queue, engine, skMain, 1700000600);
    assert(third.size() == 2);
    d1Record = queue.get(d1.draftId());
    d3Record = queue.get(d3.draftId());
    assert(d1Record.has_value());
    assert(d3Record.has_value());
    assert(d1Record->status == DraftStatus::SUBMITTED);
    assert(d3Record->status == DraftStatus::SUBMITTED);
    assert(engine.totalEntries() == 2);

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
}

static void testBatchLimitRespected() {
    AgentSubmissionPipelineConfig cfg;
    cfg.maxBatchSize = 1;
    cfg.intentMinTitleBytes = 1;
    cfg.intentMinBodyBytes = 1;
    AgentSubmissionPipeline pipeline(cfg);

    std::filesystem::path tmpDir;
    PoeV1Engine engine;
    setupEngine(&engine, &tmpDir);

    AgentDraftQueue queue;
    auto sk = makeSk(23);
    auto d1 = makeDraft(sk, 1700000600, "limit_title_1", std::string(80, '1'), 41);
    auto d2 = makeDraft(sk, 1700000601, "limit_title_2", std::string(80, '2'), 42);
    auto d3 = makeDraft(sk, 1700000602, "limit_title_3", std::string(80, '3'), 43);

    std::string reason;
    assert(queue.enqueue(d1, &reason));
    assert(queue.enqueue(d2, &reason));
    assert(queue.enqueue(d3, &reason));

    std::vector<std::string> ids = {
        synapse::crypto::toHex(d1.draftId()),
        synapse::crypto::toHex(d2.draftId()),
        synapse::crypto::toHex(d3.draftId())
    };
    std::sort(ids.begin(), ids.end());

    const auto first = pipeline.runDeterministicBatch(queue, engine, sk, 1700000700);
    assert(first.size() == 1);
    assert(first[0].action == "review_required");
    assert(synapse::crypto::toHex(first[0].draftId) == ids[0]);

    std::string reviewReason;
    const auto reviewer = synapse::crypto::derivePublicKey(sk);
    assert(queue.upsertReview(first[0].draftId, reviewer, true, 1700000701, "ok", &reviewReason));

    const auto second = pipeline.runDeterministicBatch(queue, engine, sk, 1700000702);
    assert(second.size() == 1);
    assert(second[0].action == "approved");
    assert(synapse::crypto::toHex(second[0].draftId) == ids[0]);

    auto firstRecord = queue.get(first[0].draftId);
    assert(firstRecord.has_value());
    assert(firstRecord->status == DraftStatus::APPROVED);

    const auto third = pipeline.runDeterministicBatch(queue, engine, sk, 1700000703);
    assert(third.size() == 1);
    assert(third[0].action == "submitted");
    assert(third[0].expectedAcceptanceRewardAtoms > 0);
    assert(!third[0].acceptanceRewardCredited);

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
}

static void testReviewerDiversityConstraint() {
    AgentSubmissionPipelineConfig cfg;
    cfg.maxBatchSize = 8;
    cfg.intentMinTitleBytes = 1;
    cfg.intentMinBodyBytes = 1;
    cfg.minDistinctReviewers = 2;
    AgentSubmissionPipeline pipeline(cfg);

    std::filesystem::path tmpDir;
    PoeV1Engine engine;
    setupEngine(&engine, &tmpDir);

    AgentDraftQueue queue;
    auto skAuthor = makeSk(51);
    auto skReviewer = makeSk(61);
    auto d = makeDraft(skAuthor, 1700000800, "diversity_title", std::string(80, 'v'), 55);

    std::string reason;
    assert(queue.enqueue(d, &reason));

    const auto first = pipeline.runDeterministicBatch(queue, engine, skAuthor, 1700000801);
    assert(first.size() == 1);
    assert(first[0].action == "review_required");
    auto draftRecord = queue.get(d.draftId());
    assert(draftRecord.has_value());
    assert(draftRecord->status == DraftStatus::REVIEW_REQUIRED);

    const auto authorReviewer = synapse::crypto::derivePublicKey(skAuthor);
    assert(queue.upsertReview(d.draftId(), authorReviewer, true, 1700000802, "ok", &reason));

    const auto second = pipeline.runDeterministicBatch(queue, engine, skAuthor, 1700000803);
    assert(second.size() == 1);
    assert(second[0].action == "review_pending");
    assert(second[0].reason == "review_diversity_insufficient");
    draftRecord = queue.get(d.draftId());
    assert(draftRecord.has_value());
    assert(draftRecord->status == DraftStatus::REVIEW_REQUIRED);

    const auto secondReviewer = synapse::crypto::derivePublicKey(skReviewer);
    assert(queue.upsertReview(d.draftId(), secondReviewer, true, 1700000804, "ok", &reason));

    const auto third = pipeline.runDeterministicBatch(queue, engine, skAuthor, 1700000805);
    assert(third.size() == 1);
    assert(third[0].action == "approved");
    draftRecord = queue.get(d.draftId());
    assert(draftRecord.has_value());
    assert(draftRecord->status == DraftStatus::APPROVED);

    const auto fourth = pipeline.runDeterministicBatch(queue, engine, skAuthor, 1700000806);
    assert(fourth.size() == 1);
    assert(fourth[0].action == "submitted");
    assert(fourth[0].expectedAcceptanceRewardAtoms > 0);
    assert(!fourth[0].acceptanceRewardCredited);
    draftRecord = queue.get(d.draftId());
    assert(draftRecord.has_value());
    assert(draftRecord->status == DraftStatus::SUBMITTED);

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
}

static void testDuplicateNoveltyGate() {
    AgentSubmissionPipelineConfig cfg;
    cfg.intentMinTitleBytes = 1;
    cfg.intentMinBodyBytes = 1;
    AgentSubmissionPipeline pipeline(cfg);

    std::filesystem::path tmpDir;
    PoeV1Engine engine;
    setupEngine(&engine, &tmpDir);

    auto sk = makeSk(71);
    auto first = makeDraft(sk, 1700000900, "novelty title", std::string(80, 'n'), 91);
    (void)importDraftEntry(pipeline, engine, first, sk);

    AgentDraftRecord second;
    second.proposal = makeDraft(sk, 1700000901, "novelty title", std::string(80, 'n'), 92);
    second.status = DraftStatus::QUEUED;

    auto dry = pipeline.dryRun(second, sk, engine);
    assert(!dry.ok);
    assert(dry.reason == "duplicate_gate_novelty");

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
}

static void testDuplicateCitationGraphGate() {
    AgentSubmissionPipelineConfig cfg;
    cfg.intentMinTitleBytes = 1;
    cfg.intentMinBodyBytes = 1;
    cfg.duplicateGateCitationGraph = true;
    AgentSubmissionPipeline pipeline(cfg);

    std::filesystem::path tmpDir;
    PoeV1Engine engine;
    setupEngine(&engine, &tmpDir);

    PoeV1Config engineCfg = engine.getConfig();
    engineCfg.noveltyBands = 0;
    engine.setConfig(engineCfg);

    auto sk = makeSk(81);

    auto sourceA = makeDraft(sk, 1700001000, "source_a_title", std::string(80, 'a'), 101);
    auto sourceB = makeDraft(sk, 1700001001, "source_b_title", std::string(80, 'b'), 102);
    auto sourceAEntry = importDraftEntry(pipeline, engine, sourceA, sk);
    auto sourceBEntry = importDraftEntry(pipeline, engine, sourceB, sk);

    auto anchor = makeDraft(sk, 1700001002, "anchor_title", "Anchor body for citation graph determinism test. This needs enough bytes to pass precheck.", 103);
    anchor.citations = {sourceAEntry.contentId(), sourceBEntry.contentId()};
    anchor.signature = synapse::crypto::sign(anchor.signatureHash(), sk);
    (void)importDraftEntry(pipeline, engine, anchor, sk);

    AgentDraftRecord candidate;
    candidate.proposal = makeDraft(sk, 1700001003, "anchor title variant", "Anchor body variant for citation graph determinism test. This needs enough bytes to pass precheck.", 104);
    candidate.proposal.citations = {sourceBEntry.contentId(), sourceAEntry.contentId()};
    candidate.proposal.signature = synapse::crypto::sign(candidate.proposal.signatureHash(), sk);
    candidate.status = DraftStatus::QUEUED;

    auto dry = pipeline.dryRun(candidate, sk, engine);
    assert(!dry.ok);
    assert(dry.reason == "duplicate_gate_citation_graph");

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
}

static void testCitationSanityGate() {
    AgentSubmissionPipelineConfig cfg;
    cfg.intentMinTitleBytes = 1;
    cfg.intentMinBodyBytes = 1;
    cfg.duplicateGateContentId = false;
    cfg.duplicateGateNoveltyBuckets = false;
    cfg.duplicateGateCitationGraph = false;
    cfg.citationSanityRequireKnownCitations = true;
    cfg.citationSanityRejectIntraSetCycles = true;
    cfg.citationSanityMaxIntraSetEdges = 0;
    cfg.citationSanityMinCorroboratingCitations = 2;
    AgentSubmissionPipeline pipeline(cfg);

    std::filesystem::path tmpDir;
    PoeV1Engine engine;
    setupEngine(&engine, &tmpDir);

    PoeV1Config engineCfg = engine.getConfig();
    engineCfg.noveltyBands = 0;
    engine.setConfig(engineCfg);

    auto sk = makeSk(91);

    auto sourceB = makeDraft(sk, 1700001100, "source_b_title", std::string(80, 'b'), 111);
    auto sourceBEntry = importDraftEntry(pipeline, engine, sourceB, sk);

    auto sourceA = makeDraft(sk, 1700001101, "source_a_title", std::string(80, 'a'), 112);
    sourceA.citations = {sourceBEntry.contentId()};
    sourceA.signature = synapse::crypto::sign(sourceA.signatureHash(), sk);
    auto sourceAEntry = importDraftEntry(pipeline, engine, sourceA, sk);

    AgentDraftRecord capCase;
    capCase.proposal = makeDraft(sk, 1700001102, "cap_case_title", std::string(80, 'c'), 113);
    capCase.proposal.citations = {sourceAEntry.contentId(), sourceBEntry.contentId()};
    capCase.proposal.signature = synapse::crypto::sign(capCase.proposal.signatureHash(), sk);
    capCase.status = DraftStatus::QUEUED;

    AgentSubmissionPipelineConfig capCfg = cfg;
    capCfg.citationSanityMaxCitations = 1;
    AgentSubmissionPipeline capPipeline(capCfg);
    auto capDry = capPipeline.dryRun(capCase, sk, engine);
    assert(!capDry.ok);
    assert(capDry.reason == "citation_cap_exceeded");

    AgentDraftRecord corroborationCase;
    corroborationCase.proposal = makeDraft(sk, 1700001103, "corroboration_title", std::string(80, 'k'), 114);
    corroborationCase.proposal.citations = {sourceAEntry.contentId()};
    corroborationCase.proposal.signature = synapse::crypto::sign(corroborationCase.proposal.signatureHash(), sk);
    corroborationCase.status = DraftStatus::QUEUED;
    auto corroborationDry = pipeline.dryRun(corroborationCase, sk, engine);
    assert(!corroborationDry.ok);
    assert(corroborationDry.reason == "citation_min_corroboration");

    AgentSubmissionPipelineConfig ringCfg = cfg;
    ringCfg.citationSanityMinCorroboratingCitations = 1;
    AgentSubmissionPipeline ringPipeline(ringCfg);
    AgentDraftRecord ringCase;
    ringCase.proposal = makeDraft(sk, 1700001104, "ring_case_title", std::string(80, 'r'), 115);
    ringCase.proposal.citations = {sourceAEntry.contentId(), sourceBEntry.contentId()};
    ringCase.proposal.signature = synapse::crypto::sign(ringCase.proposal.signatureHash(), sk);
    ringCase.status = DraftStatus::QUEUED;
    auto ringDry = ringPipeline.dryRun(ringCase, sk, engine);
    assert(!ringDry.ok);
    assert(ringDry.reason == "citation_ring_detected");

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
}

int main() {
    std::cout << "Running agent_submission_pipeline tests...\n";
    testDeterministicConversion();
    testDryRunRejectReasons();
    testDeterministicBatching();
    testBatchLimitRespected();
    testReviewerDiversityConstraint();
    testDuplicateNoveltyGate();
    testDuplicateCitationGraphGate();
    testCitationSanityGate();
    std::cout << "All agent_submission_pipeline tests passed!\n";
    return 0;
}
