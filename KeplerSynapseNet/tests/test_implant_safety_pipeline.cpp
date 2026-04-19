#include "core/implant_safety_pipeline.h"
#include "crypto/crypto.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

static std::string uniqueStatePath() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
                ("synapsenet_implant_safety_" + std::to_string(static_cast<unsigned long long>(now)) + ".state");
    return path.string();
}

static synapse::crypto::Hash256 bundleId(const std::string& seed) {
    return synapse::crypto::sha256(seed);
}

static void testPrepareAndStageGates() {
    std::string statePath = uniqueStatePath();

    synapse::core::ImplantSafetyPipeline pipeline;
    std::string reason;
    assert(pipeline.open(statePath, &reason));

    auto id = bundleId("implant_bundle_safety_1");

    assert(!pipeline.markPrepare(id, false, true, &reason));
    assert(reason == "deterministic_tests_failed");

    assert(!pipeline.markPrepare(id, true, false, &reason));
    assert(reason == "sandbox_boundaries_failed");

    assert(pipeline.markPrepare(id, true, true, &reason));
    assert(reason == "prepare_passed");

    assert(!pipeline.canCommit(id, &reason));
    assert(reason == "canary_not_passed");

    assert(!pipeline.markWideHealth(id, true, &reason));
    assert(reason == "canary_not_passed");

    assert(!pipeline.markCanaryHealth(id, false, &reason));
    assert(reason == "canary_health_failed");

    assert(pipeline.markCanaryHealth(id, true, &reason));
    assert(reason == "canary_passed");

    assert(!pipeline.canCommit(id, &reason));
    assert(reason == "wide_not_passed");

    assert(!pipeline.markWideHealth(id, false, &reason));
    assert(reason == "wide_health_failed");

    assert(pipeline.markWideHealth(id, true, &reason));
    assert(reason == "wide_passed");

    assert(pipeline.canCommit(id, &reason));
    assert(reason == "ok");

    std::filesystem::remove(statePath);
}

static void testPersistenceAndClear() {
    std::string statePath = uniqueStatePath();

    auto id = bundleId("implant_bundle_safety_2");

    {
        synapse::core::ImplantSafetyPipeline pipeline;
        std::string reason;
        assert(pipeline.open(statePath, &reason));
        assert(pipeline.markPrepare(id, true, true, &reason));
        assert(pipeline.markCanaryHealth(id, true, &reason));
        assert(pipeline.markWideHealth(id, true, &reason));
    }

    {
        synapse::core::ImplantSafetyPipeline reloaded;
        std::string reason;
        assert(reloaded.open(statePath, &reason));

        auto rec = reloaded.getRecord(id);
        assert(rec.has_value());
        assert(rec->deterministicTestsPassed);
        assert(rec->sandboxBoundariesPassed);
        assert(rec->canaryHealthPassed);
        assert(rec->wideHealthPassed);

        assert(reloaded.canCommit(id, &reason));

        assert(reloaded.clearRecord(id, &reason));
        assert(!reloaded.getRecord(id).has_value());
        assert(!reloaded.canCommit(id, &reason));
        assert(reason == "safety_record_missing");
    }

    std::filesystem::remove(statePath);
}

static void testMissingPrepareRejections() {
    std::string statePath = uniqueStatePath();

    synapse::core::ImplantSafetyPipeline pipeline;
    std::string reason;
    assert(pipeline.open(statePath, &reason));

    auto id = bundleId("implant_bundle_missing_prepare");
    assert(!pipeline.markCanaryHealth(id, true, &reason));
    assert(reason == "prepare_not_passed");
    assert(!pipeline.markWideHealth(id, true, &reason));
    assert(reason == "prepare_not_passed");
    assert(!pipeline.canCommit(id, &reason));
    assert(reason == "safety_record_missing");

    std::filesystem::remove(statePath);
}

int main() {
    testPrepareAndStageGates();
    testPersistenceAndClear();
    testMissingPrepareRejections();
    return 0;
}
