#pragma once

#include "core/agent_submission_pipeline.h"
#include "core/agent_runtime.h"
#include "core/agent_scheduler.h"
#include "core/agent_score.h"
#include "crypto/crypto.h"
#include "rpc/rpc_naan_views.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace synapse::rpc {

struct RpcNaanPipelineSummary {
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

struct RpcNaanRuntimeInputs {
    bool runtimeInitialized = false;
    bool poeAvailable = false;
    bool agentIdentityAvailable = false;
    NaanRedactFn redact;
    std::function<std::vector<core::ObservatoryEntry>(uint64_t, uint32_t)>
        getObservatoryFeed;
    std::function<std::optional<core::SignedArtifact>(const crypto::Hash256&)>
        getArtifact;
    std::function<std::vector<core::AgentDraftRecord>(uint32_t, bool)> listDrafts;
    std::function<std::vector<core::AgentDraftRecord>(core::DraftStatus, uint32_t)>
        listDraftsByStatus;
    std::function<std::optional<core::AgentDraftRecord>(const crypto::Hash256&)>
        getDraft;
    std::function<core::AgentSubmissionDryRunResult(const core::AgentDraftRecord&)>
        dryRunDraft;
    std::function<std::vector<core::AgentSubmissionBatchItem>(
        uint64_t,
        uint32_t,
        bool*,
        uint32_t*)> runPipelineDrain;
    std::function<RpcNaanPipelineSummary()> buildPipelineSummary;
};

struct RpcNaanStatusRuntimeInputs {
    std::string agentIdHex;
    std::atomic<bool>* runtimeInitialized = nullptr;
    std::atomic<uint64_t>* runtimeStartedAt = nullptr;
    std::function<core::AgentRuntimeFailoverState(uint64_t)> getRuntimeState;
    std::function<core::AgentRuntimeCrashState()> getCrashState;
    std::atomic<uint64_t>* runtimeRecoverySkips = nullptr;
    std::function<core::AgentTaskSchedulerSnapshot()> getSchedulerSnapshot;
    std::function<std::vector<std::string>(core::RoomType)> listRooms;
    std::function<uint64_t()> totalMessages;
    std::function<uint64_t()> draftQueueSize;
    std::function<uint64_t(core::DraftStatus)> countDraftsByStatus;
    std::atomic<uint64_t>* pipelineRuns = nullptr;
    std::atomic<uint64_t>* pipelineApproved = nullptr;
    std::atomic<uint64_t>* pipelineSubmitted = nullptr;
    std::atomic<uint64_t>* pipelineRejected = nullptr;
    std::atomic<uint64_t>* lastPipelineAt = nullptr;
    std::function<core::AgentScoreSnapshot()> getScore;
    core::AgentSubmissionPipelineConfig pipelineConfig{};
    std::function<core::AgentAdaptiveSchedule(bool, bool)> getAdaptiveSchedule;
    std::function<uint64_t()> getScoreDecayIntervalSec;
    std::atomic<uint64_t>* scoreLastDecayAt = nullptr;
    std::atomic<uint64_t>* scoreLastViolationTick = nullptr;
    std::atomic<uint64_t>* scoreBandTransitions = nullptr;
    std::atomic<uint64_t>* quarantineRecoveries = nullptr;
    std::atomic<uint64_t>* quarantineRecoveryLastAt = nullptr;
    std::function<uint64_t()> getAbuseSpamPenalty;
    std::function<uint64_t()> getAbuseCitationPenalty;
    std::function<uint64_t()> getAbusePolicyPenalty;
    std::function<std::pair<std::string, std::string>()> getPolicyHashes;
    std::atomic<uint64_t>* securityEvents = nullptr;
    std::atomic<uint64_t>* securityHighSeverityEvents = nullptr;
    std::atomic<uint64_t>* securityLastEventAt = nullptr;
    std::function<RpcNaanStorageInputs()> getStorageInputs;
    std::atomic<uint64_t>* connectorAutoQuarantineEvents = nullptr;
    std::atomic<uint64_t>* connectorAutoQuarantineLastAt = nullptr;
    std::atomic<uint64_t>* connectorAutoQuarantineLastPolicyDelta = nullptr;
    std::atomic<uint64_t>* connectorAutoQuarantineLastFailureDelta = nullptr;
    std::function<uint64_t()> getConnectorAutoQuarantineLastViolations;
    std::atomic<uint64_t>* taskRunsResearch = nullptr;
    std::atomic<uint64_t>* taskRunsVerify = nullptr;
    std::atomic<uint64_t>* taskRunsReview = nullptr;
    std::atomic<uint64_t>* taskRunsDraft = nullptr;
    std::atomic<uint64_t>* taskRunsSubmit = nullptr;
    std::atomic<uint64_t>* lastResearchAt = nullptr;
    std::atomic<uint64_t>* lastVerifyAt = nullptr;
    std::atomic<uint64_t>* lastReviewAt = nullptr;
    std::atomic<uint64_t>* lastActionAt = nullptr;
    std::atomic<uint64_t>* lastHeartbeatAt = nullptr;
    std::atomic<uint64_t>* lastDraftAt = nullptr;
    std::atomic<uint64_t>* ticks = nullptr;
    std::function<RpcNaanConnectorStatusInputs()> getConnectorStatus;
    std::atomic<uint64_t>* webFailClosedSkips = nullptr;
    std::atomic<uint64_t>* miningFailClosedSkips = nullptr;
    RpcNodeTorStatusInputs tor;
    RpcNodeNetworkHealthInputs networkHealth;
    std::function<RpcNaanRewardVisibilityInputs()> getRewardVisibility;
    std::atomic<uint64_t>* redactionCount = nullptr;
};

std::string runRpcNaanStatus(const RpcNaanStatusRuntimeInputs& inputs);

std::string runRpcNaanObservatoryArtifacts(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs);
std::string runRpcNaanObservatoryArtifactGet(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs);
std::string runRpcNaanObservatoryDrafts(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs);
std::string runRpcNaanObservatoryDraftGet(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs);
std::string runRpcNaanPipelineDryRun(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs);
std::string runRpcNaanPipelineDrain(
    const std::string& paramsJson,
    const RpcNaanRuntimeInputs& inputs);

} // namespace synapse::rpc
