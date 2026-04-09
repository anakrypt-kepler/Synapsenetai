#include "core/poe_v1_engine.h"
#include "core/poe_v1_objects.h"
#include "crypto/crypto.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>

static synapse::crypto::PrivateKey makeSk(uint8_t tag) {
    synapse::crypto::PrivateKey sk{};
    for (size_t i = 0; i < sk.size(); ++i) sk[i] = static_cast<uint8_t>(tag + i);
    return sk;
}

static std::string makeTmpDb() {
    const auto base = std::filesystem::temp_directory_path();
    for (int i = 0; i < 64; ++i) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path p = base / ("synapse_poe_e2e_" + std::to_string(static_cast<unsigned long long>(now)) + "_" + std::to_string(i));
        std::error_code ec;
        if (std::filesystem::create_directories(p, ec)) return (p / "poe.db").string();
    }
    return "";
}

int main() {
    std::cout << "Running PoE E2E test...\n";
    const std::string dbPath = makeTmpDb();
    assert(!dbPath.empty());

    synapse::core::PoeV1Engine engine;
    engine.open(dbPath);
    auto cfg = engine.getConfig();
    cfg.powBits = 8;
    cfg.limits.minPowBits = cfg.powBits;
    cfg.validatorsN = 1;
    cfg.validatorsM = 1;
    engine.setConfig(cfg);

    const auto skAuthor = makeSk(1);
    const auto skValidator = makeSk(2);
    const auto pkValidator = synapse::crypto::derivePublicKey(skValidator);

    engine.setStaticValidators({pkValidator});

    auto r = engine.submit(
        synapse::core::poe_v1::ContentType::TEXT,
        "Test Title OK",
        "Body text that is long enough to pass validation checks here.",
        {},
        skAuthor,
        false
    );
    assert(r.ok);

    synapse::core::poe_v1::ValidationVoteV1 vote;
    vote.submitId = r.submitId;
    vote.prevBlockHash = engine.chainSeed();
    vote.flags = 0;
    vote.scores = {100, 100, 100};
    synapse::core::poe_v1::signValidationVoteV1(vote, skValidator);
    assert(engine.addVote(vote));

    auto fin = engine.finalize(r.submitId);
    assert(fin.has_value());
    assert(engine.isFinalized(r.submitId));
    assert(engine.totalFinalized() == 1);
    auto entryOpt = engine.getEntry(r.submitId);
    assert(entryOpt.has_value());
    assert(engine.calculateAcceptanceReward(*entryOpt) > 0);

    std::filesystem::remove_all(std::filesystem::path(dbPath).parent_path());

    std::cout << "PoE E2E test passed (NGT reward verified)\n";
    return 0;
}
