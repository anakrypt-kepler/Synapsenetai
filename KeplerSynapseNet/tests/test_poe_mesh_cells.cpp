#include "core/poe_v1_engine.h"
#include "core/poe_v1_objects.h"
#include "crypto/crypto.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

static synapse::crypto::PrivateKey makeSk(uint8_t tag) {
    synapse::crypto::PrivateKey sk{};
    for (size_t i = 0; i < sk.size(); ++i) sk[i] = static_cast<uint8_t>(tag + i * 3);
    return sk;
}

static void applyMajority(synapse::core::PoeV1Engine& engine,
                          const std::vector<synapse::crypto::PublicKey>& vals) {
    synapse::core::PoeV1Config cfg;
    cfg.powBits = 8;
    cfg.limits.minPowBits = 8;
    cfg.limits.maxPowBits = 28;
    cfg.validatorsN = 0;
    cfg.validatorsM = 0;
    cfg.adaptiveQuorum = true;
    cfg.adaptiveMajority = true;
    cfg.allowSelfBootstrapValidator = true;
    cfg.minSubmitIntervalSeconds = 0;
    engine.setConfig(cfg);
    engine.setStaticValidators(vals);
    for (const auto& pk : vals) engine.setValidatorIdentity(pk, true);
}

int main() {
    auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto dirA = std::filesystem::temp_directory_path() / ("poe_mesh_a_" + uniq);
    auto dirB = std::filesystem::temp_directory_path() / ("poe_mesh_b_" + uniq);
    std::error_code ec;
    std::filesystem::create_directories(dirA, ec);
    std::filesystem::create_directories(dirB, ec);

    auto skA = makeSk(11);
    auto skB = makeSk(22);
    auto pkA = synapse::crypto::derivePublicKey(skA);
    auto pkB = synapse::crypto::derivePublicKey(skB);

    synapse::core::PoeV1Engine a;
    synapse::core::PoeV1Engine b;
    assert(a.open((dirA / "poe.db").string()));
    assert(b.open((dirB / "poe.db").string()));
    applyMajority(a, {pkA, pkB});
    applyMajority(b, {pkA, pkB});
    assert(a.effectiveRequiredVotes() == 2);
    assert(b.effectiveRequiredVotes() == 2);

    auto sub = a.submit(synapse::core::poe_v1::ContentType::TEXT,
                        "mesh_title", std::string(80, 'x'), {}, skA, true);
    assert(sub.ok);
    assert(!sub.finalized);

    auto entry = a.getEntry(sub.submitId);
    assert(entry);
    std::string reason;
    assert(b.importEntry(*entry, &reason) || reason == "duplicate_submit");

    auto authorVotes = a.getVotesForSubmit(sub.submitId);
    assert(!authorVotes.empty());
    assert(b.addVote(authorVotes.front()));

    synapse::core::poe_v1::ValidationVoteV1 vote;
    vote.version = 1;
    vote.submitId = sub.submitId;
    vote.prevBlockHash = b.chainSeed();
    vote.flags = 0;
    vote.scores = {100, 100, 100};
    assert(synapse::core::poe_v1::signValidationVoteV1(vote, skB));
    assert(b.addVote(vote));
    assert(b.finalize(sub.submitId).has_value());

    auto blob = vote.serialize();
    auto vote2 = synapse::core::poe_v1::ValidationVoteV1::deserialize(blob);
    assert(vote2);
    assert(a.addVote(*vote2));
    assert(a.finalize(sub.submitId).has_value());
    assert(a.isFinalized(sub.submitId));
    assert(b.isFinalized(sub.submitId));

    a.close();
    b.close();
    std::filesystem::remove_all(dirA, ec);
    std::filesystem::remove_all(dirB, ec);
    std::cout << "two-cell mesh PoE passed\n";
    return 0;
}
