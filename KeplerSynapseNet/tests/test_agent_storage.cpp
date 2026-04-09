#include "core/agent_storage.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace synapse::core;
namespace fs = std::filesystem;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "CHECK failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    } \
} while (0)

static fs::path makeTempDir(const std::string& prefix) {
    const auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path p = fs::temp_directory_path() / (prefix + "_" + uniq);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

static bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static bool testAppendAndReload() {
    AgentStorageAuditPolicy policy;
    policy.maxSegments = 8;
    policy.maxSegmentBytes = 4096;

    fs::path root = makeTempDir("synapsenet_agent_storage_append_reload");
    std::string reason;

    AgentStorageAuditLog log(policy);
    CHECK(log.open(root.string(), &reason));
    CHECK(reason == "ok");

    CHECK(log.append(100, "draft_enqueue", "d1", "{\"ok\":1}", &reason));
    CHECK(log.append(101, "review_upsert", "d1", "{\"approved\":true}", &reason));
    CHECK(log.append(102, "pipeline_drain", "batch", "{\"accepted\":1}", &reason));

    auto before = log.stats();
    CHECK(before.lastSequence == 3);
    CHECK(before.retainedEvents == 3);

    AgentStorageAuditLog reloaded(policy);
    CHECK(reloaded.open(root.string(), &reason));
    auto after = reloaded.stats();
    CHECK(after.lastSequence == 3);
    CHECK(after.retainedEvents == 3);
    CHECK(after.segmentCount >= 1);

    std::error_code ec;
    fs::remove_all(root, ec);
    return true;
}

static bool testTrailingCorruptionRepair() {
    AgentStorageAuditPolicy policy;
    policy.maxSegments = 8;
    policy.maxSegmentBytes = 4096;

    fs::path root = makeTempDir("synapsenet_agent_storage_repair");
    std::string reason;

    AgentStorageAuditLog log(policy);
    CHECK(log.open(root.string(), &reason));
    CHECK(log.append(200, "draft_enqueue", "d2", "{\"ok\":1}", &reason));
    CHECK(log.append(201, "draft_enqueue", "d3", "{\"ok\":1}", &reason));

    fs::path segment = root / "audit" / "segment_000001.log";
    {
        std::ofstream out(segment, std::ios::app);
        CHECK(out.good());
        out << "v1|bad|line|that|is|corrupted";
    }

    AgentStorageAuditLog repaired(policy);
    CHECK(repaired.open(root.string(), &reason));
    CHECK(startsWith(reason, "repaired_"));

    auto st = repaired.stats();
    CHECK(st.recoveredTruncatedLines >= 1);
    CHECK(st.retainedEvents == 2);
    CHECK(st.lastSequence == 2);

    std::error_code ec;
    fs::remove_all(root, ec);
    return true;
}

static bool testSegmentRotationAndRetention() {
    AgentStorageAuditPolicy policy;
    policy.maxSegments = 2;
    policy.maxSegmentBytes = 256;

    fs::path root = makeTempDir("synapsenet_agent_storage_rotation");
    std::string reason;

    AgentStorageAuditLog log(policy);
    CHECK(log.open(root.string(), &reason));

    for (uint64_t i = 0; i < 30; ++i) {
        std::string payload = "payload_" + std::to_string(i) + std::string(96, 'x');
        CHECK(log.append(300 + i, "event", "obj_" + std::to_string(i), payload, &reason));
    }

    auto st = log.stats();
    CHECK(st.segmentCount <= 2);
    CHECK(st.lastSequence >= 30);
    CHECK(st.retainedEvents > 0);

    AgentStorageAuditLog reloaded(policy);
    CHECK(reloaded.open(root.string(), &reason));
    auto st2 = reloaded.stats();
    CHECK(st2.segmentCount <= 2);
    CHECK(st2.lastSequence == st.lastSequence);
    CHECK(st2.retainedEvents > 0);

    std::error_code ec;
    fs::remove_all(root, ec);
    return true;
}

int main() {
    std::cout << "Running agent_storage tests...\n";
    if (!testAppendAndReload()) return 1;
    if (!testTrailingCorruptionRepair()) return 1;
    if (!testSegmentRotationAndRetention()) return 1;
    std::cout << "All agent_storage tests passed!\n";
    return 0;
}
