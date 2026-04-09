#include "core/agent_coordination.h"
#include "core/agent_draft_queue.h"
#include "core/agent_scheduler.h"
#include "core/agent_score.h"
#include "core/agent_storage.h"
#include "core/update_bundle.h"
#include "crypto/crypto.h"
#include "web/web.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace synapse::core;

static std::string hashText(const std::string& text) {
    return synapse::crypto::toHex(
        synapse::crypto::sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size())
    );
}

static std::string schedulerTraceDigest() {
    AgentTaskSchedulerPolicy policy;
    policy.tickSeconds = 1;
    policy.epochTicks = 120;
    policy.starvationThresholdTicks = 9;
    policy.epochBudget = {500, 500, 500};
    policy.fixedPriority = {
        AgentTaskClass::SUBMIT,
        AgentTaskClass::REVIEW,
        AgentTaskClass::VERIFY,
        AgentTaskClass::DRAFT,
        AgentTaskClass::RESEARCH
    };
    for (size_t i = 0; i < kAgentTaskClassCount; ++i) {
        policy.classes[i].cost = {3, 2, 1};
        policy.classes[i].minIntervalTicks = 1;
    }

    AgentTaskScheduler scheduler(policy);
    std::array<AgentTaskRequest, kAgentTaskClassCount> requests = {
        AgentTaskRequest{AgentTaskClass::RESEARCH, true},
        AgentTaskRequest{AgentTaskClass::VERIFY, true},
        AgentTaskRequest{AgentTaskClass::REVIEW, true},
        AgentTaskRequest{AgentTaskClass::DRAFT, true},
        AgentTaskRequest{AgentTaskClass::SUBMIT, true}
    };

    std::ostringstream trace;
    for (int i = 0; i < 40; ++i) {
        auto decision = scheduler.tick(requests);
        trace << decision.scheduled
              << "|" << synapse::core::agentTaskClassToString(decision.taskClass)
              << "|" << decision.starvationFallback
              << "|" << decision.snapshot.tick
              << "|" << decision.snapshot.remaining.cpu
              << "|" << decision.snapshot.remaining.ram
              << "|" << decision.snapshot.remaining.network
              << "\n";
    }
    return hashText(trace.str());
}

static std::string scoreTraceDigest() {
    AgentScorePolicy policy;
    policy.scoreMin = -400;
    policy.scoreMax = 400;
    policy.initialScore = 0;
    policy.acceptWeight = 11;
    policy.rejectWeight = 27;
    policy.violationWeight = 90;
    policy.decayNumerator = 95;
    policy.decayDenominator = 100;
    policy.throttledBelowOrEqual = -40;
    policy.reviewOnlyBelowOrEqual = -90;
    policy.localDraftOnlyBelowOrEqual = -180;
    policy.localDraftRecoveryAbove = -120;
    policy.localDraftRecoveryCleanSteps = 4;

    AgentScoreTracker tracker(policy);
    const std::vector<std::array<uint32_t, 3>> steps = {
        {2, 0, 0},
        {0, 1, 0},
        {0, 0, 1},
        {1, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0}
    };

    std::ostringstream trace;
    for (const auto& step : steps) {
        tracker.applyStep(step[0], step[1], step[2]);
        auto s = tracker.snapshot();
        trace << s.score
              << "|" << synapse::core::agentScoreBandToString(s.band)
              << "|" << s.throttled
              << "|" << s.quarantined
              << "|" << s.cleanSteps
              << "|" << s.steps
              << "\n";
    }
    return hashText(trace.str());
}

static std::string routePolicyDigest() {
    synapse::web::SearchConfig cfg = synapse::web::defaultSearchConfig();
    cfg.fetchAllowlistRoutes = {"suffix:.onion", "prefix:https://docs.synapsenet.org/"};
    cfg.fetchDenylistRoutes = {"host:blocked.onion", "prefix:https://docs.synapsenet.org/private"};

    const std::vector<std::string> urls = {
        "http://allowed.onion/index",
        "http://blocked.onion/index",
        "https://docs.synapsenet.org/guide",
        "https://docs.synapsenet.org/private/spec",
        "https://example.org/page"
    };

    std::ostringstream trace;
    for (const auto& url : urls) {
        trace << url << "|" << synapse::web::isUrlAllowedByRoutePolicy(url, cfg) << "\n";
    }
    return hashText(trace.str());
}

static std::string searchConfigSanitizationDigest() {
    namespace fs = std::filesystem;
    const auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path root = fs::temp_directory_path() / ("synapsenet_det_cfg_sanitize_" + uniq);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    fs::path src = root / "input.conf";
    {
        std::ofstream out(src);
        if (!out.good()) return hashText("cfg_open_failed");
        out << "clearnet_engines=duckduckgo,duckduckgo,ahmia\n";
        out << "darknet_engines=ahmia,torch,duckduckgo,ahmia\n";
        out << "custom_darknet_urls=http://alpha.onion/,http://alpha.onion/\n";
        out << "naan_force_tor_mode=1\n";
        out << "route_clearnet_through_tor=0\n";
        out << "naan_auto_search_mode=invalid\n";
        out << "naan_auto_search_max_results=100\n";
        out << "unknown_key=1\n";
        out << "timeout_seconds=invalid\n";
    }

    std::ostringstream trace;
    for (int i = 0; i < 3; ++i) {
        synapse::web::SearchConfig cfg;
        synapse::web::SearchConfigValidationStats validation;
        if (!synapse::web::loadSearchConfig(src.string(), cfg, &validation)) {
            return hashText("cfg_load_failed");
        }

        fs::path outPath = root / ("canonical_" + std::to_string(i) + ".conf");
        if (!synapse::web::saveSearchConfig(cfg, outPath.string())) {
            return hashText("cfg_save_failed");
        }

        std::ifstream in(outPath, std::ios::binary);
        if (!in.good()) return hashText("cfg_read_failed");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        trace << validation.totalLines
              << "|" << validation.appliedLines
              << "|" << validation.invalidLines
              << "|" << validation.unknownKeys
              << "|" << content
              << "\n";
        src = outPath;
    }

    fs::remove_all(root, ec);
    return hashText(trace.str());
}

static std::string updatePayloadDigest() {
    synapse::crypto::PrivateKey sk{};
    for (size_t i = 0; i < sk.size(); ++i) sk[i] = static_cast<uint8_t>(i + 7);

    UpdateManifest manifest;
    UpdateChunk c1;
    c1.hash = synapse::crypto::sha256("chunk_a");
    c1.size = 100;
    UpdateChunk c2;
    c2.hash = synapse::crypto::sha256("chunk_b");
    c2.size = 120;
    UpdateChunk c3;
    c3.hash = synapse::crypto::sha256("chunk_a");
    c3.size = 100;
    manifest.chunks = {c2, c1, c3};
    manifest.target = "implant/driver";
    manifest.protocolMin = 1;
    manifest.protocolMax = 2;

    std::string reason;
    assert(signUpdateManifest(manifest, sk, &reason));
    auto bytes = manifest.serialize();
    std::string payloadHash = synapse::crypto::toHex(
        synapse::crypto::sha256(bytes.data(), bytes.size())
    );

    std::ostringstream trace;
    trace << synapse::crypto::toHex(manifest.bundleId) << "\n";
    trace << synapse::crypto::toHex(manifest.contentHash) << "\n";
    trace << synapse::crypto::toHex(manifest.signer) << "\n";
    trace << synapse::crypto::toHex(manifest.signature) << "\n";
    trace << payloadHash << "\n";
    return hashText(trace.str());
}

static synapse::core::AgentDraftProposal makeReplayDraft(const synapse::core::AgentIdentity& id,
                                                         uint64_t ts,
                                                         const std::string& title,
                                                         const std::string& body,
                                                         uint64_t nonce) {
    synapse::core::AgentDraftProposal p;
    p.createdAt = ts;
    p.author = id.id;
    p.title = title;
    p.body = body;
    p.powBits = 0;
    p.powNonce = nonce;
    p.signature = synapse::crypto::sign(p.signatureHash(), id.privateKey);
    return p;
}

static std::string recoveryProjectionReplayDigest() {
    namespace fs = std::filesystem;
    const auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path root = fs::temp_directory_path() / ("synapsenet_det_replay_recovery_" + uniq);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    AgentStorageAuditPolicy auditPolicy;
    auditPolicy.maxSegments = 4;
    auditPolicy.maxSegmentBytes = 4096;

    std::string reason;
    AgentStorageAuditLog initialLog(auditPolicy);
    assert(initialLog.open(root.string(), &reason));
    assert(initialLog.append(1700002000, "tick", "init_a", "{\"ok\":1}", &reason));
    assert(initialLog.append(1700002001, "tick", "init_b", "{\"ok\":2}", &reason));

    const fs::path segment = root / "audit" / "segment_000001.log";
    {
        std::ofstream out(segment, std::ios::app);
        assert(out.good());
        out << "v1|corrupt|tail";
    }

    CoordinationConfig coordinationCfg;
    coordinationCfg.maxArtifactHistory = 64;
    CoordinationHub hub(coordinationCfg);
    RoomConfig roomCfg;
    roomCfg.maxMessages = 2;
    assert(hub.createRoom("tasks/main", RoomType::TASKS, roomCfg));

    const auto identity = AgentIdentity::fromSeed(synapse::crypto::sha256("determinism_recovery_projection_seed"));
    assert(identity.valid());
    assert(hub.postToRoom("tasks/main", identity, RoomMessageType::TASK, "{\"event\":\"init0\"}", 1700002100));
    assert(hub.postToRoom("tasks/main", identity, RoomMessageType::TASK, "{\"event\":\"init1\"}", 1700002101));
    assert(hub.postToRoom("tasks/main", identity, RoomMessageType::TASK, "{\"event\":\"init2\"}", 1700002102));

    AgentDraftQueue queue;
    auto d1 = makeReplayDraft(identity, 1700002200, "draft0", "body0", 11);
    auto d2 = makeReplayDraft(identity, 1700002201, "draft1", "body1", 12);
    std::string queueReason;
    assert(queue.enqueue(d1, &queueReason));
    assert(queue.enqueue(d2, &queueReason));

    std::ostringstream trace;
    for (int cycle = 0; cycle < 3; ++cycle) {
        AgentStorageAuditLog restartLog(auditPolicy);
        std::string openReason;
        assert(restartLog.open(root.string(), &openReason));
        auto st = restartLog.stats();
        std::string appendReason;
        assert(restartLog.append(
            static_cast<uint64_t>(1700002300 + cycle),
            "tick",
            "cycle_" + std::to_string(cycle),
            "{\"ok\":1}",
            &appendReason
        ));

        CoordinationRecoveryStats recoveryStats;
        std::string recoveryReason;
        assert(hub.verifyAndRepairIndexes(&recoveryStats, &recoveryReason));

        AgentDraftQueueRecoveryStats queueStats;
        std::string queueRepairReason;
        assert(queue.verifyAndRepairIndexes(&queueStats, &queueRepairReason));

        auto feed = hub.getObservatoryFeed(0, 32);
        trace << cycle
              << "|" << openReason
              << "|" << st.lastSequence
              << "|" << st.retainedEvents
              << "|" << st.recoveredTruncatedLines
              << "|" << recoveryReason
              << "|" << recoveryStats.roomArtifacts
              << "|" << recoveryStats.observatoryEntries
              << "|" << recoveryStats.rebuiltEntries
              << "|" << recoveryStats.rebuiltIndexEntries
              << "|" << queueRepairReason
              << "|" << queueStats.records
              << "|" << queueStats.rebuiltIndexEntries
              << "\n";
        for (const auto& entry : feed) {
            trace << synapse::crypto::toHex(entry.hash) << "|" << entry.timestamp << "\n";
        }

        assert(hub.postToRoom(
            "tasks/main",
            identity,
            RoomMessageType::TASK,
            std::string("{\"cycle\":") + std::to_string(cycle) + "}",
            static_cast<uint64_t>(1700002400 + cycle)
        ));
    }

    fs::remove_all(root, ec);
    return hashText(trace.str());
}

static std::string adversarialReplayDigest() {
    RoomConfig roomCfg;
    roomCfg.maxMessages = 3;
    AgentRoom room("adversarial-room", RoomType::TASKS, roomCfg);

    const auto identity = AgentIdentity::fromSeed(synapse::crypto::sha256("determinism_adversarial_replay_seed"));
    if (!identity.valid()) return hashText("invalid_identity");
    if (!room.post(identity, RoomMessageType::TASK, "{\"event\":\"seed\"}", 1700002500)) {
        return hashText("seed_post_failed");
    }

    auto artifacts = room.getArtifacts(0, 8);
    if (artifacts.empty()) return hashText("missing_seed_artifact");

    const bool replayRejected = !room.addVerifiedMessage(artifacts[0].message);

    RoomMessage forged = artifacts[0].message;
    forged.sequence = artifacts[0].message.sequence;
    forged.timestamp = 1700002501;
    forged.signature = synapse::crypto::sign(forged.computeHash(), identity.privateKey);
    const bool forgedRejected = !room.addVerifiedMessage(forged);

    if (!room.post(identity, RoomMessageType::TASK, "{\"event\":\"legit1\"}", 1700002502)) {
        return hashText("legit1_post_failed");
    }
    if (!room.post(identity, RoomMessageType::TASK, "{\"event\":\"legit2\"}", 1700002503)) {
        return hashText("legit2_post_failed");
    }
    if (!room.post(identity, RoomMessageType::TASK, "{\"event\":\"legit3\"}", 1700002504)) {
        return hashText("legit3_post_failed");
    }

    auto finalArtifacts = room.getArtifacts(0, 16);
    std::ostringstream trace;
    trace << replayRejected
          << "|" << forgedRejected
          << "|" << finalArtifacts.size()
          << "\n";
    for (const auto& artifact : finalArtifacts) {
        trace << synapse::crypto::toHex(artifact.hash)
              << "|" << artifact.message.sequence
              << "|" << artifact.message.timestamp
              << "\n";
    }
    return hashText(trace.str());
}

static std::string computeReplayDigest() {
    std::ostringstream combined;
    combined << schedulerTraceDigest() << "\n";
    combined << scoreTraceDigest() << "\n";
    combined << routePolicyDigest() << "\n";
    combined << searchConfigSanitizationDigest() << "\n";
    combined << updatePayloadDigest() << "\n";
    combined << recoveryProjectionReplayDigest() << "\n";
    combined << adversarialReplayDigest() << "\n";
    return hashText(combined.str());
}

int main() {
    const std::string first = computeReplayDigest();
    const std::string second = computeReplayDigest();
    assert(first == second);
    std::cout << first << "\n";
    return 0;
}
