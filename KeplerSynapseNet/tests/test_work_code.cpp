#include "core/agent_runtime.h"
#include "core/poe_v1_engine.h"
#include "core/poe_v1_objects.h"
#include "core/work_code.h"
#include "crypto/crypto.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using synapse::core::AgentCapability;
using synapse::core::AgentRuntimeSandbox;
using synapse::core::PoeV1Config;
using synapse::core::PoeV1Engine;
using synapse::core::SandboxPolicy;
using synapse::core::bindWorkVote;
using synapse::core::executeWork;
using synapse::core::poe_v1::ContentType;
using synapse::core::poe_v1::ValidationVoteV1;
using synapse::core::poe_v1::signValidationVoteV1;
using synapse::core::workBundle;
using synapse::core::workVoteNote;

static int gFails = 0;

static void expect(bool cond, const char* msg) {
    if (cond) return;
    std::cerr << "FAIL " << msg << "\n";
    ++gFails;
}

static synapse::crypto::PrivateKey makeSk(uint8_t tag) {
    synapse::crypto::PrivateKey sk{};
    for (size_t i = 0; i < sk.size(); ++i) sk[i] = static_cast<uint8_t>(tag + i);
    return sk;
}

static std::filesystem::path tmpDir(const std::string& name) {
    auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto dir = std::filesystem::temp_directory_path() / (name + "_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static std::string passing(int n) {
    return workBundle(
        "fn add(a, b) = a + b\n",
        "assert add(" + std::to_string(n) + ", 1) == " + std::to_string(n + 1) + "\n");
}

static PoeV1Config soloCfg() {
    PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = 8;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 1;
    cfg.validatorsM = 1;
    cfg.allowSelfBootstrapValidator = true;
    cfg.minSubmitIntervalSeconds = 0;
    cfg.noveltyBands = 0;
    return cfg;
}

static void openEngine(PoeV1Engine& engine, const std::string& db, const PoeV1Config& cfg,
                       const std::vector<synapse::crypto::PublicKey>& validators) {
    expect(engine.open(db), "open db");
    engine.setConfig(cfg);
    engine.setStaticValidators(validators);
    for (const auto& pk : validators) engine.setValidatorIdentity(pk, true);
}

static void testRunner() {
    const std::string good = workBundle(
        "fn add(a, b) = a + b\n",
        "assert add(2, 3) == 5\n");
    auto a = executeWork(good);
    auto b = executeWork(good);
    expect(a.passed, "add passes");
    expect(a.reason == "ok", "pass reason");
    expect(a.resultHash == b.resultHash, "pass hash stable");
    expect(a.transcript == b.transcript, "pass transcript stable");

    const std::string bad = workBundle(
        "fn add(a, b) = a + b\n",
        "assert add(2, 3) == 4\n");
    auto f1 = executeWork(bad);
    auto f2 = executeWork(bad);
    expect(!f1.passed, "failing assert");
    expect(f1.reason == "work_tests_failed", "fail reason");
    expect(f1.resultHash == f2.resultHash, "fail hash stable");
    expect(f1.resultHash != a.resultHash, "pass and fail differ");

    auto missing = executeWork("fn add(a, b) = a + b\n");
    expect(!missing.passed && missing.reason == "work_bundle_required", "missing header");

    auto noTests = executeWork(workBundle("fn add(a, b) = a + b\n", ""));
    expect(!noTests.passed && noTests.reason == "work_tests_required", "empty tests");

    auto junk = executeWork(workBundle("not a function\n", "assert 1 == 1\n"));
    expect(!junk.passed && junk.reason == "compile_failed", "compile failed");

    auto div = executeWork(workBundle("fn z(a) = a / 0\n", "assert z(1) == 0\n"));
    expect(!div.passed && div.reason == "divide_by_zero", "divide by zero");

    auto branch = executeWork(workBundle(
        "fn pick(a) = if a then 2 else 3\n"
        "fn orz(a, b) = a || b\n"
        "fn andz(a, b) = a && b\n",
        "assert pick(1) == 2\nassert pick(0) == 3\n"
        "assert orz(1, 0) == 1\nassert orz(0, 4) == 4\n"
        "assert andz(1, 7) == 7\nassert andz(0, 7) == 0\n"));
    expect(branch.passed, "branch and short-circuit");

    auto emitted = executeWork(workBundle(
        "fn id(a) = a\n",
        "emit id(7)\nassert id(7) == 7\n"));
    expect(emitted.passed, "emit passes");
    expect(emitted.transcript.find("output=7\n") != std::string::npos, "emit output");

    AgentRuntimeSandbox empty;
    auto denied = executeWork(good, empty);
    expect(!denied.passed && denied.reason == "sandbox_denied", "empty allowlist");

    SandboxPolicy side;
    side.allowlist = {AgentCapability::PROPOSE_KNOWLEDGE};
    side.allowSideEffects = true;
    AgentRuntimeSandbox writable(side);
    auto sideDenied = executeWork(good, writable);
    expect(!sideDenied.passed && sideDenied.reason == "sandbox_denied", "side effects denied");
}

static bool castVote(PoeV1Engine& engine, const synapse::crypto::Hash256& sid,
                     const synapse::crypto::PrivateKey& sk, const synapse::crypto::Hash256& hash,
                     bool honest) {
    ValidationVoteV1 vote;
    vote.version = 1;
    vote.submitId = sid;
    vote.prevBlockHash = engine.chainSeed();
    vote.flags = 0;
    if (honest) {
        bindWorkVote(vote, hash);
    } else {
        vote.note = std::string("powc:v1:") + std::string(64, '0');
        vote.scores = {0, 0, 0};
    }
    if (!signValidationVoteV1(vote, sk)) return false;
    return engine.addVote(vote);
}

static void testMint() {
    auto dir = tmpDir("poe_work_mint");
    auto author = makeSk(21);
    auto pk = synapse::crypto::derivePublicKey(author);
    PoeV1Engine engine;
    auto cfg = soloCfg();
    openEngine(engine, (dir / "poe.db").string(), cfg, {pk});

    auto raw = engine.submit(ContentType::CODE, "raw_code_title", std::string(80, 'c'), {}, author, true);
    expect(!raw.ok && raw.error == "work_bundle_required", "raw code rejected");

    auto failed = engine.submit(
        ContentType::CODE,
        "failed_code_title",
        workBundle("fn add(a, b) = a + b\n", "assert add(2, 3) == 9\n"),
        {},
        author,
        true);
    expect(!failed.ok && failed.error == "work_tests_failed", "failing bundle rejected");
    expect(!failed.finalized, "failing bundle not finalized");

    auto body = passing(2);
    expect(body.size() >= 50 && body.size() < 8192, "bundle size");
    auto ok = engine.submit(ContentType::CODE, "working_code_title", body, {}, author, true);
    expect(ok.ok, "passing bundle accepted");
    expect(ok.finalized, "solo cell finalizes its own passing run");
    expect(engine.shouldMintAcceptanceReward(ok.submitId), "passing code mints");
    expect(ok.acceptanceReward == cfg.acceptanceBaseReward + cfg.acceptanceWorkingCodeBonus,
           "working code bonus");

    auto text = engine.submit(ContentType::TEXT, "plain_text_title", std::string(80, 't'), {}, author, true);
    expect(text.ok && text.finalized, "text still finalizes");
    expect(engine.shouldMintAcceptanceReward(text.submitId), "text still mints");
    auto textEntry = engine.getEntry(text.submitId);
    expect(textEntry.has_value(), "text entry");
    if (textEntry) {
        expect(engine.calculateAcceptanceReward(*textEntry) == cfg.acceptanceBaseReward, "text has no code bonus");
    }

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

static void testMajority() {
    auto dir = tmpDir("poe_work_majority");
    auto author = makeSk(31);
    auto s1 = makeSk(41);
    auto s2 = makeSk(42);
    auto s3 = makeSk(43);
    auto p1 = synapse::crypto::derivePublicKey(s1);
    auto p2 = synapse::crypto::derivePublicKey(s2);
    auto p3 = synapse::crypto::derivePublicKey(s3);

    PoeV1Config cfg = soloCfg();
    cfg.validatorsN = 3;
    cfg.validatorsM = 2;
    PoeV1Engine engine;
    openEngine(engine, (dir / "poe.db").string(), cfg, {p1, p2, p3});

    auto honestBody = passing(4);
    auto work = executeWork(honestBody);
    expect(work.passed, "majority bundle passes locally");

    auto pending = engine.submit(ContentType::CODE, "majority_code_a", honestBody, {}, author, false);
    expect(pending.ok && !pending.finalized, "pending until votes");

    expect(castVote(engine, pending.submitId, s1, work.resultHash, true), "first honest vote");
    expect(!engine.finalize(pending.submitId).has_value(), "one vote is short of M=2");
    expect(castVote(engine, pending.submitId, s2, work.resultHash, true), "second honest vote");
    expect(engine.finalize(pending.submitId).has_value(), "two matching hashes finalize");
    expect(engine.shouldMintAcceptanceReward(pending.submitId), "majority mints");

    ValidationVoteV1 swapped;
    swapped.version = 1;
    swapped.submitId = pending.submitId;
    swapped.prevBlockHash = engine.chainSeed();
    swapped.note = workVoteNote(work.resultHash);
    swapped.scores = {1, 2, 3};
    expect(signValidationVoteV1(swapped, s3), "sign swapped scores");
    expect(!engine.addVote(swapped), "note without the signed prefix is rejected");

    auto fakeBody = passing(5);
    auto fakeWork = executeWork(fakeBody);
    auto fake = engine.submit(ContentType::CODE, "majority_code_b", fakeBody, {}, author, false);
    expect(fake.ok, "second bundle accepted");
    expect(!castVote(engine, fake.submitId, s1, fakeWork.resultHash, false), "fake note rejected");
    expect(!castVote(engine, fake.submitId, s2, fakeWork.resultHash, false), "second fake rejected");
    expect(!castVote(engine, fake.submitId, s3, fakeWork.resultHash, false), "third fake rejected");
    expect(!engine.finalize(fake.submitId).has_value(), "colluding notes do not mint");

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

int main() {
    testRunner();
    testMint();
    testMajority();
    if (gFails != 0) {
        std::cerr << gFails << " work-code checks failed\n";
        return 1;
    }
    std::cout << "work code tests passed\n";
    return 0;
}
