#pragma once

#include "core/agent_coordination.h"
#include "core/agent_draft_queue.h"
#include "core/agent_submission_pipeline.h"
#include "rpc/rpc_node_views.h"
#include "web/web.h"

#include <functional>
#include <string>
#include <vector>

namespace synapse::rpc {

using NaanRedactFn = std::function<std::string(const std::string&)>;

std::string buildRpcNaanObservatoryArtifactsResponse(
    const std::vector<core::ObservatoryEntry>& entries,
    const NaanRedactFn& redact);

std::string buildRpcNaanObservatoryArtifactResponse(
    const core::SignedArtifact& artifact,
    bool includePayload,
    const NaanRedactFn& redact);

std::string buildRpcNaanDraftsResponse(
    const std::vector<core::AgentDraftRecord>& records,
    bool includeBody,
    const NaanRedactFn& redact);

std::string buildRpcNaanDraftResponse(
    const core::AgentDraftRecord& record,
    bool includeBody,
    const NaanRedactFn& redact);

struct RpcNaanPipelineDryRunInputs {
    std::vector<core::AgentSubmissionDryRunResult> items;
    int64_t agentScore = 0;
    bool agentThrottled = false;
    bool agentQuarantined = false;
    uint32_t pipelineBatchLimit = 0;
    std::string schedulerState;
    uint32_t draftIntervalSec = 0;
    uint32_t pipelineIntervalSec = 0;
    uint32_t heartbeatIntervalSec = 0;
};

std::string buildRpcNaanPipelineDryRunResponse(
    const RpcNaanPipelineDryRunInputs& inputs,
    const NaanRedactFn& redact);

struct RpcNaanPipelineDrainInputs {
    std::vector<core::AgentSubmissionBatchItem> items;
    bool requestedLimitSet = false;
    uint32_t requestedLimit = 0;
    uint32_t effectiveLimit = 0;
    bool skippedQuarantine = false;
    uint64_t pipelineRuns = 0;
    uint64_t pipelineApproved = 0;
    uint64_t pipelineSubmitted = 0;
    uint64_t pipelineRejected = 0;
    uint64_t lastPipelineAt = 0;
    int64_t agentScore = 0;
    bool agentThrottled = false;
    bool agentQuarantined = false;
    uint32_t pipelineBatchLimit = 0;
    std::string schedulerState;
    uint32_t draftIntervalSec = 0;
    uint32_t pipelineIntervalSec = 0;
    uint32_t heartbeatIntervalSec = 0;
};

std::string buildRpcNaanPipelineDrainResponse(
    const RpcNaanPipelineDrainInputs& inputs,
    const NaanRedactFn& redact);

struct RpcNaanRoomsInputs {
    std::vector<std::string> tasks;
    std::vector<std::string> reviews;
    std::vector<std::string> disputes;
    std::vector<std::string> alerts;
};

struct RpcNaanSchedulerBudgetInputs {
    uint32_t cpu = 0;
    uint32_t ram = 0;
    uint32_t network = 0;
};

struct RpcNaanStorageInputs {
    std::string root;
    uint64_t auditSegments = 0;
    uint64_t auditRetainedEvents = 0;
    uint64_t auditLastSeq = 0;
    std::string auditLastHash;
    uint64_t auditRecoveredLines = 0;
    uint64_t auditDroppedSegments = 0;
    uint64_t indexRecoveryRuns = 0;
    uint64_t indexRecoveryLastAt = 0;
    uint64_t consistencyChecks = 0;
    uint64_t consistencyRepairs = 0;
    uint64_t consistencyLastAt = 0;
};

struct RpcNaanWebConfigValidationInputs {
    uint64_t totalLines = 0;
    uint64_t appliedLines = 0;
    uint64_t invalidLines = 0;
    uint64_t unknownKeys = 0;
    uint64_t sanitizedWrites = 0;
    std::vector<std::string> unknownKeySamples;
};

struct RpcNaanConnectorStatusInputs {
    bool available = false;
    web::WebConnectorHealth health{};
    web::SearchConfig config{};
    web::WebStats stats{};
    RpcNaanWebConfigValidationInputs validation;
    std::string sitePolicyPath;
};

struct RpcNaanRewardVisibilityInputs {
    bool deterministic = true;
    uint64_t observedSubmissions = 0;
    uint64_t finalizedSubmissions = 0;
    uint64_t creditedSubmissions = 0;
    uint64_t expectedAtoms = 0;
    uint64_t creditedAtoms = 0;
};

struct RpcNaanStatusInputs {
    std::string agentId;
    std::string runtimeState;
    bool runtimeInitialized = false;
    uint64_t runtimeStartedAt = 0;
    uint64_t runtimeCrashCount = 0;
    uint64_t runtimeConsecutiveCrashes = 0;
    uint64_t runtimeLastCrashAt = 0;
    uint64_t runtimeRecoveryUntil = 0;
    uint64_t runtimeRecoverySkips = 0;
    uint64_t schedulerTick = 0;
    uint64_t schedulerEpochStartTick = 0;
    uint64_t schedulerEpochIndex = 0;
    uint32_t schedulerRoundRobinCursor = 0;
    RpcNaanSchedulerBudgetInputs schedulerBudgetRemaining;
    bool humanWritable = false;
    RpcNaanRoomsInputs rooms;
    uint64_t totalMessages = 0;
    uint64_t draftQueueSize = 0;
    uint64_t queuedDrafts = 0;
    uint64_t reviewDrafts = 0;
    uint64_t approvedDrafts = 0;
    uint64_t rejectedDrafts = 0;
    uint64_t submittedDrafts = 0;
    uint64_t pipelineRuns = 0;
    uint64_t pipelineApproved = 0;
    uint64_t pipelineSubmitted = 0;
    uint64_t pipelineRejected = 0;
    uint64_t lastPipelineAt = 0;
    int64_t agentScore = 0;
    std::string agentScoreBand;
    bool agentThrottled = false;
    bool agentReviewOnly = false;
    bool agentLocalDraftOnly = false;
    bool agentQuarantined = false;
    std::string agentQuarantineReason = "none";
    uint64_t agentQuarantineReasonSince = 0;
    uint32_t pipelineBatchLimit = 0;
    core::AgentSubmissionPipelineConfig pipelineConfig{};
    std::string schedulerState;
    uint32_t draftIntervalSec = 0;
    uint32_t pipelineIntervalSec = 0;
    uint32_t heartbeatIntervalSec = 0;
    uint64_t agentScoreSteps = 0;
    uint64_t agentScoreCleanSteps = 0;
    uint64_t agentAcceptedTotal = 0;
    uint64_t agentRejectedTotal = 0;
    uint64_t agentViolationsTotal = 0;
    uint64_t agentScoreDecayIntervalSec = 0;
    uint64_t agentScoreLastDecayAt = 0;
    uint64_t agentScoreLastViolationTick = 0;
    uint64_t agentScoreBandTransitions = 0;
    uint64_t agentQuarantineRecoveries = 0;
    uint64_t agentQuarantineRecoveryLastAt = 0;
    uint64_t agentAbuseSpamPenalty = 0;
    uint64_t agentAbuseCitationPenalty = 0;
    uint64_t agentAbusePolicyPenalty = 0;
    std::string policyHashNaan;
    std::string policyHashImplant;
    uint64_t securityEvents = 0;
    uint64_t securityHighSeverityEvents = 0;
    uint64_t securityLastEventAt = 0;
    RpcNaanStorageInputs storage;
    uint64_t connectorAutoQuarantineEvents = 0;
    uint64_t connectorAutoQuarantineLastAt = 0;
    uint64_t connectorAutoQuarantineLastPolicyDelta = 0;
    uint64_t connectorAutoQuarantineLastFailureDelta = 0;
    uint64_t connectorAutoQuarantineLastViolations = 0;
    uint64_t taskRunsResearch = 0;
    uint64_t taskRunsVerify = 0;
    uint64_t taskRunsReview = 0;
    uint64_t taskRunsDraft = 0;
    uint64_t taskRunsSubmit = 0;
    uint64_t lastResearchAt = 0;
    uint64_t lastVerifyAt = 0;
    uint64_t lastReviewAt = 0;
    uint64_t lastActionAt = 0;
    uint64_t lastHeartbeatAt = 0;
    uint64_t lastDraftAt = 0;
    uint64_t ticks = 0;
    RpcNaanConnectorStatusInputs connector;
    uint64_t webFailClosedSkips = 0;
    uint64_t miningFailClosedSkips = 0;
    RpcNodeTorStatusInputs tor;
    RpcNodeNetworkHealthInputs networkHealth;
    RpcNaanRewardVisibilityInputs rewardVisibility;
    uint64_t redactionCount = 0;
};

std::string buildRpcNaanStatusResponse(const RpcNaanStatusInputs& inputs);

} // namespace synapse::rpc
