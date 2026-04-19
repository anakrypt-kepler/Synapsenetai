#include "core/agent_coordination.h"
#include "core/agent_storage.h"
#include "crypto/crypto.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace synapse::core;

static std::string hashText(const std::string& text) {
    return synapse::crypto::toHex(
        synapse::crypto::sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size())
    );
}

static void testAuditTailRepairAfterPartialWrite() {
    const auto unique = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = std::filesystem::temp_directory_path() / ("synapsenet_naan_recovery_" + unique);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    AgentStorageAuditPolicy policy;
    policy.maxSegments = 4;
    policy.maxSegmentBytes = 4096;

    AgentStorageAuditLog log(policy);
    std::string reason;
    assert(log.open(root.string(), &reason));
    assert(log.append(1700000000, "tick", "obj1", "{\"ok\":1}", &reason));
    assert(log.append(1700000001, "tick", "obj2", "{\"ok\":2}", &reason));

    const auto badPath = root / "audit" / "segment_000001.log";
    {
        std::ofstream out(badPath, std::ios::app);
        assert(out.good());
        out << "partial|write|without|expected|fields";
    }

    AgentStorageAuditLog recovered(policy);
    assert(recovered.open(root.string(), &reason));
    auto stats = recovered.stats();
    assert(stats.recoveredTruncatedLines >= 1);
    assert(stats.retainedEvents >= 2);
    assert(recovered.append(1700000002, "tick", "obj3", "{\"ok\":3}", &reason));

    std::filesystem::remove_all(root, ec);
}

static void testCoordinationRecoveryNoopAfterRestart() {
    CoordinationHub hub;
    assert(hub.createRoom("tasks/main", RoomType::TASKS));
    auto identity = AgentIdentity::fromSeed(synapse::crypto::sha256("recovery_agent"));
    assert(identity.valid());
    assert(hub.postToRoom("tasks/main", identity, RoomMessageType::TASK, "{\"event\":\"recovery\"}", 1700000100));

    CoordinationRecoveryStats stats;
    std::string reason;
    assert(hub.verifyAndRepairIndexes(&stats, &reason));
    assert(reason == "ok" || reason == "repaired");
    assert(stats.roomCount == 1);
    assert(stats.roomArtifacts >= 1);
    assert(stats.observatoryEntries >= 1);
}

static std::string recoveryProjectionReplayDigest() {
    const auto unique = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = std::filesystem::temp_directory_path() / ("synapsenet_naan_recovery_replay_" + unique);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    AgentStorageAuditPolicy policy;
    policy.maxSegments = 4;
    policy.maxSegmentBytes = 4096;

    std::string reason;
    AgentStorageAuditLog log(policy);
    assert(log.open(root.string(), &reason));
    assert(log.append(1700001000, "tick", "init0", "{\"ok\":0}", &reason));
    assert(log.append(1700001001, "tick", "init1", "{\"ok\":1}", &reason));

    const auto badPath = root / "audit" / "segment_000001.log";
    {
        std::ofstream out(badPath, std::ios::app);
        assert(out.good());
        out << "v1|broken|tail";
    }

    CoordinationConfig cfg;
    cfg.maxArtifactHistory = 64;
    CoordinationHub hub(cfg);
    RoomConfig roomCfg;
    roomCfg.maxMessages = 2;
    assert(hub.createRoom("tasks/main", RoomType::TASKS, roomCfg));

    auto identity = AgentIdentity::fromSeed(synapse::crypto::sha256("naan_recovery_projection_seed"));
    assert(identity.valid());
    assert(hub.postToRoom("tasks/main", identity, RoomMessageType::TASK, "{\"event\":\"init0\"}", 1700001100));
    assert(hub.postToRoom("tasks/main", identity, RoomMessageType::TASK, "{\"event\":\"init1\"}", 1700001101));
    assert(hub.postToRoom("tasks/main", identity, RoomMessageType::TASK, "{\"event\":\"init2\"}", 1700001102));
    assert(hub.postToRoom("tasks/main", identity, RoomMessageType::TASK, "{\"event\":\"init3\"}", 1700001103));

    std::ostringstream trace;
    for (int cycle = 0; cycle < 3; ++cycle) {
        AgentStorageAuditLog replay(policy);
        std::string openReason;
        assert(replay.open(root.string(), &openReason));
        auto st = replay.stats();
        std::string appendReason;
        assert(replay.append(
            static_cast<uint64_t>(1700001200 + cycle),
            "tick",
            "cycle_" + std::to_string(cycle),
            "{\"ok\":1}",
            &appendReason
        ));

        CoordinationRecoveryStats repairStats;
        std::string repairReason;
        assert(hub.verifyAndRepairIndexes(&repairStats, &repairReason));
        auto feed = hub.getObservatoryFeed(0, 32);

        trace << cycle
              << "|" << openReason
              << "|" << st.lastSequence
              << "|" << st.retainedEvents
              << "|" << st.recoveredTruncatedLines
              << "|" << repairReason
              << "|" << repairStats.roomArtifacts
              << "|" << repairStats.observatoryEntries
              << "|" << repairStats.rebuiltEntries
              << "|" << repairStats.rebuiltIndexEntries
              << "\n";
        for (const auto& entry : feed) {
            trace << synapse::crypto::toHex(entry.hash) << "|" << entry.timestamp << "\n";
        }

        assert(hub.postToRoom(
            "tasks/main",
            identity,
            RoomMessageType::TASK,
            std::string("{\"cycle\":") + std::to_string(cycle) + "}",
            static_cast<uint64_t>(1700001300 + cycle)
        ));
    }

    std::filesystem::remove_all(root, ec);
    return hashText(trace.str());
}

static void testRecoveryProjectionReplayDeterminism() {
    const std::string first = recoveryProjectionReplayDigest();
    const std::string second = recoveryProjectionReplayDigest();
    assert(first == second);
}

int main() {
    testAuditTailRepairAfterPartialWrite();
    testCoordinationRecoveryNoopAfterRestart();
    testRecoveryProjectionReplayDeterminism();
    return 0;
}
