#include "core/poe_v1_engine.h"
#include "core/poe_v1_layers.h"
#include "core/poe_v1_objects.h"
#include "crypto/crypto.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using synapse::core::PoeV1Config;
using synapse::core::PoeV1Engine;
using synapse::core::poe_v1::AbsenceOutcome;
using synapse::core::poe_v1::ContentType;
using synapse::core::poe_v1::HarvestRecipeV1;
using synapse::core::poe_v1::KnowStatus;
using synapse::core::poe_v1::LymphDraftV1;
using synapse::core::poe_v1::RetractV1;
using synapse::core::poe_v1::ScarV1;
using synapse::core::poe_v1::ValidationVoteV1;
using synapse::core::poe_v1::WitnessV1;
using synapse::core::poe_v1::containsInferenceKernelPayload;
using synapse::core::poe_v1::hashFetchedBody;
using synapse::core::poe_v1::lymphExportIfMatch;
using synapse::core::poe_v1::signHarvestRecipeV1;
using synapse::core::poe_v1::signRetractV1;
using synapse::core::poe_v1::signValidationVoteV1;
using synapse::core::poe_v1::signWitnessV1;

static synapse::crypto::PrivateKey makeSk(uint8_t tag) {
    synapse::crypto::PrivateKey sk{};
    for (size_t i = 0; i < sk.size(); ++i) sk[i] = static_cast<uint8_t>(tag + i);
    return sk;
}

static std::filesystem::path tmpPoe(const std::string& name) {
    auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto dir = std::filesystem::temp_directory_path() / (name + "_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static PoeV1Config testCfg() {
    PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = 8;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 1;
    cfg.validatorsM = 1;
    cfg.allowSelfBootstrapValidator = true;
    cfg.minSubmitIntervalSeconds = 0;
    cfg.witnessWindowSeconds = 10;
    cfg.absenceQuorumN = 2;
    cfg.noveltyBands = 0;
    return cfg;
}

static void openSolo(PoeV1Engine& engine, const std::string& db, const synapse::crypto::PublicKey& pk) {
    assert(engine.open(db));
    engine.setConfig(testCfg());
    engine.setStaticValidators({pk});
    engine.setValidatorIdentity(pk, true);
}

static std::vector<uint8_t> bytesOf(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

static HarvestRecipeV1 makeRecipe(const std::string& loc, const std::vector<uint8_t>& page) {
    HarvestRecipeV1 r;
    r.version = 1;
    r.locator = loc;
    r.selector = "body";
    r.mediaType = "text/plain";
    r.bodyHash = hashFetchedBody(page);
    return r;
}

static void voteSolo(PoeV1Engine& engine, const synapse::crypto::Hash256& sid, const synapse::crypto::PrivateKey& sk) {
    ValidationVoteV1 v;
    v.version = 1;
    v.submitId = sid;
    v.prevBlockHash = engine.chainSeed();
    v.flags = 0;
    v.scores = {100, 100, 100};
    signValidationVoteV1(v, sk);
    engine.addVote(v);
}

static void testMouthIsolation() {
    auto dir = tmpPoe("poe_mouth");
    auto sk = makeSk(11);
    auto pk = synapse::crypto::derivePublicKey(sk);
    PoeV1Engine engine;
    openSolo(engine, (dir / "poe.db").string(), pk);

    auto dirty = engine.submit(
        ContentType::CODE,
        "patch_title_ok",
        std::string("score from model says this patch is good ") + std::string(40, 'x'),
        {},
        sk,
        true);
    assert(!dirty.ok);
    assert(dirty.error == "inference_kernel_payload");

    auto clean = engine.submit(
        ContentType::CODE,
        "patch_title_ok",
        std::string(80, 'c'),
        {},
        sk,
        true);
    assert(clean.ok);
    assert(clean.finalized);
    assert(engine.shouldMintAcceptanceReward(clean.submitId));

    ValidationVoteV1 badVote;
    badVote.version = 1;
    badVote.submitId = clean.submitId;
    badVote.prevBlockHash = engine.chainSeed();
    badVote.flags = 0;
    badVote.scores = {100, 100, 100};
    badVote.note = "score from model";
    signValidationVoteV1(badVote, sk);
    assert(!engine.addVote(badVote));

    ValidationVoteV1 flagVote = badVote;
    flagVote.note.clear();
    flagVote.flags = synapse::core::poe_v1::kVoteFlagInference;
    signValidationVoteV1(flagVote, sk);
    assert(!engine.addVote(flagVote));

    assert(containsInferenceKernelPayload("<|im_start|>user"));
    assert(!containsInferenceKernelPayload("int main() { return 0; }"));

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

static void testHarvestRecipeAndReplay() {
    auto dir = tmpPoe("poe_recipe");
    auto sk = makeSk(21);
    auto pk = synapse::crypto::derivePublicKey(sk);
    PoeV1Engine engine;
    openSolo(engine, (dir / "poe.db").string(), pk);

    const std::vector<uint8_t> page = bytesOf("public harvest body v1");
    auto recipe = makeRecipe("https://example.invalid/item/alpha", page);
    engine.setRecipeFetcher([&](const HarvestRecipeV1&) {
        return page;
    });

    auto pending = engine.submitRecipe(recipe, {}, sk, false);
    assert(pending.ok);
    assert(!pending.finalized);
    assert(!engine.shouldMintAcceptanceReward(pending.submitId));

    std::string err;
    assert(engine.finalize(pending.submitId) == std::nullopt);
    assert(engine.replayRecipe(*engine.getRecipeIdForSubmit(pending.submitId), sk, &err));
    voteSolo(engine, pending.submitId, sk);
    auto fin = engine.finalize(pending.submitId);
    assert(fin.has_value());
    assert(engine.shouldMintAcceptanceReward(pending.submitId));

    // Essay/GGUF commentary is not a recipe mint input.
    auto essay = engine.submit(
        ContentType::TEXT,
        "essay_title_here",
        std::string("gguf commentary from the local model dump ") + std::string(20, 'e'),
        {},
        sk,
        true);
    assert(!essay.ok);

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

static void testAbsenceQuorum() {
    auto dir = tmpPoe("poe_absence");
    auto sk1 = makeSk(31);
    auto sk2 = makeSk(32);
    auto pk1 = synapse::crypto::derivePublicKey(sk1);
    PoeV1Engine engine;
    openSolo(engine, (dir / "poe.db").string(), pk1);

    const std::vector<uint8_t> page{'a', 'b', 'c'};
    auto recipe = makeRecipe("https://example.invalid/search/none", page);
    signHarvestRecipeV1(recipe, sk1);
    std::string err;
    assert(engine.importRecipe(recipe, &err));
    auto rid = recipe.recipeId();

    engine.setAbsenceSearch([](const HarvestRecipeV1&, uint64_t, uint64_t) {
        return std::vector<synapse::crypto::Hash256>{};
    });
    assert(engine.reportAbsence(rid, 100, 200, sk1, &err));
    assert(!engine.tryAbsenceQuorum(rid).has_value());
    assert(engine.reportAbsence(rid, 100, 200, sk2, &err));
    auto q = engine.tryAbsenceQuorum(rid);
    assert(q.has_value());
    assert(q->outcome == AbsenceOutcome::NOT_SEEN);
    assert(q->reports.size() >= 2);

    auto dir2 = tmpPoe("poe_absence_hit");
    PoeV1Engine engine2;
    openSolo(engine2, (dir2 / "poe.db").string(), pk1);
    assert(engine2.importRecipe(recipe, &err));
    engine2.setAbsenceSearch([](const HarvestRecipeV1&, uint64_t, uint64_t) {
        return std::vector<synapse::crypto::Hash256>{synapse::crypto::sha256(std::string("hit"))};
    });
    assert(engine2.reportAbsence(rid, 100, 200, sk1, &err));
    assert(engine2.reportAbsence(rid, 100, 200, sk2, &err));
    assert(!engine2.tryAbsenceQuorum(rid).has_value());

    engine.close();
    engine2.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::remove_all(dir2, ec);
}

static void testHalfLifeAndWitness() {
    auto dir = tmpPoe("poe_halflife");
    auto sk = makeSk(41);
    auto sk2 = makeSk(42);
    auto pk = synapse::crypto::derivePublicKey(sk);
    PoeV1Engine engine;
    openSolo(engine, (dir / "poe.db").string(), pk);
    engine.setNowUnix(1000);

    auto r = engine.submit(ContentType::CODE, "legacy_code_patch", std::string(80, 'p'), {}, sk, true);
    assert(r.ok);
    assert(r.finalized);
    assert(engine.knowStatus(r.submitId) == KnowStatus::ACTIVE);
    assert(engine.refreshKnowStatus(r.submitId) == KnowStatus::ACTIVE);

    engine.setNowUnix(1020);
    assert(engine.refreshKnowStatus(r.submitId) == KnowStatus::SLEEPING);
    assert(engine.getEntry(r.submitId).has_value());
    assert(engine.isFinalized(r.submitId));
    assert(engine.shouldMintAcceptanceReward(r.submitId));

    auto dir2 = tmpPoe("poe_halflife_keep");
    PoeV1Engine engine2;
    openSolo(engine2, (dir2 / "poe.db").string(), pk);
    engine2.setNowUnix(2000);
    auto r2 = engine2.submit(ContentType::CODE, "legacy_code_kept", std::string(80, 'q'), {}, sk, true);
    assert(r2.finalized);
    WitnessV1 w;
    w.version = 1;
    w.submitId = r2.submitId;
    w.witnessedAt = 2005;
    signWitnessV1(w, sk2);
    engine2.setNowUnix(2005);
    std::string err;
    assert(engine2.addWitness(w, &err));
    engine2.setNowUnix(2012);
    assert(engine2.refreshKnowStatus(r2.submitId) == KnowStatus::ACTIVE);

    engine.close();
    engine2.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::remove_all(dir2, ec);
}

static void testRetraction() {
    auto dir = tmpPoe("poe_retract");
    auto sk = makeSk(51);
    auto pk = synapse::crypto::derivePublicKey(sk);
    PoeV1Engine engine;
    openSolo(engine, (dir / "poe.db").string(), pk);

    auto r = engine.submit(ContentType::CODE, "code_to_retract", std::string(80, 'r'), {}, sk, true);
    assert(r.finalized);
    assert(engine.shouldMintAcceptanceReward(r.submitId));

    RetractV1 retract;
    retract.version = 1;
    retract.submitId = r.submitId;
    retract.retractedAt = 1;
    signRetractV1(retract, sk);
    std::string err;
    assert(engine.addRetract(retract, &err));
    assert(engine.isRetracted(r.submitId));
    assert(engine.isRewardUnreclaimable(r.submitId));
    assert(!engine.shouldMintAcceptanceReward(r.submitId));
    assert(engine.knowStatus(r.submitId) == KnowStatus::RETRACTED);
    assert(engine.getEntry(r.submitId).has_value());
    assert(engine.getRetract(r.submitId).has_value());

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

static void testAnonymousSeniority() {
    auto dir = tmpPoe("poe_senior");
    auto sk = makeSk(61);
    auto sk2 = makeSk(62);
    auto pk = synapse::crypto::derivePublicKey(sk);
    auto pk2 = synapse::crypto::derivePublicKey(sk2);
    PoeV1Engine engine;
    assert(engine.open((dir / "poe.db").string()));
    auto cfg = testCfg();
    engine.setConfig(cfg);
    engine.setStaticValidators({pk, pk2});
    engine.setValidatorIdentity(pk, true);
    engine.setValidatorIdentity(pk2, true);
    cfg.validatorsN = 1;
    cfg.validatorsM = 1;
    engine.setConfig(cfg);
    engine.setStaticValidators({pk});

    std::vector<synapse::crypto::Hash256> mine;
    for (int i = 0; i < 3; ++i) {
        auto r = engine.submit(
            ContentType::CODE,
            std::string("senior_patch_") + std::to_string(i) + "xx",
            std::string(80, static_cast<char>('a' + i)),
            {},
            sk,
            true);
        assert(r.ok);
        assert(r.finalized);
        std::string err;
        assert(engine.publishSeniorityToken(r.submitId, sk, &err));
        mine.push_back(r.submitId);
    }

    engine.setStaticValidators({pk2});
    auto other = engine.submit(
        ContentType::CODE,
        "decoy_patch_xx",
        std::string(80, 'z'),
        {},
        sk2,
        true);
    assert(other.ok && other.finalized);
    std::string err;
    assert(engine.publishSeniorityToken(other.submitId, sk2, &err));

    auto proof = engine.proveSeniority(sk, 2, &err);
    assert(proof.has_value());
    assert(engine.verifySeniorityProof(*proof, &err));

    auto forged = *proof;
    assert(!forged.ringSigs.empty());
    if (!forged.ringSigs[0].empty()) forged.ringSigs[0][0] ^= 0xFF;
    assert(!engine.verifySeniorityProof(forged, &err));

    auto blob = proof->serialize();
    for (const auto& sid : mine) {
        auto needle = std::vector<uint8_t>(sid.begin(), sid.end());
        auto it = std::search(blob.begin(), blob.end(), needle.begin(), needle.end());
        assert(it == blob.end());
    }

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

static void testTwoClocks() {
    auto dir = tmpPoe("poe_clocks");
    auto sk = makeSk(71);
    auto pk = synapse::crypto::derivePublicKey(sk);
    PoeV1Engine engine;
    openSolo(engine, (dir / "poe.db").string(), pk);

    const std::vector<uint8_t> pageA = bytesOf("clock-body-A");
    const std::vector<uint8_t> pageB = bytesOf("clock-body-B");
    engine.setRecipeFetcher([&](const HarvestRecipeV1& rec) -> std::optional<std::vector<uint8_t>> {
        if (rec.locator.find("alpha") != std::string::npos) return pageA;
        return pageB;
    });

    auto recA = makeRecipe("https://example.invalid/clock/alpha", pageA);
    auto a = engine.submitRecipe(recA, {}, sk, false);
    assert(a.ok);
    auto entryA = engine.getEntry(a.submitId);
    assert(entryA);

    auto recB = makeRecipe("https://example.invalid/clock/beta", pageB);
    auto b = engine.submitRecipe(recB, {entryA->contentId()}, sk, false);
    assert(b.ok);

    auto order = engine.citationDagFinalizeOrder({b.submitId, a.submitId});
    assert(order.size() == 2);
    assert(order[0] == a.submitId);
    assert(order[1] == b.submitId);

    std::string err;
    assert(engine.replayRecipe(*engine.getRecipeIdForSubmit(a.submitId), sk, &err));
    assert(engine.replayRecipe(*engine.getRecipeIdForSubmit(b.submitId), sk, &err));
    voteSolo(engine, a.submitId, sk);
    voteSolo(engine, b.submitId, sk);
    assert(!engine.finalize(b.submitId).has_value());
    assert(engine.finalize(a.submitId).has_value());
    assert(engine.finalize(b.submitId).has_value());

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

static void testScar() {
    auto dir = tmpPoe("poe_scar");
    auto sk = makeSk(81);
    auto pk = synapse::crypto::derivePublicKey(sk);
    PoeV1Engine engine;
    openSolo(engine, (dir / "poe.db").string(), pk);

    ScarV1 ok;
    ok.gateClass = "captcha";
    ok.dayUtc = "2026-09-16";
    ok.methodClass = "GET";
    std::string err;
    assert(engine.importScar(ok, &err));
    assert(engine.getScar(ok.scarId()).has_value());

    ScarV1 bad = ok;
    bad.cookie = "sid=secret";
    assert(!engine.importScar(bad, &err));
    assert(err == "scar_secret_field");

    ScarV1 sess = ok;
    sess.session = "abc";
    assert(!engine.importScar(sess, &err));

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

static void testLymph() {
    auto dir = tmpPoe("poe_lymph");
    auto sk = makeSk(91);
    auto pk = synapse::crypto::derivePublicKey(sk);
    PoeV1Engine engine;
    openSolo(engine, (dir / "poe.db").string(), pk);

    const std::vector<uint8_t> page = bytesOf("lymph-local-page");
    LymphDraftV1 draft;
    draft.recipe = makeRecipe("https://example.invalid/lymph/one", page);
    signHarvestRecipeV1(draft.recipe, sk);
    draft.pageBytes = page;

    std::string err;
    auto mismatch = engine.lymphExport(draft, std::vector<uint8_t>{'n', 'o'}, &err);
    assert(!mismatch);
    assert(err == "lymph_mismatch");

    auto exported = engine.lymphExport(draft, page, &err);
    assert(exported.has_value());
    auto ser = exported->serialize();
    auto round = HarvestRecipeV1::deserialize(ser);
    assert(round);
    assert(round->bodyHash == hashFetchedBody(page));
    std::string serText(ser.begin(), ser.end());
    assert(serText.find("lymph-local-page") == std::string::npos);

    auto freeFn = lymphExportIfMatch(draft, page, &err);
    assert(freeFn);
    auto bad = lymphExportIfMatch(draft, std::vector<uint8_t>(page.size(), 'x'), &err);
    assert(!bad);

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

static void testLiveCodePathUntouched() {
    auto dir = tmpPoe("poe_live_code");
    auto sk = makeSk(101);
    auto pk = synapse::crypto::derivePublicKey(sk);
    PoeV1Engine engine;
    openSolo(engine, (dir / "poe.db").string(), pk);

    auto r = engine.submit(
        ContentType::CODE,
        "ide_patch_title",
        std::string("// Kepler IDE patch body stays mineable\n") + std::string(50, 'k'),
        {},
        sk,
        true);
    assert(r.ok);
    assert(r.finalized);
    assert(r.acceptanceReward > 0);
    assert(engine.shouldMintAcceptanceReward(r.submitId));
    assert(engine.getEntry(r.submitId)->contentType == ContentType::CODE);

    engine.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

int main() {
    testMouthIsolation();
    testHarvestRecipeAndReplay();
    testAbsenceQuorum();
    testHalfLifeAndWitness();
    testRetraction();
    testAnonymousSeniority();
    testTwoClocks();
    testScar();
    testLymph();
    testLiveCodePathUntouched();
    std::cout << "PoE nine-layer tests passed\n";
    return 0;
}
