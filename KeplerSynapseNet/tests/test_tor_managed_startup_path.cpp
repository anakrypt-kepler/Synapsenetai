#include "core/tor_route_policy.h"

#include <cassert>
#include <optional>
#include <vector>

static void testManagedStartupReturnsAlreadyRunningWhenSocksAlreadyReachable() {
    int probeCalls = 0;
    int spawnCalls = 0;
    int sleepCalls = 0;
    int binaryCalls = 0;
    int mkdirCalls = 0;

    const auto out = synapse::core::runManagedTorStartup(
        true,
        40,
        [&]() {
            ++probeCalls;
            return true;
        },
        [&]() {
            ++binaryCalls;
            return true;
        },
        [&]() {
            ++mkdirCalls;
            return true;
        },
        [&]() {
            ++spawnCalls;
            return true;
        },
        [&]() -> std::optional<int64_t> { return 123; },
        [&](int) { ++sleepCalls; });

    assert(out.socksReachable);
    assert(out.usedExistingSocks);
    assert(!out.attemptedSpawn);
    assert(out.stage == "already_running");
    assert(probeCalls == 1);
    assert(binaryCalls == 0);
    assert(mkdirCalls == 0);
    assert(spawnCalls == 0);
    assert(sleepCalls == 0);
}

static void testManagedStartupSkipsSpawnWhenExternalMode() {
    int probeCalls = 0;
    int spawnCalls = 0;
    int binaryCalls = 0;

    const auto out = synapse::core::runManagedTorStartup(
        false,
        40,
        [&]() {
            ++probeCalls;
            return false;
        },
        [&]() {
            ++binaryCalls;
            return true;
        },
        [&]() { return true; },
        [&]() {
            ++spawnCalls;
            return true;
        },
        [&]() -> std::optional<int64_t> { return std::nullopt; },
        [&](int) {});

    assert(!out.socksReachable);
    assert(!out.attemptedSpawn);
    assert(out.stage == "external_mode");
    assert(probeCalls == 1);
    assert(binaryCalls == 0);
    assert(spawnCalls == 0);
}

static void testManagedStartupFailsAtBinaryCheckAndSpawnStages() {
    int probeCalls = 0;
    int spawnCalls = 0;
    int binaryCalls = 0;

    auto binaryFail = synapse::core::runManagedTorStartup(
        true,
        40,
        [&]() {
            ++probeCalls;
            return false;
        },
        [&]() {
            ++binaryCalls;
            return false;
        },
        [&]() { return true; },
        [&]() {
            ++spawnCalls;
            return true;
        },
        [&]() -> std::optional<int64_t> { return std::nullopt; },
        [&](int) {});

    assert(!binaryFail.socksReachable);
    assert(binaryFail.stage == "tor_binary_missing");
    assert(!binaryFail.attemptedSpawn);
    assert(binaryCalls == 1);
    assert(spawnCalls == 0);

    auto spawnFail = synapse::core::runManagedTorStartup(
        true,
        40,
        [&]() { return false; },
        [&]() { return true; },
        [&]() { return true; },
        [&]() { return false; },
        [&]() -> std::optional<int64_t> { return std::nullopt; },
        [&](int) {});

    assert(!spawnFail.socksReachable);
    assert(spawnFail.attemptedSpawn);
    assert(!spawnFail.spawnSucceeded);
    assert(spawnFail.stage == "spawn_failed");
}

static void testManagedStartupSucceedsAfterProbeRetriesAndReadsPid() {
    std::vector<bool> probeSeq = {false, false, false, true};
    size_t probeIdx = 0;
    int sleepCalls = 0;
    int spawnCalls = 0;
    int pidReads = 0;

    const auto out = synapse::core::runManagedTorStartup(
        true,
        40,
        [&]() {
            const bool v = probeIdx < probeSeq.size() ? probeSeq[probeIdx] : probeSeq.back();
            ++probeIdx;
            return v;
        },
        [&]() { return true; },
        [&]() { return true; },
        [&]() {
            ++spawnCalls;
            return true;
        },
        [&]() -> std::optional<int64_t> {
            ++pidReads;
            return static_cast<int64_t>(77777);
        },
        [&](int ms) {
            assert(ms == 250);
            ++sleepCalls;
        });

    assert(out.socksReachable);
    assert(!out.usedExistingSocks);
    assert(out.attemptedSpawn);
    assert(out.spawnSucceeded);
    assert(out.probeAttemptsUsed == 3);
    assert(out.managedPid == 77777);
    assert(out.stage == "ready");
    assert(spawnCalls == 1);
    assert(pidReads == 1);
    assert(sleepCalls == 2);
}

static void testManagedStartupTimesOutWhenSocksNeverBecomesReachable() {
    int sleepCalls = 0;

    const auto out = synapse::core::runManagedTorStartup(
        true,
        3,
        [&]() { return false; },
        [&]() { return true; },
        [&]() { return true; },
        [&]() { return true; },
        [&]() -> std::optional<int64_t> { return std::nullopt; },
        [&](int ms) {
            assert(ms == 250);
            ++sleepCalls;
        });

    assert(!out.socksReachable);
    assert(out.attemptedSpawn);
    assert(out.spawnSucceeded);
    assert(out.probeAttemptsUsed == 3);
    assert(out.managedPid == 0);
    assert(out.stage == "timeout");
    assert(sleepCalls == 3);
}

int main() {
    testManagedStartupReturnsAlreadyRunningWhenSocksAlreadyReachable();
    testManagedStartupSkipsSpawnWhenExternalMode();
    testManagedStartupFailsAtBinaryCheckAndSpawnStages();
    testManagedStartupSucceedsAfterProbeRetriesAndReadsPid();
    testManagedStartupTimesOutWhenSocksNeverBecomesReachable();
    return 0;
}
