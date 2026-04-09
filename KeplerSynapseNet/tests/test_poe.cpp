#include "core/poe_v1.h"
#include "core/poe_v1_objects.h"
#include "core/poe_v1_engine.h"
#include "crypto/crypto.h"
#include "database/database.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>

using synapse::core::poe_v1::canonicalizeText;
using synapse::core::poe_v1::canonicalizeCode;
using synapse::core::poe_v1::canonicalizeCodeForSimhash;
using synapse::core::poe_v1::hammingDistance64;
using synapse::core::poe_v1::hasLeadingZeroBits;
using synapse::core::poe_v1::minhash16;
using synapse::core::poe_v1::minhashEqualCount;
using synapse::core::poe_v1::selectValidators;
using synapse::core::poe_v1::simhash64;

static synapse::crypto::PublicKey makePk(uint8_t tag) {
    synapse::crypto::PublicKey pk{};
    pk[0] = tag;
    for (size_t i = 1; i < pk.size(); ++i) pk[i] = static_cast<uint8_t>(tag + i);
    return pk;
}

static synapse::crypto::PrivateKey makeSk(uint8_t tag) {
    synapse::crypto::PrivateKey sk{};
    for (size_t i = 0; i < sk.size(); ++i) sk[i] = static_cast<uint8_t>(tag + i);
    return sk;
}

static synapse::crypto::Hash256 hashFromHex(const std::string& hex) {
    synapse::crypto::Hash256 out{};
    auto bytes = synapse::crypto::fromHex(hex);
    assert(bytes.size() == out.size());
    std::memcpy(out.data(), bytes.data(), out.size());
    return out;
}

static void testCanonicalize() {
    std::string in = "  Hello\tWORLD\r\n\nTest  ";
    std::string out = canonicalizeText(in);
    assert(out == "hello world test");
}

static void testCodeCanonicalize() {
    std::string in = "Foo\r\nBar\rBaz";
    std::string out = canonicalizeCode(in);
    assert(out == "Foo\nBarBaz");

    std::string sim = canonicalizeCodeForSimhash("  Foo\tBAR\r\nBaz  ");
    assert(sim == "Foo BAR Baz");
}

static void testCodeVsTextHashing() {
    synapse::crypto::PrivateKey sk = makeSk(1);
    synapse::crypto::PublicKey pk = synapse::crypto::derivePublicKey(sk);

    synapse::core::poe_v1::KnowledgeEntryV1 a;
    a.version = 1;
    a.timestamp = 123;
    a.authorPubKey = pk;
    a.contentType = synapse::core::poe_v1::ContentType::CODE;
    a.title = "Patch";
    a.body = "Foo";
    a.powBits = 12;

    synapse::core::poe_v1::KnowledgeEntryV1 b = a;
    b.body = "foo";

    assert(a.contentId() != b.contentId());

    synapse::core::poe_v1::KnowledgeEntryV1 q1 = a;
    q1.contentType = synapse::core::poe_v1::ContentType::QA;
    synapse::core::poe_v1::KnowledgeEntryV1 q2 = q1;
    q2.body = "foo";
    assert(q1.contentId() == q2.contentId());
}

static void testSimhashDeterminism() {
    std::string a = canonicalizeText("HELLO   world");
    std::string b = canonicalizeText(" hello world ");
    uint64_t ha = simhash64(a);
    uint64_t hb = simhash64(b);
    assert(ha == hb);
    assert(hammingDistance64(ha, hb) == 0);

    std::string c = canonicalizeText("completely different text");
    uint64_t hc = simhash64(c);
    assert(hammingDistance64(ha, hc) > 0);
}

static void testMinhashDeterminism() {
    std::string a = canonicalizeText("HELLO   world");
    std::string b = canonicalizeText(" hello world ");
    auto ha = minhash16(a);
    auto hb = minhash16(b);
    assert(ha == hb);
    assert(minhashEqualCount(ha, hb) == 16);

    std::string c = canonicalizeText("completely different text");
    auto hc = minhash16(c);
    assert(minhashEqualCount(ha, hc) < 16);
}

static void testLeadingZeros() {
    synapse::crypto::Hash256 h{};
    assert(hasLeadingZeroBits(h, 0));
    assert(hasLeadingZeroBits(h, 256));
    h[0] = 0x00;
    h[1] = 0x0F;
    assert(hasLeadingZeroBits(h, 12));
    h[1] = 0x8F;
    assert(!hasLeadingZeroBits(h, 12));
}

static void testValidatorSelectionDeterminism() {
    synapse::crypto::Hash256 prev = synapse::crypto::sha256(std::string("prev"));
    synapse::crypto::Hash256 sid = synapse::crypto::sha256(std::string("sid"));

    std::vector<synapse::crypto::PublicKey> validators = {
        makePk(3), makePk(1), makePk(9), makePk(2), makePk(7)
    };

    auto a = selectValidators(prev, sid, validators, 3);
    auto b = selectValidators(prev, sid, validators, 3);
    assert(a == b);
    assert(a.size() == 3);
    assert(a[0] != a[1] && a[0] != a[2] && a[1] != a[2]);

    std::reverse(validators.begin(), validators.end());
    auto c = selectValidators(prev, sid, validators, 3);
    assert(a == c);
}

static void testValidatorSelectionAlwaysFillsRequestedCount() {
    synapse::crypto::Hash256 prev = hashFromHex("f5423270dd9923fb25d7012f7edb9412cd93da2d540f21013bef39b6dac91664");
    synapse::crypto::Hash256 sid = hashFromHex("801be2696a7bcd61ee3807664e876f12ff0867f6b420b75ba51c7617b7ca098d");

    std::vector<synapse::crypto::PublicKey> validators = {
        makePk(1), makePk(2), makePk(3)
    };

    auto selected = selectValidators(prev, sid, validators, 2);
    assert(selected.size() == 2);
    assert(selected[0] != selected[1]);
}

static void testKnowledgeEntryV1Roundtrip() {
    synapse::crypto::PrivateKey sk = makeSk(42);
    synapse::crypto::PublicKey pk = synapse::crypto::derivePublicKey(sk);

    synapse::core::poe_v1::LimitsV1 limits;
    limits.minPowBits = 8;
    limits.maxPowBits = 28;

    synapse::core::poe_v1::KnowledgeEntryV1 e;
    e.version = 1;
    e.timestamp = 12345;
    e.authorPubKey = pk;
    e.contentType = synapse::core::poe_v1::ContentType::QA;
    e.title = "Hello";
    e.body = "World";
    e.powBits = 8;

    for (uint64_t nonce = 0;; ++nonce) {
        e.powNonce = nonce;
        if (hasLeadingZeroBits(e.submitId(), e.powBits)) break;
    }

    synapse::core::poe_v1::signKnowledgeEntryV1(e, sk);
    std::string reason;
    assert(e.verifyAll(limits, &reason));

    auto ser = e.serialize();
    auto d = synapse::core::poe_v1::KnowledgeEntryV1::deserialize(ser);
    assert(d.has_value());
    assert(d->version == e.version);
    assert(d->timestamp == e.timestamp);
    assert(d->authorPubKey == e.authorPubKey);
    assert(d->contentType == e.contentType);
    assert(d->title == e.title);
    assert(d->body == e.body);
    assert(d->powNonce == e.powNonce);
    assert(d->powBits == e.powBits);
    assert(d->authorSig == e.authorSig);
    assert(d->submitId() == e.submitId());
}

static void testValidationVoteV1Roundtrip() {
    synapse::crypto::PrivateKey sk = makeSk(7);
    synapse::core::poe_v1::ValidationVoteV1 v;
    v.version = 1;
    v.submitId = synapse::crypto::sha256(std::string("s"));
    v.prevBlockHash = synapse::crypto::sha256(std::string("p"));
    v.flags = 0;
    v.scores = {10, 20, 30};
    synapse::core::poe_v1::signValidationVoteV1(v, sk);
    assert(v.verifySignature());

    auto ser = v.serialize();
    auto d = synapse::core::poe_v1::ValidationVoteV1::deserialize(ser);
    assert(d.has_value());
    assert(d->version == v.version);
    assert(d->submitId == v.submitId);
    assert(d->prevBlockHash == v.prevBlockHash);
    assert(d->validatorPubKey == v.validatorPubKey);
    assert(d->flags == v.flags);
    assert(d->scores == v.scores);
    assert(d->signature == v.signature);
    assert(d->verifySignature());
}

static void testAcceptanceRewardDeterminism() {
    synapse::core::PoeV1Engine engine;
    synapse::core::PoeV1Config cfg;
    cfg.limits.minPowBits = 12;
    cfg.powBits = 12;
    cfg.acceptanceBaseReward = 10000000ULL;
    cfg.acceptanceMinReward = 1000000ULL;
    cfg.acceptanceMaxReward = 100000000ULL;
    cfg.acceptanceBonusPerPowBit = 1000000U;
    cfg.acceptanceSizePenaltyBytes = 2048;
    cfg.acceptancePenaltyPerChunk = 1000000U;
    engine.setConfig(cfg);

    synapse::core::poe_v1::KnowledgeEntryV1 e;
    e.version = 1;
    e.timestamp = 1;
    e.authorPubKey = makePk(1);
    e.contentType = synapse::core::poe_v1::ContentType::TEXT;
    e.title = "t";
    e.body = std::string(100, 'a');
    e.powBits = 12;
    uint64_t r1 = engine.calculateAcceptanceReward(e);
    uint64_t r2 = engine.calculateAcceptanceReward(e);
    assert(r1 == r2);
    assert(r1 >= cfg.acceptanceMinReward && r1 <= cfg.acceptanceMaxReward);
}

static void testEpochDeterminism() {
    auto uniq = std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto tmpDir = std::filesystem::temp_directory_path() / ("synapsenet_poe_epoch_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);
    std::string dbPath = (tmpDir / "poe.db").string();

    synapse::core::PoeV1Engine engine;
    assert(engine.open(dbPath));

    synapse::core::PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = cfg.powBits;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 2;
    cfg.validatorsM = 1;
    engine.setConfig(cfg);

    synapse::crypto::PrivateKey sk1 = makeSk(1);
    synapse::crypto::PrivateKey sk2 = makeSk(2);
    synapse::crypto::PublicKey pk1 = synapse::crypto::derivePublicKey(sk1);
    synapse::crypto::PublicKey pk2 = synapse::crypto::derivePublicKey(sk2);
    engine.setStaticValidators({pk1, pk2});

    auto r1 = engine.submit(synapse::core::poe_v1::ContentType::TEXT, "entry_a_long", std::string(80, 'a'), {}, sk1, true);
    assert(r1.ok && r1.finalized);

    auto r2 = engine.submit(synapse::core::poe_v1::ContentType::TEXT, "entry_b_long", std::string(80, 'b'), {r1.contentId}, sk2, true);
    assert(r2.ok && r2.finalized);

    uint64_t budget = 1000000ULL;
    auto e1 = engine.runEpoch(budget, 20);
    assert(e1.ok);
    auto e2 = engine.runEpoch(budget, 20);
    assert(e2.ok);

    assert(e1.allocations.size() == e2.allocations.size());
    assert(e1.allocationHash == e2.allocationHash);

    uint64_t sum1 = 0;
    uint64_t sum2 = 0;
    for (size_t i = 0; i < e1.allocations.size(); ++i) {
        assert(e1.allocations[i].contentId == e2.allocations[i].contentId);
        assert(e1.allocations[i].amount == e2.allocations[i].amount);
        sum1 += e1.allocations[i].amount;
        sum2 += e2.allocations[i].amount;
    }
    assert(sum1 == budget);
    assert(sum2 == budget);

    engine.close();
    std::filesystem::remove_all(tmpDir, ec);
}

static void testDuplicateContentRejected() {
    auto uniq = std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto tmpDir = std::filesystem::temp_directory_path() / ("synapsenet_poe_dup_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);
    std::string dbPath = (tmpDir / "poe.db").string();

    synapse::core::PoeV1Engine engine;
    assert(engine.open(dbPath));

    synapse::core::PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = cfg.powBits;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 1;
    cfg.validatorsM = 1;
    engine.setConfig(cfg);

    synapse::crypto::PrivateKey sk = makeSk(11);
    synapse::crypto::PublicKey pk = synapse::crypto::derivePublicKey(sk);
    engine.setStaticValidators({pk});

    auto r1 = engine.submit(synapse::core::poe_v1::ContentType::TEXT, "duplicate_title", std::string(80, 'd'), {}, sk, false);
    assert(r1.ok);

    auto r2 = engine.submit(synapse::core::poe_v1::ContentType::TEXT, "duplicate_title", std::string(80, 'd'), {}, sk, false);
    assert(!r2.ok);
    assert(r2.error == "duplicate_content");

    engine.close();
    std::filesystem::remove_all(tmpDir, ec);
}

static void testVoteSelectionAndDeterministicFinalizationRecord() {
    auto uniq = std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto tmpDir = std::filesystem::temp_directory_path() / ("synapsenet_poe_vote_sel_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);
    std::string dbPath = (tmpDir / "poe.db").string();

    synapse::core::PoeV1Engine engine;
    assert(engine.open(dbPath));

    synapse::core::PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = cfg.powBits;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 1;
    cfg.validatorsM = 1;
    engine.setConfig(cfg);

    synapse::crypto::PrivateKey skAuthor = makeSk(31);
    synapse::crypto::PrivateKey skSelected = makeSk(32);
    synapse::crypto::PrivateKey skOther = makeSk(33);
    auto pkSelected = synapse::crypto::derivePublicKey(skSelected);
    engine.setStaticValidators({pkSelected});

    auto submitRes = engine.submit(
        synapse::core::poe_v1::ContentType::TEXT,
        "vote_test_title",
        std::string(80, 'v'),
        {},
        skAuthor,
        false
    );
    assert(submitRes.ok);
    assert(!submitRes.finalized);

    auto entry = engine.getEntry(submitRes.submitId);
    assert(entry.has_value());

    synapse::core::poe_v1::ValidationVoteV1 badVote;
    badVote.version = 1;
    badVote.submitId = submitRes.submitId;
    badVote.prevBlockHash = engine.chainSeed();
    badVote.flags = 0;
    badVote.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(badVote, skOther);
    assert(!engine.addVote(badVote));

    synapse::core::poe_v1::ValidationVoteV1 goodVote;
    goodVote.version = 1;
    goodVote.submitId = submitRes.submitId;
    goodVote.prevBlockHash = engine.chainSeed();
    goodVote.flags = 0;
    goodVote.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(goodVote, skSelected);
    assert(engine.addVote(goodVote));

    auto fin = engine.finalize(submitRes.submitId);
    assert(fin.has_value());
    assert(fin->finalizedAt == entry->timestamp);
    assert(fin->prevBlockHash == engine.chainSeed());

    engine.close();
    std::filesystem::remove_all(tmpDir, ec);
}

static std::vector<uint8_t> u64le(uint64_t v) {
    std::vector<uint8_t> out;
    out.reserve(8);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    return out;
}

static void testOpenRepairsEntryAndFinalizationCounters() {
    auto uniq = std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto tmpDir = std::filesystem::temp_directory_path() / ("synapsenet_poe_meta_repair_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);
    std::string dbPath = (tmpDir / "poe.db").string();

    synapse::core::PoeV1Engine engine;
    assert(engine.open(dbPath));

    synapse::core::PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = cfg.powBits;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 1;
    cfg.validatorsM = 1;
    engine.setConfig(cfg);

    auto sk = makeSk(71);
    auto pk = synapse::crypto::derivePublicKey(sk);
    engine.setStaticValidators({pk});

    auto submitRes = engine.submit(
        synapse::core::poe_v1::ContentType::TEXT,
        "repair_meta_title",
        std::string(80, 'r'),
        {},
        sk,
        true
    );
    assert(submitRes.ok);
    assert(submitRes.finalized);
    assert(engine.totalEntries() == 1);
    assert(engine.totalFinalized() == 1);
    engine.close();

    synapse::database::Database rawDb;
    assert(rawDb.open(dbPath));
    assert(rawDb.put("meta:poe_v1:entries", u64le(0)));
    assert(rawDb.put("meta:poe_v1:finalized", u64le(0)));
    rawDb.close();

    synapse::core::PoeV1Engine repaired;
    assert(repaired.open(dbPath));
    assert(repaired.totalEntries() == 1);
    assert(repaired.totalFinalized() == 1);
    repaired.close();
    std::filesystem::remove_all(tmpDir, ec);
}

static void testStakeIdentityDeterministicValidators() {
    auto uniq = std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto tmpDir = std::filesystem::temp_directory_path() / ("synapsenet_poe_stake_identity_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);
    std::string dbPath = (tmpDir / "poe.db").string();

    synapse::core::PoeV1Engine engine;
    assert(engine.open(dbPath));

    synapse::core::PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = cfg.powBits;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 2;
    cfg.validatorsM = 1;
    cfg.validatorMode = "stake";
    cfg.validatorMinStakeAtoms = 100;
    engine.setConfig(cfg);

    auto skAuthor = makeSk(81);
    auto skA = makeSk(82);
    auto skB = makeSk(83);
    auto skLow = makeSk(84);
    auto skNoId = makeSk(85);

    auto pkA = synapse::crypto::derivePublicKey(skA);
    auto pkB = synapse::crypto::derivePublicKey(skB);
    auto pkLow = synapse::crypto::derivePublicKey(skLow);
    auto pkNoId = synapse::crypto::derivePublicKey(skNoId);

    engine.setStaticValidators({pkA, pkB, pkLow, pkNoId});
    engine.setValidatorIdentity(pkA, true);
    engine.setValidatorIdentity(pkB, true);
    engine.setValidatorIdentity(pkLow, true);
    engine.setValidatorStake(pkA, 700);
    engine.setValidatorStake(pkB, 650);
    engine.setValidatorStake(pkLow, 90);
    engine.setValidatorStake(pkNoId, 1000);

    auto containsPk = [](const std::vector<synapse::crypto::PublicKey>& set, const synapse::crypto::PublicKey& pk) {
        return std::find(set.begin(), set.end(), pk) != set.end();
    };

    auto initialSet = engine.getDeterministicValidators();
    assert(initialSet.size() == 2);
    assert(containsPk(initialSet, pkA));
    assert(containsPk(initialSet, pkB));
    assert(!containsPk(initialSet, pkLow));
    assert(!containsPk(initialSet, pkNoId));

    auto s1 = engine.submit(
        synapse::core::poe_v1::ContentType::TEXT,
        "stake_identity_title_1",
        std::string(80, 's'),
        {},
        skAuthor,
        false
    );
    assert(s1.ok);

    synapse::core::poe_v1::ValidationVoteV1 lowVote;
    lowVote.version = 1;
    lowVote.submitId = s1.submitId;
    lowVote.prevBlockHash = engine.chainSeed();
    lowVote.flags = 0;
    lowVote.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(lowVote, skLow);
    assert(!engine.addVote(lowVote));

    synapse::core::poe_v1::ValidationVoteV1 noIdVote;
    noIdVote.version = 1;
    noIdVote.submitId = s1.submitId;
    noIdVote.prevBlockHash = engine.chainSeed();
    noIdVote.flags = 0;
    noIdVote.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(noIdVote, skNoId);
    assert(!engine.addVote(noIdVote));

    synapse::core::poe_v1::ValidationVoteV1 goodVoteA;
    goodVoteA.version = 1;
    goodVoteA.submitId = s1.submitId;
    goodVoteA.prevBlockHash = engine.chainSeed();
    goodVoteA.flags = 0;
    goodVoteA.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(goodVoteA, skA);
    assert(engine.addVote(goodVoteA));
    assert(engine.finalize(s1.submitId).has_value());

    engine.setValidatorStake(pkB, 80);
    auto reducedSet = engine.getDeterministicValidators();
    assert(reducedSet.size() == 1);
    assert(containsPk(reducedSet, pkA));

    auto s2 = engine.submit(
        synapse::core::poe_v1::ContentType::TEXT,
        "stake_identity_title_2",
        std::string(80, 't'),
        {},
        skAuthor,
        false
    );
    assert(s2.ok);

    synapse::core::poe_v1::ValidationVoteV1 voteBRejected;
    voteBRejected.version = 1;
    voteBRejected.submitId = s2.submitId;
    voteBRejected.prevBlockHash = engine.chainSeed();
    voteBRejected.flags = 0;
    voteBRejected.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(voteBRejected, skB);
    assert(!engine.addVote(voteBRejected));

    synapse::core::poe_v1::ValidationVoteV1 voteAAccepted;
    voteAAccepted.version = 1;
    voteAAccepted.submitId = s2.submitId;
    voteAAccepted.prevBlockHash = engine.chainSeed();
    voteAAccepted.flags = 0;
    voteAAccepted.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(voteAAccepted, skA);
    assert(engine.addVote(voteAAccepted));
    assert(engine.finalize(s2.submitId).has_value());

    engine.close();
    std::filesystem::remove_all(tmpDir, ec);
}

static void testAdaptiveQuorumGrowsWithValidatorSet() {
    auto uniq = std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto tmpDir = std::filesystem::temp_directory_path() / ("synapsenet_poe_adaptive_quorum_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);
    std::string dbPath = (tmpDir / "poe.db").string();

    synapse::core::PoeV1Engine engine;
    assert(engine.open(dbPath));

    synapse::core::PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = cfg.powBits;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 0;
    cfg.validatorsM = 0;
    cfg.adaptiveQuorum = true;
    cfg.adaptiveMinVotes = 1;
    cfg.allowSelfBootstrapValidator = true;
    cfg.minSubmitIntervalSeconds = 0;
    engine.setConfig(cfg);

    auto sk1 = makeSk(101);
    auto sk2 = makeSk(102);
    auto sk3 = makeSk(103);
    auto pk1 = synapse::crypto::derivePublicKey(sk1);
    auto pk2 = synapse::crypto::derivePublicKey(sk2);
    auto pk3 = synapse::crypto::derivePublicKey(sk3);

    auto r1 = engine.submit(
        synapse::core::poe_v1::ContentType::TEXT,
        "adaptive_title_one",
        std::string(80, 'a'),
        {},
        sk1,
        true
    );
    assert(r1.ok);
    assert(r1.finalized);
    assert(engine.effectiveSelectedValidators() == 1);
    assert(engine.effectiveRequiredVotes() == 1);

    engine.setStaticValidators({pk1, pk2});
    assert(engine.effectiveSelectedValidators() == 2);
    assert(engine.effectiveRequiredVotes() == 2);

    auto r2 = engine.submit(
        synapse::core::poe_v1::ContentType::TEXT,
        "adaptive_title_two",
        std::string(80, 'b'),
        {},
        sk1,
        true
    );
    assert(r2.ok);
    assert(!r2.finalized);

    synapse::core::poe_v1::ValidationVoteV1 vote2;
    vote2.version = 1;
    vote2.submitId = r2.submitId;
    vote2.prevBlockHash = engine.chainSeed();
    vote2.flags = 0;
    vote2.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(vote2, sk2);
    assert(engine.addVote(vote2));
    assert(engine.finalize(r2.submitId).has_value());

    engine.setStaticValidators({pk1, pk2, pk3});
    assert(engine.effectiveSelectedValidators() == 3);
    assert(engine.effectiveRequiredVotes() == 3);

    auto r3 = engine.submit(
        synapse::core::poe_v1::ContentType::TEXT,
        "adaptive_title_three",
        std::string(80, 'c'),
        {},
        sk1,
        true
    );
    assert(r3.ok);
    assert(!r3.finalized);

    synapse::core::poe_v1::ValidationVoteV1 vote3a;
    vote3a.version = 1;
    vote3a.submitId = r3.submitId;
    vote3a.prevBlockHash = engine.chainSeed();
    vote3a.flags = 0;
    vote3a.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(vote3a, sk2);
    assert(engine.addVote(vote3a));
    assert(!engine.finalize(r3.submitId).has_value());

    synapse::core::poe_v1::ValidationVoteV1 vote3b;
    vote3b.version = 1;
    vote3b.submitId = r3.submitId;
    vote3b.prevBlockHash = engine.chainSeed();
    vote3b.flags = 0;
    vote3b.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(vote3b, sk3);
    assert(engine.addVote(vote3b));
    assert(engine.finalize(r3.submitId).has_value());

    engine.close();
    std::filesystem::remove_all(tmpDir, ec);
}

static void testSubmitDoesNotSelfBootstrapValidatorWhenDisabled() {
    auto uniq = std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto tmpDir = std::filesystem::temp_directory_path() / ("synapsenet_poe_no_self_bootstrap_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);
    std::string dbPath = (tmpDir / "poe.db").string();

    synapse::core::PoeV1Engine engine;
    assert(engine.open(dbPath));

    synapse::core::PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = cfg.powBits;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 1;
    cfg.validatorsM = 1;
    cfg.allowSelfBootstrapValidator = false;
    engine.setConfig(cfg);

    auto sk = makeSk(91);
    auto submitRes = engine.submit(
        synapse::core::poe_v1::ContentType::TEXT,
        "mainnet_guard_title",
        std::string(80, 'g'),
        {},
        sk,
        true
    );
    assert(submitRes.ok);
    assert(!submitRes.finalized);
    assert(submitRes.acceptanceReward == 0);
    assert(engine.getStaticValidators().empty());

    engine.close();
    std::filesystem::remove_all(tmpDir, ec);
}

int main() {
    testCanonicalize();
    testCodeCanonicalize();
    testCodeVsTextHashing();
    testSimhashDeterminism();
    testMinhashDeterminism();
    testLeadingZeros();
    testValidatorSelectionDeterminism();
    testValidatorSelectionAlwaysFillsRequestedCount();
    testKnowledgeEntryV1Roundtrip();
    testValidationVoteV1Roundtrip();
    testAcceptanceRewardDeterminism();
    testEpochDeterminism();
    testDuplicateContentRejected();
    testVoteSelectionAndDeterministicFinalizationRecord();
    testOpenRepairsEntryAndFinalizationCounters();
    testStakeIdentityDeterministicValidators();
    testAdaptiveQuorumGrowsWithValidatorSet();
    testSubmitDoesNotSelfBootstrapValidatorWhenDisabled();
    std::cout << "PoE v1 determinism tests passed\n";
    return 0;
}
