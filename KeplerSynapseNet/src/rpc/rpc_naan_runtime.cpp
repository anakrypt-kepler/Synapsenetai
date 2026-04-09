#include "rpc/rpc_naan_runtime.h"

#include <algorithm>
#include <ctime>
#include <stdexcept>

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

namespace synapse::rpc {

using json = nlohmann::json;

namespace {

constexpr uint32_t kDraftScanLimit = 1000000;

json parseRpcParams(const std::string& paramsJson) {
    if (paramsJson.empty()) {
        return json::object();
    }
    json parsed = json::parse(paramsJson, nullptr, false);
    if (parsed.is_discarded()) {
        throw std::runtime_error("Invalid JSON params");
    }
    if (parsed.is_array()) {
        if (parsed.empty()) {
            return json::object();
        }
        if (!parsed.front().is_object()) {
            throw std::runtime_error("Expected object params");
        }
        return parsed.front();
    }
    if (!parsed.is_object()) {
        throw std::runtime_error("Expected object params");
    }
    return parsed;
}

crypto::Hash256 parseHash256Hex(const std::string& hex) {
    crypto::Hash256 out{};
    const auto bytes = crypto::fromHex(hex);
    if (bytes.size() != out.size()) {
        throw std::runtime_error("Expected 32-byte hex string");
    }
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return out;
}

uint32_t readBoundedLimit(const json& params, uint32_t defaultValue) {
    uint32_t limit = defaultValue;
    if (params.contains("limit")) {
        int64_t raw = params.value("limit", static_cast<int64_t>(defaultValue));
        if (raw < 1) raw = 1;
        if (raw > 500) raw = 500;
        limit = static_cast<uint32_t>(raw);
    }
    return limit;
}

void requireCallback(bool available, const char* name) {
    if (!available) {
        throw std::runtime_error(name);
    }
}

RpcNaanPipelineSummary buildPipelineSummaryOrThrow(const RpcNaanRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.buildPipelineSummary),
        "invalid_rpc_naan_runtime_inputs");
    return inputs.buildPipelineSummary();
}

void appendDrafts(
    std::vector<core::AgentDraftRecord>* out,
    const RpcNaanRuntimeInputs& inputs,
    core::DraftStatus status) {
    requireCallback(
        static_cast<bool>(inputs.listDraftsByStatus),
        "invalid_rpc_naan_runtime_inputs");
    auto records = inputs.listDraftsByStatus(status, kDraftScanLimit);
    out->insert(out->end(), records.begin(), records.end());
}

void sortDraftRecords(std::vector<core::AgentDraftRecord>* records) {
    std::sort(
        records->begin(),
        records->end(),
        [](const core::AgentDraftRecord& a, const core::AgentDraftRecord& b) {
            const std::string ah = crypto::toHex(a.proposal.draftId());
            const std::string bh = crypto::toHex(b.proposal.draftId());
            if (ah != bh) {
                return ah < bh;
            }
            return a.proposal.createdAt < b.proposal.createdAt;
        });
}

template <typename T>
T loadAtomic(const std::atomic<T>* value, T fallback = T{}) {
    return value ? value->load() : fallback;
}

void validateNaanStatusInputs(const RpcNaanStatusRuntimeInputs& inputs) {
    if (inputs.agentIdHex.empty() ||
        !inputs.getRuntimeState ||
        !inputs.getCrashState ||
        !inputs.getSchedulerSnapshot ||
        !inputs.listRooms ||
        !inputs.totalMessages ||
        !inputs.draftQueueSize ||
        !inputs.countDraftsByStatus ||
        !inputs.getScore ||
        !inputs.getAdaptiveSchedule ||
        !inputs.getScoreDecayIntervalSec ||
        !inputs.getAbuseSpamPenalty ||
        !inputs.getAbuseCitationPenalty ||
        !inputs.getAbusePolicyPenalty ||
        !inputs.getPolicyHashes ||
        !inputs.getStorageInputs ||
        !inputs.getConnectorAutoQuarantineLastViolations ||
        !inputs.getConnectorStatus ||
        !inputs.getRewardVisibility) {
        throw std::runtime_error("invalid_rpc_naan_runtime_inputs");
    }
}

std::pair<std::string, uint64_t> buildQuarantineReason(
    const core::AgentScoreSnapshot& score,
    uint64_t connectorAutoQuarantineLastAt) {
    if (!score.quarantined) {
        return {"none", 0};
    }
    if (connectorAutoQuarantineLastAt > 0) {
        return {"connector_auto_quarantine", connectorAutoQuarantineLastAt};
    }
    if (score.localDraftOnly) {
        return {"score_policy_local_draft_only", 0};
    }
    if (score.reviewOnly) {
        return {"score_policy_review_only", 0};
    }
    if (score.throttled) {
        return {"score_policy_throttled", 0};
    }
    return {"score_policy", 0};
}

} // namespace

std::string runRpcNaanStatus(const RpcNaanStatusRuntimeInputs& inputs) {
    validateNaanStatusInputs(inputs);

    RpcNaanStatusInputs response;
    const auto score = inputs.getScore();
    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    const bool runtimeInitialized = loadAtomic(inputs.runtimeInitialized, false);
    const auto runtimeState = runtimeInitialized
        ? inputs.getRuntimeState(now)
        : core::AgentRuntimeFailoverState::RECOVERY;
    const auto crashState = inputs.getCrashState();
    const auto schedulerState = inputs.getSchedulerSnapshot();
    const auto connectorAutoQuarantineLastAt =
        loadAtomic(inputs.connectorAutoQuarantineLastAt, uint64_t{0});
    const auto quarantine = buildQuarantineReason(score, connectorAutoQuarantineLastAt);
    const auto schedule =
        inputs.getAdaptiveSchedule(score.throttled, score.quarantined);
    const auto policyHashes = inputs.getPolicyHashes();

    response.agentId = inputs.agentIdHex;
    response.runtimeState = core::failoverStateToString(runtimeState);
    response.runtimeInitialized = runtimeInitialized;
    response.runtimeStartedAt = loadAtomic(inputs.runtimeStartedAt, uint64_t{0});
    response.runtimeCrashCount = crashState.totalCrashes;
    response.runtimeConsecutiveCrashes = crashState.consecutiveCrashes;
    response.runtimeLastCrashAt = crashState.lastCrashTimestamp;
    response.runtimeRecoveryUntil = crashState.recoveryUntilTimestamp;
    response.runtimeRecoverySkips =
        loadAtomic(inputs.runtimeRecoverySkips, uint64_t{0});
    response.schedulerTick = schedulerState.tick;
    response.schedulerEpochStartTick = schedulerState.epochStartTick;
    response.schedulerEpochIndex = schedulerState.epochIndex;
    response.schedulerRoundRobinCursor = schedulerState.roundRobinCursor;
    response.schedulerBudgetRemaining.cpu = schedulerState.remaining.cpu;
    response.schedulerBudgetRemaining.ram = schedulerState.remaining.ram;
    response.schedulerBudgetRemaining.network = schedulerState.remaining.network;
    response.humanWritable = false;
    response.rooms.tasks = inputs.listRooms(core::RoomType::TASKS);
    response.rooms.reviews = inputs.listRooms(core::RoomType::REVIEWS);
    response.rooms.disputes = inputs.listRooms(core::RoomType::DISPUTES);
    response.rooms.alerts = inputs.listRooms(core::RoomType::ALERTS);
    response.totalMessages = inputs.totalMessages();
    response.draftQueueSize = inputs.draftQueueSize();
    response.queuedDrafts = inputs.countDraftsByStatus(core::DraftStatus::QUEUED);
    response.reviewDrafts =
        inputs.countDraftsByStatus(core::DraftStatus::REVIEW_REQUIRED);
    response.approvedDrafts =
        inputs.countDraftsByStatus(core::DraftStatus::APPROVED);
    response.rejectedDrafts =
        inputs.countDraftsByStatus(core::DraftStatus::REJECTED);
    response.submittedDrafts =
        inputs.countDraftsByStatus(core::DraftStatus::SUBMITTED);
    response.pipelineRuns = loadAtomic(inputs.pipelineRuns, uint64_t{0});
    response.pipelineApproved = loadAtomic(inputs.pipelineApproved, uint64_t{0});
    response.pipelineSubmitted = loadAtomic(inputs.pipelineSubmitted, uint64_t{0});
    response.pipelineRejected = loadAtomic(inputs.pipelineRejected, uint64_t{0});
    response.lastPipelineAt = loadAtomic(inputs.lastPipelineAt, uint64_t{0});
    response.agentScore = score.score;
    response.agentScoreBand = core::agentScoreBandToString(score.band);
    response.agentThrottled = score.throttled;
    response.agentReviewOnly = score.reviewOnly;
    response.agentLocalDraftOnly = score.localDraftOnly;
    response.agentQuarantined = score.quarantined;
    response.agentQuarantineReason = quarantine.first;
    response.agentQuarantineReasonSince = quarantine.second;
    response.pipelineBatchLimit = score.batchLimit;
    response.pipelineConfig = inputs.pipelineConfig;
    response.schedulerState = core::schedulingStateToString(schedule.state);
    response.draftIntervalSec = schedule.draftIntervalSeconds;
    response.pipelineIntervalSec = schedule.pipelineIntervalSeconds;
    response.heartbeatIntervalSec = schedule.heartbeatIntervalSeconds;
    response.agentScoreSteps = score.steps;
    response.agentScoreCleanSteps = score.cleanSteps;
    response.agentAcceptedTotal = score.acceptedTotal;
    response.agentRejectedTotal = score.rejectedTotal;
    response.agentViolationsTotal = score.violationsTotal;
    response.agentScoreDecayIntervalSec = inputs.getScoreDecayIntervalSec();
    response.agentScoreLastDecayAt =
        loadAtomic(inputs.scoreLastDecayAt, uint64_t{0});
    response.agentScoreLastViolationTick =
        loadAtomic(inputs.scoreLastViolationTick, uint64_t{0});
    response.agentScoreBandTransitions =
        loadAtomic(inputs.scoreBandTransitions, uint64_t{0});
    response.agentQuarantineRecoveries =
        loadAtomic(inputs.quarantineRecoveries, uint64_t{0});
    response.agentQuarantineRecoveryLastAt =
        loadAtomic(inputs.quarantineRecoveryLastAt, uint64_t{0});
    response.agentAbuseSpamPenalty = inputs.getAbuseSpamPenalty();
    response.agentAbuseCitationPenalty = inputs.getAbuseCitationPenalty();
    response.agentAbusePolicyPenalty = inputs.getAbusePolicyPenalty();
    response.policyHashNaan = policyHashes.first;
    response.policyHashImplant = policyHashes.second;
    response.securityEvents = loadAtomic(inputs.securityEvents, uint64_t{0});
    response.securityHighSeverityEvents =
        loadAtomic(inputs.securityHighSeverityEvents, uint64_t{0});
    response.securityLastEventAt =
        loadAtomic(inputs.securityLastEventAt, uint64_t{0});
    response.storage = inputs.getStorageInputs();
    response.connectorAutoQuarantineEvents =
        loadAtomic(inputs.connectorAutoQuarantineEvents, uint64_t{0});
    response.connectorAutoQuarantineLastAt = connectorAutoQuarantineLastAt;
    response.connectorAutoQuarantineLastPolicyDelta =
        loadAtomic(inputs.connectorAutoQuarantineLastPolicyDelta, uint64_t{0});
    response.connectorAutoQuarantineLastFailureDelta =
        loadAtomic(inputs.connectorAutoQuarantineLastFailureDelta, uint64_t{0});
    response.connectorAutoQuarantineLastViolations =
        inputs.getConnectorAutoQuarantineLastViolations();
    response.taskRunsResearch =
        loadAtomic(inputs.taskRunsResearch, uint64_t{0});
    response.taskRunsVerify = loadAtomic(inputs.taskRunsVerify, uint64_t{0});
    response.taskRunsReview = loadAtomic(inputs.taskRunsReview, uint64_t{0});
    response.taskRunsDraft = loadAtomic(inputs.taskRunsDraft, uint64_t{0});
    response.taskRunsSubmit = loadAtomic(inputs.taskRunsSubmit, uint64_t{0});
    response.lastResearchAt = loadAtomic(inputs.lastResearchAt, uint64_t{0});
    response.lastVerifyAt = loadAtomic(inputs.lastVerifyAt, uint64_t{0});
    response.lastReviewAt = loadAtomic(inputs.lastReviewAt, uint64_t{0});
    response.lastActionAt = loadAtomic(inputs.lastActionAt, uint64_t{0});
    response.lastHeartbeatAt = loadAtomic(inputs.lastHeartbeatAt, uint64_t{0});
    response.lastDraftAt = loadAtomic(inputs.lastDraftAt, uint64_t{0});
    response.ticks = loadAtomic(inputs.ticks, uint64_t{0});
    response.connector = inputs.getConnectorStatus();
    response.webFailClosedSkips =
        loadAtomic(inputs.webFailClosedSkips, uint64_t{0});
    response.miningFailClosedSkips =
        loadAtomic(inputs.miningFailClosedSkips, uint64_t{0});
    response.tor = inputs.tor;
    response.networkHealth = inputs.networkHealth;
    response.rewardVisibility = inputs.getRewardVisibility();
    response.redactionCount = loadAtomic(inputs.redactionCount, uint64_t{0});
    return buildRpcNaanStatusResponse(response);
}

std::string runRpcNaanObservatoryArtifacts(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.getObservatoryFeed),
        "invalid_rpc_naan_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    uint64_t sinceTimestamp = 0;
    if (params.contains("sinceTimestamp")) {
        sinceTimestamp = static_cast<uint64_t>(
            std::max<int64_t>(0, params.value("sinceTimestamp", 0)));
    } else if (params.contains("since")) {
        sinceTimestamp = static_cast<uint64_t>(
            std::max<int64_t>(0, params.value("since", 0)));
    }

    const auto feed = inputs.getObservatoryFeed(
        sinceTimestamp,
        readBoundedLimit(params, 100));
    return buildRpcNaanObservatoryArtifactsResponse(feed, inputs.redact);
}

std::string runRpcNaanObservatoryArtifactGet(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.getArtifact),
        "invalid_rpc_naan_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    std::string hashHex = params.value("hash", "");
    if (hashHex.empty()) {
        hashHex = params.value("id", "");
    }
    if (hashHex.empty()) {
        throw std::runtime_error("hash required");
    }

    const auto artifact = inputs.getArtifact(parseHash256Hex(hashHex));
    if (!artifact) {
        throw std::runtime_error("not_found");
    }
    return buildRpcNaanObservatoryArtifactResponse(*artifact, true, inputs.redact);
}

std::string runRpcNaanObservatoryDrafts(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs) {
    const auto params = parseRpcParams(paramsJson);
    const uint32_t limit = readBoundedLimit(params, 100);

    bool includeRejected = true;
    if (params.contains("includeRejected")) {
        includeRejected = params.value("includeRejected", true);
    }

    std::vector<core::AgentDraftRecord> records;
    if (params.contains("status") && params["status"].is_string()) {
        requireCallback(
            static_cast<bool>(inputs.listDraftsByStatus),
            "invalid_rpc_naan_runtime_inputs");
        const std::string status = params["status"].get<std::string>();
        records = inputs.listDraftsByStatus(core::stringToDraftStatus(status), limit);
        if (!includeRejected && status == "rejected") {
            records.clear();
        }
    } else {
        requireCallback(
            static_cast<bool>(inputs.listDrafts),
            "invalid_rpc_naan_runtime_inputs");
        records = inputs.listDrafts(limit, includeRejected);
    }

    return buildRpcNaanDraftsResponse(records, false, inputs.redact);
}

std::string runRpcNaanObservatoryDraftGet(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.getDraft),
        "invalid_rpc_naan_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    std::string draftIdHex = params.value("draftId", "");
    if (draftIdHex.empty()) {
        draftIdHex = params.value("id", "");
    }
    if (draftIdHex.empty()) {
        throw std::runtime_error("draftId required");
    }

    const auto record = inputs.getDraft(parseHash256Hex(draftIdHex));
    if (!record) {
        throw std::runtime_error("not_found");
    }
    return buildRpcNaanDraftResponse(*record, true, inputs.redact);
}

std::string runRpcNaanPipelineDryRun(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs) {
    if (!inputs.runtimeInitialized) {
        throw std::runtime_error("naan_runtime_uninitialized");
    }
    if (!inputs.poeAvailable) {
        throw std::runtime_error("poe_unavailable");
    }
    if (!inputs.agentIdentityAvailable) {
        throw std::runtime_error("agent_identity_unavailable");
    }
    requireCallback(
        static_cast<bool>(inputs.dryRunDraft),
        "invalid_rpc_naan_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    const uint32_t limit = readBoundedLimit(params, 64);

    std::vector<core::AgentDraftRecord> records;
    records.reserve(256);
    appendDrafts(&records, inputs, core::DraftStatus::QUEUED);
    appendDrafts(&records, inputs, core::DraftStatus::REVIEW_REQUIRED);
    appendDrafts(&records, inputs, core::DraftStatus::APPROVED);
    sortDraftRecords(&records);

    RpcNaanPipelineDryRunInputs response;
    for (const auto& record : records) {
        if (response.items.size() >= limit) {
            break;
        }
        response.items.push_back(inputs.dryRunDraft(record));
    }

    const auto summary = buildPipelineSummaryOrThrow(inputs);
    response.agentScore = summary.agentScore;
    response.agentThrottled = summary.agentThrottled;
    response.agentQuarantined = summary.agentQuarantined;
    response.pipelineBatchLimit = summary.pipelineBatchLimit;
    response.schedulerState = summary.schedulerState;
    response.draftIntervalSec = summary.draftIntervalSec;
    response.pipelineIntervalSec = summary.pipelineIntervalSec;
    response.heartbeatIntervalSec = summary.heartbeatIntervalSec;
    return buildRpcNaanPipelineDryRunResponse(response, inputs.redact);
}

std::string runRpcNaanPipelineDrain(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs) {
    if (!inputs.runtimeInitialized) {
        throw std::runtime_error("naan_runtime_uninitialized");
    }
    if (!inputs.poeAvailable) {
        throw std::runtime_error("poe_unavailable");
    }
    if (!inputs.agentIdentityAvailable) {
        throw std::runtime_error("agent_identity_unavailable");
    }
    requireCallback(
        static_cast<bool>(inputs.runPipelineDrain),
        "invalid_rpc_naan_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    uint32_t limit = 0;
    if (params.contains("limit")) {
        int64_t raw = params.value("limit", 0);
        if (raw < 1) raw = 1;
        if (raw > 500) raw = 500;
        limit = static_cast<uint32_t>(raw);
    }

    uint64_t atTimestamp = static_cast<uint64_t>(std::time(nullptr));
    if (params.contains("atTimestamp")) {
        int64_t rawTs = params.value("atTimestamp", static_cast<int64_t>(atTimestamp));
        if (rawTs > 0) {
            atTimestamp = static_cast<uint64_t>(rawTs);
        }
    }

    bool skippedQuarantine = false;
    uint32_t effectiveLimit = 0;
    auto batch = inputs.runPipelineDrain(
        atTimestamp,
        limit,
        &skippedQuarantine,
        &effectiveLimit);

    const auto summary = buildPipelineSummaryOrThrow(inputs);
    RpcNaanPipelineDrainInputs response;
    response.items = std::move(batch);
    response.requestedLimitSet = limit != 0;
    response.requestedLimit = limit;
    response.effectiveLimit = effectiveLimit;
    response.skippedQuarantine = skippedQuarantine;
    response.pipelineRuns = summary.pipelineRuns;
    response.pipelineApproved = summary.pipelineApproved;
    response.pipelineSubmitted = summary.pipelineSubmitted;
    response.pipelineRejected = summary.pipelineRejected;
    response.lastPipelineAt = summary.lastPipelineAt;
    response.agentScore = summary.agentScore;
    response.agentThrottled = summary.agentThrottled;
    response.agentQuarantined = summary.agentQuarantined;
    response.pipelineBatchLimit = summary.pipelineBatchLimit;
    response.schedulerState = summary.schedulerState;
    response.draftIntervalSec = summary.draftIntervalSec;
    response.pipelineIntervalSec = summary.pipelineIntervalSec;
    response.heartbeatIntervalSec = summary.heartbeatIntervalSec;
    return buildRpcNaanPipelineDrainResponse(response, inputs.redact);
}

} // namespace synapse::rpc
