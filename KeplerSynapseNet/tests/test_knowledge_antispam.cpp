#include "core/poe_v1_engine.h"
#include "core/poe_v1_objects.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

static synapse::crypto::PrivateKey makeSk(uint8_t tag) {
    synapse::crypto::PrivateKey sk{};
    for (size_t i = 0; i < sk.size(); ++i) sk[i] = static_cast<uint8_t>(tag + i);
    return sk;
}

static std::string makeTmpDb() {
    const auto base = std::filesystem::temp_directory_path();
    for (int i = 0; i < 64; ++i) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path p = base / ("synapse_poe_antispam_" + std::to_string(static_cast<unsigned long long>(now)) + "_" + std::to_string(i));
        std::error_code ec;
        if (std::filesystem::create_directories(p, ec)) return (p / "poe.db").string();
    }
    return "";
}

int main() {
    std::cout << "Running knowledge anti-spam tests...\n";
    const std::string dbPath = makeTmpDb();
    assert(!dbPath.empty());

    synapse::core::PoeV1Engine engine;
    engine.open(dbPath);
    auto cfg = engine.getConfig();
    cfg.powBits = 8;
    cfg.limits.minPowBits = cfg.powBits;
    cfg.validatorsN = 1;
    cfg.validatorsM = 1;
    cfg.limits.maxCitations = 1;
    engine.setConfig(cfg);

    const auto sk = makeSk(1);
    auto r = engine.submit(synapse::core::poe_v1::ContentType::TEXT, "Valid Title Here", std::string(60, 'A'), {}, sk, false);
    assert(r.ok);

    auto r2 = engine.submit(synapse::core::poe_v1::ContentType::TEXT, "Valid Title Here", std::string(60, 'A'), {}, sk, false);
    assert(!r2.ok);
    assert(r2.error == "duplicate_content");

    synapse::crypto::Hash256 c1{};
    synapse::crypto::Hash256 c2{};
    c1[0] = 1;
    c2[0] = 2;
    std::vector<synapse::crypto::Hash256> citations = {c1, c2};
    auto r3 = engine.submit(synapse::core::poe_v1::ContentType::TEXT, "Another Valid Title", std::string(60, 'B'), citations, makeSk(2), false);
    assert(!r3.ok);
    assert(r3.error == "too_many_citations");

    auto r4 = engine.submit(synapse::core::poe_v1::ContentType::TEXT, "", std::string(60, 'C'), {}, makeSk(3), false);
    assert(!r4.ok);

    std::filesystem::remove_all(std::filesystem::path(dbPath).parent_path());
    std::cout << "Anti-spam tests passed\n";
    return 0;
}
