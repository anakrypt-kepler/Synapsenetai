#pragma once

#include "core/agent_coordination.h"
#include "core/agent_draft_queue.h"
#include "core/agent_runtime.h"
#include "core/agent_scheduler.h"
#include "core/agent_score.h"
#include "core/agent_storage.h"
#include "core/knowledge.h"
#include "core/ledger.h"
#include "core/poe_v1_engine.h"
#include "core/tor_peer_identity.h"
#include "core/tor_route_policy.h"
#include "core/transfer.h"
#include "model/model_loader.h"
#include "network/discovery.h"
#include "network/network.h"
#include "tui/tui_runtime.h"
#include "web/web.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace synapse::tui {

struct TuiPoeBootstrapInfo {
    uint64_t validatorCount = 0;
    bool selfBootstrapActive = false;
    std::string mode = "disabled";
};

struct TuiCoreSnapshotBuilderHooks {
    std::function<size_t()> peerCount;
    std::function<uint64_t()> knowledgeEntries;
    std::function<uint64_t()> knowledgeFinalized;
    std::function<std::optional<network::DiscoveryStats>()> discoveryStats;
    std::function<size_t()> bootstrapNodeCount;
    std::function<size_t()> dnsSeedCount;
    std::function<TuiPoeBootstrapInfo()> poeBootstrapInfo;
    std::function<double()> syncProgress;
    std::function<uint64_t()> lastReward;
    std::function<uint64_t()> lastRewardEpochId;
    std::function<uint64_t()> lastRewardEntries;
    std::function<uint64_t()> lastRewardAt;
    std::function<std::vector<network::Peer>()> peers;
    std::function<core::PeerDisplayInfo(const network::Peer&)> peerDisplayInfo;
    std::function<std::vector<model::ModelInfo>(const std::string&)> listModels;
    std::string modelsDirectory;
    std::function<bool()> walletReady;
    std::function<std::string()> walletAddress;
    std::function<uint64_t()> walletBalance;
    std::function<uint64_t()> walletPending;
    std::function<uint64_t()> blockHeight;
    std::function<bool()> quantumEnabled;
    std::function<bool()> liboqsEnabled;
    std::function<std::string()> quantumCapabilityMode;
    std::function<bool()> kyberReal;
    std::function<bool()> dilithiumReal;
    std::function<bool()> sphincsReal;
    std::function<std::string()> kyberImplementation;
    std::function<std::string()> dilithiumImplementation;
    std::function<std::string()> sphincsImplementation;
};

TuiCoreSnapshot buildTuiCoreSnapshot(const TuiCoreSnapshotBuilderHooks& hooks);

struct TuiCoreSnapshotCollectorInputs {
    const network::Network* network = nullptr;
    const core::KnowledgeNetwork* knowledge = nullptr;
    const core::PoeV1Engine* poe = nullptr;
    const network::Discovery* discovery = nullptr;
    TuiPoeBootstrapInfo poeBootstrapInfo{};
    double syncProgress = 0.0;
    uint64_t lastReward = 0;
    uint64_t lastRewardEpochId = 0;
    uint64_t lastRewardEntries = 0;
    uint64_t lastRewardAt = 0;
    std::function<core::PeerDisplayInfo(const network::Peer&)> peerDisplayInfo;
    const model::ModelLoader* modelLoader = nullptr;
    std::string modelsDirectory;
    bool walletReady = false;
    std::string walletAddress;
    uint64_t walletBalance = 0;
    uint64_t walletPending = 0;
    uint64_t blockHeight = 0;
    bool quantumEnabled = false;
    bool liboqsEnabled = false;
    std::string quantumCapabilityMode;
    bool kyberReal = false;
    bool dilithiumReal = false;
    bool sphincsReal = false;
    std::string kyberImplementation;
    std::string dilithiumImplementation;
    std::string sphincsImplementation;
};

TuiCoreSnapshot collectTuiCoreSnapshot(const TuiCoreSnapshotCollectorInputs& inputs);

struct TuiKnowledgeSnapshotBuilderHooks {
    std::function<std::vector<crypto::Hash256>()> listEntryIds;
    std::function<core::PoeV1Config()> poeConfig;
    std::function<uint32_t()> effectiveRequiredVotes;
    std::function<std::optional<core::poe_v1::KnowledgeEntryV1>(const crypto::Hash256&)> getEntry;
    std::function<bool(const crypto::Hash256&)> isFinalized;
    std::function<size_t(const crypto::Hash256&)> voteCount;
    std::function<uint64_t(const core::poe_v1::KnowledgeEntryV1&)> calculateAcceptanceReward;
    std::function<bool(const crypto::Hash256&)> acceptanceRewardCredited;
    std::function<std::string(const crypto::PublicKey&)> addressFromPubKey;
    std::function<std::string()> currentAddress;
    std::function<double(uint64_t)> atomsToNgt;
};

TuiKnowledgeRefresh buildTuiKnowledgeRefresh(
    const TuiKnowledgeSnapshotBuilderHooks& hooks,
    std::unordered_set<std::string>& notifiedKnowledgePaid);

struct TuiKnowledgeSnapshotCollectorInputs {
    const core::PoeV1Engine* poe = nullptr;
    std::function<bool(const crypto::Hash256&)> acceptanceRewardCredited;
    std::function<std::string(const crypto::PublicKey&)> addressFromPubKey;
    std::string currentAddress;
    std::function<double(uint64_t)> atomsToNgt;
};

TuiKnowledgeRefresh collectTuiKnowledgeRefresh(
    const TuiKnowledgeSnapshotCollectorInputs& inputs,
    std::unordered_set<std::string>& notifiedKnowledgePaid);

struct TuiObservatoryFeedBuilderHooks {
    std::function<std::vector<core::ObservatoryEntry>()> listObservatoryEntries;
    std::function<std::string(const std::string&)> redactPayload;
    std::function<void()> onPayloadRedacted;
};

std::vector<ObservatoryArtifactInfo> buildObservatoryFeed(
    const TuiObservatoryFeedBuilderHooks& hooks);

struct TuiRawAgentEvent {
    uint64_t timestamp = 0;
    std::string category;
    std::string message;
};

struct TuiAgentEventsBuilderHooks {
    std::function<std::vector<TuiRawAgentEvent>()> listAgentEvents;
};

std::vector<AgentEventInfo> buildAgentEvents(
    const TuiAgentEventsBuilderHooks& hooks);

struct TuiAttachedAgentDraftQueueSnapshot {
    uint64_t size = 0;
    uint64_t queued = 0;
    uint64_t review = 0;
    uint64_t approved = 0;
    uint64_t rejected = 0;
    uint64_t submitted = 0;
};

struct TuiAttachedAgentPipelineSnapshot {
    uint64_t runs = 0;
    uint64_t approved = 0;
    uint64_t rejected = 0;
    uint64_t submitted = 0;
};

struct TuiAttachedAgentTimingSnapshot {
    uint64_t lastActionAt = 0;
    uint64_t lastReviewAt = 0;
    uint64_t lastDraftAt = 0;
    uint64_t lastHeartbeatAt = 0;
    uint64_t tickCount = 0;
};

struct TuiAttachedAgentTaskRunsSnapshot {
    uint64_t research = 0;
    uint64_t verify = 0;
    uint64_t review = 0;
    uint64_t draft = 0;
    uint64_t submit = 0;
};

struct TuiAttachedAgentTorSnapshot {
    bool required = false;
    bool reachable = false;
    bool readyForWeb = false;
    bool readyForOnion = false;
    bool controlReachable = false;
    uint16_t controlPort = 0;
    bool managed = false;
    core::TorRoutePolicyDecision route{};
    uint32_t bootstrapPercent = 0;
    std::string socksHost;
    uint16_t socksPort = 0;
    bool conflictHint9050 = false;
    bool clearnetFallbackAllowed = false;
    bool p2pFallbackAllowed = false;
    std::string runtimeMode;
    bool privacyMode = false;
    bool onionServiceActive = false;
    std::string onionAddress;
    std::string torSeedAddress;
};

struct TuiAttachedAgentConfigSnapshot {
    std::string dataDir;
    std::string configPath;
    std::string webConfigPath;
    uint64_t redactionCount = 0;
    std::string policyHash;
    uint16_t p2pPort = 0;
    double syncProgress = 0.0;
    uint32_t maxPeers = 0;
    uint32_t maxInbound = 0;
    uint32_t maxOutbound = 0;
    uint64_t networkHeight = 0;
};

struct TuiAttachedAgentNetworkSnapshot {
    bool available = false;
    uint64_t connected = 0;
    uint64_t inbound = 0;
    uint64_t outbound = 0;
    network::NetworkStats stats{};
    uint64_t invBackpressureDrops = 0;
    uint64_t getDataBackpressureDrops = 0;
    uint64_t gossipSuppressed = 0;
    uint64_t gossipSubsetRouted = 0;
};

struct TuiAttachedAgentLedgerSnapshot {
    bool available = false;
    uint64_t height = 0;
    std::string tipHash;
    uint64_t lastBlockTime = 0;
};

struct TuiAttachedAgentMiningSnapshot {
    bool active = false;
    uint64_t hashAttemptsTotal = 0;
    uint64_t hashAttemptsLast = 0;
    uint64_t lastSolvedAt = 0;
    std::string workTarget;
    std::string candidateHash;
};

struct TuiAttachedAgentStorageSnapshot {
    core::AgentStorageAuditStats audit{};
    uint64_t recoveredLines = 0;
    uint64_t droppedSegments = 0;
    uint64_t indexRecoveryRuns = 0;
    uint64_t indexRecoveryLastAt = 0;
    uint64_t consistencyChecks = 0;
    uint64_t consistencyRepairs = 0;
    uint64_t consistencyLastAt = 0;
};

struct TuiAttachedAgentWebSnapshot {
    bool connectorAvailable = false;
    web::WebConnectorHealth connectorHealth{};
    web::WebStats stats{};
    uint64_t lastSearchAt = 0;
    std::string query;
    std::string queryType;
    uint64_t resultCount = 0;
    uint64_t clearnetResults = 0;
    uint64_t onionResults = 0;
    std::string topSites;
    bool saved = false;
    std::string skipReason;
    std::string error;
};

struct TuiAttachedAgentStatusInputs {
    bool available = true;
    bool runtimeInitialized = false;
    std::string runtimeState;
    core::AgentRuntimeCrashState crashState{};
    core::AgentTaskSchedulerSnapshot schedulerState{};
    core::AgentAdaptiveSchedule adaptiveSchedule{};
    core::AgentScoreSnapshot score{};
    TuiAttachedAgentDraftQueueSnapshot draftQueue{};
    TuiAttachedAgentPipelineSnapshot pipeline{};
    TuiAttachedAgentTimingSnapshot timing{};
    TuiAttachedAgentTaskRunsSnapshot taskRuns{};
    TuiAttachedAgentTorSnapshot tor{};
    TuiAttachedAgentConfigSnapshot config{};
    TuiAttachedAgentNetworkSnapshot network{};
    TuiAttachedAgentLedgerSnapshot ledger{};
    TuiAttachedAgentMiningSnapshot mining{};
    TuiAttachedAgentStorageSnapshot storage{};
    TuiAttachedAgentWebSnapshot web{};
    uint64_t connectorAbuseLastAt = 0;
};

struct TuiAttachedAgentStatusCollectorInputs {
    bool available = true;
    bool runtimeInitialized = false;
    core::AgentRuntimeFailoverState runtimeState = core::AgentRuntimeFailoverState::RECOVERY;
    core::AgentRuntimeCrashState crashState{};
    core::AgentTaskSchedulerSnapshot schedulerState{};
    core::AgentAdaptiveSchedule adaptiveSchedule{};
    core::AgentScoreSnapshot score{};
    const core::AgentDraftQueue* draftQueue = nullptr;
    TuiAttachedAgentPipelineSnapshot pipeline{};
    TuiAttachedAgentTimingSnapshot timing{};
    TuiAttachedAgentTaskRunsSnapshot taskRuns{};
    TuiAttachedAgentTorSnapshot tor{};
    TuiAttachedAgentConfigSnapshot config{};
    const network::Network* network = nullptr;
    uint64_t invBackpressureDrops = 0;
    uint64_t getDataBackpressureDrops = 0;
    uint64_t gossipSuppressed = 0;
    uint64_t gossipSubsetRouted = 0;
    const core::Ledger* ledger = nullptr;
    bool miningActive = false;
    uint64_t miningHashAttemptsTotal = 0;
    uint64_t miningHashAttemptsLast = 0;
    uint64_t miningLastSolvedAt = 0;
    std::string miningWorkTarget;
    std::string miningCandidateHash;
    core::AgentStorageAuditStats storageAudit{};
    uint64_t storageRecoveredLines = 0;
    uint64_t storageDroppedSegments = 0;
    uint64_t storageIndexRecoveryRuns = 0;
    uint64_t storageIndexRecoveryLastAt = 0;
    uint64_t storageConsistencyChecks = 0;
    uint64_t storageConsistencyRepairs = 0;
    uint64_t storageConsistencyLastAt = 0;
    bool webConnectorAvailable = false;
    web::WebConnectorHealth webConnectorHealth{};
    web::WebStats webStats{};
    uint64_t webLastSearchAt = 0;
    std::string webQuery;
    std::string webQueryType;
    uint64_t webResultCount = 0;
    uint64_t webClearnetResults = 0;
    uint64_t webOnionResults = 0;
    std::string webTopSites;
    bool webSaved = false;
    std::string webSkipReason;
    std::string webError;
    uint64_t connectorAbuseLastAt = 0;
};

AttachedAgentStatusInfo buildAttachedAgentStatus(
    const TuiAttachedAgentStatusInputs& inputs);

AttachedAgentStatusInfo collectAttachedAgentStatus(
    const TuiAttachedAgentStatusCollectorInputs& inputs);

} // namespace synapse::tui
