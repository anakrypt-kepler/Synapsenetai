#include "tui/tui_snapshot_builder.h"

#include "crypto/crypto.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace synapse::tui {

TuiCoreSnapshot buildTuiCoreSnapshot(const TuiCoreSnapshotBuilderHooks& hooks) {
    TuiCoreSnapshot snapshot;
    auto& netInfo = snapshot.networkInfo;
    netInfo.totalNodes = hooks.peerCount ? static_cast<uint64_t>(hooks.peerCount()) : 0;
    netInfo.knowledgeEntries = hooks.knowledgeEntries ? hooks.knowledgeEntries() : 0;
    netInfo.knowledgeFinalized = hooks.knowledgeFinalized ? hooks.knowledgeFinalized() : 0;
    netInfo.knowledgePending =
        netInfo.knowledgeEntries > netInfo.knowledgeFinalized
            ? (netInfo.knowledgeEntries - netInfo.knowledgeFinalized)
            : 0;

    if (hooks.discoveryStats) {
        auto stats = hooks.discoveryStats();
        if (stats.has_value()) {
            netInfo.networkSize = static_cast<double>(stats->networkSize);
            netInfo.knownPeers = stats->knownPeersCount;
            netInfo.connectedPeers = stats->connectedPeers;
            netInfo.dnsQueries = stats->dnsQueries;
            netInfo.peerExchanges = stats->peerExchanges;
            netInfo.lastPeerRefresh = stats->lastRefreshTime;
            netInfo.lastAnnounce = stats->lastAnnounceTime;
            netInfo.bootstrapNodes = hooks.bootstrapNodeCount ? static_cast<uint64_t>(hooks.bootstrapNodeCount()) : 0;
            netInfo.dnsSeeds = hooks.dnsSeedCount ? static_cast<uint64_t>(hooks.dnsSeedCount()) : 0;
        }
    }

    if (hooks.poeBootstrapInfo) {
        const auto poeBootstrap = hooks.poeBootstrapInfo();
        netInfo.poeValidatorCount = poeBootstrap.validatorCount;
        netInfo.poeSelfBootstrapActive = poeBootstrap.selfBootstrapActive;
        netInfo.poeBootstrapMode = poeBootstrap.mode;
    }

    netInfo.yourStorage = 0.0;
    netInfo.syncProgress = hooks.syncProgress ? hooks.syncProgress() : 0.0;
    netInfo.synced = netInfo.syncProgress >= 1.0;
    netInfo.lastReward = hooks.lastReward ? hooks.lastReward() : 0;
    netInfo.lastRewardEpochId = hooks.lastRewardEpochId ? hooks.lastRewardEpochId() : 0;
    netInfo.lastRewardEntries = hooks.lastRewardEntries ? hooks.lastRewardEntries() : 0;
    netInfo.lastRewardAt = hooks.lastRewardAt ? hooks.lastRewardAt() : 0;

    snapshot.peerCount = hooks.peerCount ? hooks.peerCount() : 0;
    if (hooks.peers && hooks.peerDisplayInfo) {
        auto peers = hooks.peers();
        snapshot.peers.reserve(peers.size());
        for (const auto& peer : peers) {
            const auto display = hooks.peerDisplayInfo(peer);
            NodeInfo nodeInfo;
            nodeInfo.nodeId = peer.id;
            nodeInfo.id = peer.id.substr(0, 16) + "...";
            nodeInfo.address = display.displayAddress;
            nodeInfo.displayAddress = display.displayAddress;
            nodeInfo.rawAddress = core::formatPeerAddress(display.rawAddress, display.rawPort);
            nodeInfo.transport = display.transport;
            nodeInfo.location = core::peerTransportLabel(display.transport);
            nodeInfo.port = peer.port;
            nodeInfo.latency = 50;
            nodeInfo.ping = 50;
            nodeInfo.version = std::to_string(peer.version);
            nodeInfo.isInbound = !peer.isOutbound;
            snapshot.peers.push_back(std::move(nodeInfo));
        }
    }

    auto& modelInfo = snapshot.modelInfo;
    if (hooks.listModels) {
        auto models = hooks.listModels(hooks.modelsDirectory);
        if (!models.empty()) {
            modelInfo.name = models[0].name;
            modelInfo.status = "ACTIVE";
            modelInfo.progress = 1.0;
        } else {
            modelInfo.name = "";
            modelInfo.status = "NOT LOADED";
            modelInfo.progress = 0.0;
        }
    } else {
        modelInfo.name = "";
        modelInfo.status = "NOT LOADED";
        modelInfo.progress = 0.0;
    }
    modelInfo.mode = "PRIVATE";
    modelInfo.slotsUsed = 0;
    modelInfo.slotsMax = 1;
    modelInfo.uptime = 0.0;
    modelInfo.earningsTodayAtoms = 0;
    modelInfo.earningsWeekAtoms = 0;
    modelInfo.earningsTotalAtoms = 0;

    auto& walletInfo = snapshot.walletInfo;
    const bool walletReady = hooks.walletReady && hooks.walletReady();
    walletInfo.address = walletReady && hooks.walletAddress ? hooks.walletAddress() : "";
    walletInfo.balance = walletReady && hooks.walletBalance ? hooks.walletBalance() : 0;
    walletInfo.pending = walletReady && hooks.walletPending ? hooks.walletPending() : 0;
    walletInfo.staked = 0;
    walletInfo.totalEarned = 0;

    auto& status = snapshot.statusInfo;
    status.blockHeight = hooks.blockHeight ? hooks.blockHeight() : 0;
    status.peerCount = snapshot.peerCount;
    status.knowledgeCount = netInfo.knowledgeEntries;
    status.balance = walletInfo.balance;
    status.walletAddress = walletInfo.address;
    status.modelName = modelInfo.name;
    status.modelStatus = modelInfo.status;
    status.syncProgress = netInfo.syncProgress;
    status.quantumEnabled = hooks.quantumEnabled ? hooks.quantumEnabled() : false;
    status.liboqsEnabled = hooks.liboqsEnabled ? hooks.liboqsEnabled() : false;
    status.quantumCapabilityMode = hooks.quantumCapabilityMode ? hooks.quantumCapabilityMode() : "";
    status.kyberReal = hooks.kyberReal ? hooks.kyberReal() : false;
    status.dilithiumReal = hooks.dilithiumReal ? hooks.dilithiumReal() : false;
    status.sphincsReal = hooks.sphincsReal ? hooks.sphincsReal() : false;
    status.kyberImplementation = hooks.kyberImplementation ? hooks.kyberImplementation() : "";
    status.dilithiumImplementation = hooks.dilithiumImplementation ? hooks.dilithiumImplementation() : "";
    status.sphincsImplementation = hooks.sphincsImplementation ? hooks.sphincsImplementation() : "";
    return snapshot;
}

TuiCoreSnapshot collectTuiCoreSnapshot(const TuiCoreSnapshotCollectorInputs& inputs) {
    TuiCoreSnapshotBuilderHooks hooks;
    hooks.peerCount = [network = inputs.network]() { return network ? network->peerCount() : 0; };
    hooks.knowledgeEntries = [poe = inputs.poe, knowledge = inputs.knowledge]() {
        return poe ? poe->totalEntries() : (knowledge ? knowledge->totalEntries() : 0);
    };
    hooks.knowledgeFinalized = [poe = inputs.poe]() { return poe ? poe->totalFinalized() : 0; };
    hooks.discoveryStats = [discovery = inputs.discovery]() -> std::optional<network::DiscoveryStats> {
        if (!discovery) {
            return std::nullopt;
        }
        return discovery->getStats();
    };
    hooks.bootstrapNodeCount = [discovery = inputs.discovery]() {
        return discovery ? discovery->getBootstrapNodes().size() : 0;
    };
    hooks.dnsSeedCount = [discovery = inputs.discovery]() {
        return discovery ? discovery->getDnsSeeds().size() : 0;
    };
    hooks.poeBootstrapInfo = [info = inputs.poeBootstrapInfo]() { return info; };
    hooks.syncProgress = [value = inputs.syncProgress]() { return value; };
    hooks.lastReward = [value = inputs.lastReward]() { return value; };
    hooks.lastRewardEpochId = [value = inputs.lastRewardEpochId]() { return value; };
    hooks.lastRewardEntries = [value = inputs.lastRewardEntries]() { return value; };
    hooks.lastRewardAt = [value = inputs.lastRewardAt]() { return value; };
    hooks.peers = [network = inputs.network]() {
        if (!network) {
            return std::vector<network::Peer>{};
        }
        return network->getPeers();
    };
    hooks.peerDisplayInfo = inputs.peerDisplayInfo;
    hooks.listModels = [loader = inputs.modelLoader](const std::string& directory) {
        if (!loader) {
            return std::vector<model::ModelInfo>{};
        }
        return loader->listModels(directory);
    };
    hooks.modelsDirectory = inputs.modelsDirectory;
    hooks.walletReady = [value = inputs.walletReady]() { return value; };
    hooks.walletAddress = [value = inputs.walletAddress]() { return value; };
    hooks.walletBalance = [value = inputs.walletBalance]() { return value; };
    hooks.walletPending = [value = inputs.walletPending]() { return value; };
    hooks.blockHeight = [value = inputs.blockHeight]() { return value; };
    hooks.quantumEnabled = [value = inputs.quantumEnabled]() { return value; };
    hooks.liboqsEnabled = [value = inputs.liboqsEnabled]() { return value; };
    hooks.quantumCapabilityMode = [value = inputs.quantumCapabilityMode]() { return value; };
    hooks.kyberReal = [value = inputs.kyberReal]() { return value; };
    hooks.dilithiumReal = [value = inputs.dilithiumReal]() { return value; };
    hooks.sphincsReal = [value = inputs.sphincsReal]() { return value; };
    hooks.kyberImplementation = [value = inputs.kyberImplementation]() { return value; };
    hooks.dilithiumImplementation = [value = inputs.dilithiumImplementation]() { return value; };
    hooks.sphincsImplementation = [value = inputs.sphincsImplementation]() { return value; };
    return buildTuiCoreSnapshot(hooks);
}

TuiKnowledgeRefresh buildTuiKnowledgeRefresh(
    const TuiKnowledgeSnapshotBuilderHooks& hooks,
    std::unordered_set<std::string>& notifiedKnowledgePaid) {
    TuiKnowledgeRefresh refresh;
    if (!hooks.listEntryIds || !hooks.poeConfig || !hooks.effectiveRequiredVotes ||
        !hooks.getEntry || !hooks.isFinalized || !hooks.voteCount ||
        !hooks.calculateAcceptanceReward || !hooks.acceptanceRewardCredited ||
        !hooks.addressFromPubKey || !hooks.currentAddress) {
        return refresh;
    }

    struct Tmp {
        KnowledgeEntrySummary summary;
        uint64_t timestamp = 0;
        bool mine = false;
    };

    std::vector<Tmp> tmp;
    auto ids = hooks.listEntryIds();
    tmp.reserve(ids.size());
    core::PoeV1Config cfg = hooks.poeConfig();
    uint32_t requiredVotes = hooks.effectiveRequiredVotes();
    if (requiredVotes == 0) {
        requiredVotes = cfg.validatorsM == 0 ? 1U : cfg.validatorsM;
    }

    const std::string currentAddress = hooks.currentAddress();
    for (const auto& submitId : ids) {
        auto entry = hooks.getEntry(submitId);
        if (!entry.has_value()) {
            continue;
        }
        Tmp item;
        item.timestamp = entry->timestamp;
        item.summary.submitId = crypto::toHex(submitId);
        item.summary.title = entry->title;
        item.summary.contentType = static_cast<uint8_t>(entry->contentType);
        item.summary.finalized = hooks.isFinalized(submitId);
        item.summary.votes = static_cast<uint32_t>(hooks.voteCount(submitId));
        item.summary.requiredVotes = requiredVotes;
        item.summary.acceptanceRewardAtoms = hooks.calculateAcceptanceReward(*entry);
        item.summary.acceptanceRewardCredited = hooks.acceptanceRewardCredited(submitId);
        item.mine = !currentAddress.empty() && hooks.addressFromPubKey(entry->authorPubKey) == currentAddress;
        tmp.push_back(std::move(item));
    }

    std::sort(tmp.begin(), tmp.end(), [](const Tmp& lhs, const Tmp& rhs) { return lhs.timestamp > rhs.timestamp; });
    refresh.entries.reserve(tmp.size());
    for (const auto& item : tmp) {
        refresh.entries.push_back(item.summary);
    }
    for (const auto& item : tmp) {
        if (!item.mine || !item.summary.finalized || !item.summary.acceptanceRewardCredited) {
            continue;
        }
        if (!notifiedKnowledgePaid.insert(item.summary.submitId).second) {
            continue;
        }
        std::string sidShort =
            item.summary.submitId.size() > 8 ? item.summary.submitId.substr(0, 8) : item.summary.submitId;
        std::ostringstream msg;
        std::string kind =
            (item.summary.contentType == static_cast<uint8_t>(core::poe_v1::ContentType::CODE))
                ? "Code"
                : "Knowledge";
        msg << kind << " reward (" << sidShort << "): +" << std::fixed << std::setprecision(8)
            << (static_cast<double>(item.summary.acceptanceRewardAtoms) / 100000000.0) << " NGT";
        refresh.chatMessages.push_back({"assistant", msg.str()});
    }

    return refresh;
}

TuiKnowledgeRefresh collectTuiKnowledgeRefresh(
    const TuiKnowledgeSnapshotCollectorInputs& inputs,
    std::unordered_set<std::string>& notifiedKnowledgePaid) {
    TuiKnowledgeSnapshotBuilderHooks hooks;
    hooks.listEntryIds = [poe = inputs.poe]() {
        if (!poe) {
            return std::vector<crypto::Hash256>{};
        }
        return poe->listEntryIds(0);
    };
    hooks.poeConfig = [poe = inputs.poe]() { return poe ? poe->getConfig() : core::PoeV1Config{}; };
    hooks.effectiveRequiredVotes = [poe = inputs.poe]() { return poe ? poe->effectiveRequiredVotes() : 0; };
    hooks.getEntry = [poe = inputs.poe](const crypto::Hash256& submitId) {
        if (!poe) {
            return std::optional<core::poe_v1::KnowledgeEntryV1>{};
        }
        return poe->getEntry(submitId);
    };
    hooks.isFinalized = [poe = inputs.poe](const crypto::Hash256& submitId) {
        return poe && poe->isFinalized(submitId);
    };
    hooks.voteCount = [poe = inputs.poe](const crypto::Hash256& submitId) {
        return poe ? poe->getVotesForSubmit(submitId).size() : static_cast<size_t>(0);
    };
    hooks.calculateAcceptanceReward = [poe = inputs.poe](const core::poe_v1::KnowledgeEntryV1& entry) {
        return poe ? poe->calculateAcceptanceReward(entry) : static_cast<uint64_t>(0);
    };
    hooks.acceptanceRewardCredited = inputs.acceptanceRewardCredited;
    hooks.addressFromPubKey = inputs.addressFromPubKey;
    hooks.currentAddress = [value = inputs.currentAddress]() { return value; };
    hooks.atomsToNgt = inputs.atomsToNgt;
    return buildTuiKnowledgeRefresh(hooks, notifiedKnowledgePaid);
}

std::vector<ObservatoryArtifactInfo> buildObservatoryFeed(
    const TuiObservatoryFeedBuilderHooks& hooks) {
    std::vector<ObservatoryArtifactInfo> items;
    if (!hooks.listObservatoryEntries || !hooks.redactPayload) {
        return items;
    }

    auto feed = hooks.listObservatoryEntries();
    items.reserve(feed.size());
    for (const auto& entry : feed) {
        ObservatoryArtifactInfo item;
        item.hash = crypto::toHex(entry.hash);
        item.roomId = entry.roomId;
        item.type = core::roomMessageTypeToString(entry.type);
        item.author = crypto::toHex(entry.author);
        item.payloadPreview = hooks.redactPayload(entry.payloadPreview);
        if (hooks.onPayloadRedacted && item.payloadPreview != entry.payloadPreview) {
            hooks.onPayloadRedacted();
        }
        item.timestamp = entry.timestamp;
        items.push_back(std::move(item));
    }

    return items;
}

std::vector<AgentEventInfo> buildAgentEvents(
    const TuiAgentEventsBuilderHooks& hooks) {
    std::vector<AgentEventInfo> events;
    if (!hooks.listAgentEvents) {
        return events;
    }

    auto rawEvents = hooks.listAgentEvents();
    events.reserve(rawEvents.size());
    for (const auto& event : rawEvents) {
        AgentEventInfo info;
        info.timestamp = event.timestamp;
        info.category = event.category;
        info.message = event.message;
        events.push_back(std::move(info));
    }

    return events;
}

AttachedAgentStatusInfo buildAttachedAgentStatus(
    const TuiAttachedAgentStatusInputs& inputs) {
    AttachedAgentStatusInfo info;
    info.available = inputs.available;
    info.runtimeState = inputs.runtimeState;
    info.runtimeInitialized = inputs.runtimeInitialized;
    info.schedulerState = core::schedulingStateToString(inputs.adaptiveSchedule.state);
    info.runtimeCrashCount = inputs.crashState.totalCrashes;
    info.runtimeConsecutiveCrashes = inputs.crashState.consecutiveCrashes;
    info.schedulerTick = inputs.schedulerState.tick;
    info.schedulerEpochIndex = inputs.schedulerState.epochIndex;
    info.schedulerBudgetCpu = inputs.schedulerState.remaining.cpu;
    info.schedulerBudgetRam = inputs.schedulerState.remaining.ram;
    info.schedulerBudgetNetwork = inputs.schedulerState.remaining.network;
    info.agentScore = inputs.score.score;
    info.agentScoreBand = core::agentScoreBandToString(inputs.score.band);
    info.throttled = inputs.score.throttled;
    info.quarantined = inputs.score.quarantined;
    info.reviewOnly = inputs.score.reviewOnly;
    info.localDraftOnly = inputs.score.localDraftOnly;
    info.batchLimit = inputs.score.batchLimit;

    info.draftQueueSize = inputs.draftQueue.size;
    info.queuedDrafts = inputs.draftQueue.queued;
    info.reviewDrafts = inputs.draftQueue.review;
    info.approvedDrafts = inputs.draftQueue.approved;
    info.rejectedDrafts = inputs.draftQueue.rejected;
    info.submittedDrafts = inputs.draftQueue.submitted;

    info.pipelineRuns = inputs.pipeline.runs;
    info.pipelineApproved = inputs.pipeline.approved;
    info.pipelineRejected = inputs.pipeline.rejected;
    info.pipelineSubmitted = inputs.pipeline.submitted;

    info.lastActionAt = inputs.timing.lastActionAt;
    info.lastReviewAt = inputs.timing.lastReviewAt;
    info.lastDraftAt = inputs.timing.lastDraftAt;
    info.lastHeartbeatAt = inputs.timing.lastHeartbeatAt;
    info.tickCount = inputs.timing.tickCount;

    info.taskRunsResearch = inputs.taskRuns.research;
    info.taskRunsVerify = inputs.taskRuns.verify;
    info.taskRunsReview = inputs.taskRuns.review;
    info.taskRunsDraft = inputs.taskRuns.draft;
    info.taskRunsSubmit = inputs.taskRuns.submit;

    info.torRequired = inputs.tor.required;
    info.torReachable = inputs.tor.reachable;
    info.torReadyForWeb = inputs.tor.readyForWeb;
    info.torReadyForOnion = inputs.tor.readyForOnion;
    info.torControlReachable = inputs.tor.controlReachable;
    info.torControlPort = inputs.tor.controlPort;
    info.torManaged = inputs.tor.managed;
    info.torDegraded = inputs.tor.route.torDegraded;
    info.torBootstrapPercent = inputs.tor.bootstrapPercent;
    info.torSocksHost = inputs.tor.socksHost;
    info.torSocksPort = inputs.tor.socksPort;
    info.torConflictHint9050 = inputs.tor.conflictHint9050;
    info.clearnetFallbackAllowed = inputs.tor.clearnetFallbackAllowed;
    info.p2pFallbackAllowed = inputs.tor.p2pFallbackAllowed;
    info.routeMode = inputs.tor.route.routeMode;
    info.torRuntimeMode = inputs.tor.runtimeMode;
    info.onionServiceActive = inputs.tor.onionServiceActive;
    info.onionAddress = inputs.tor.onionAddress;
    info.torSeedAddress = inputs.tor.torSeedAddress;
    core::TorOnionServiceStateInput onionSvc{
        inputs.tor.required,
        inputs.tor.reachable,
        inputs.tor.readyForWeb,
        inputs.tor.route.torDegraded,
        inputs.tor.privacyMode,
        inputs.tor.controlReachable,
        inputs.tor.onionServiceActive};
    info.torReadyForOnionService = core::evaluateTorReadyForOnionService(onionSvc);
    info.onionServiceState = core::evaluateTorOnionServiceState(onionSvc);

    info.dataDir = inputs.config.dataDir;
    info.configPath = inputs.config.configPath;
    info.webConfigPath = inputs.config.webConfigPath;
    info.redactionCount = inputs.config.redactionCount;
    info.policyHash = inputs.config.policyHash;
    info.p2pPort = inputs.config.p2pPort;
    info.p2pSyncProgress = inputs.config.syncProgress;

    info.p2pConnected = inputs.network.connected;
    info.p2pInbound = inputs.network.inbound;
    info.p2pOutbound = inputs.network.outbound;
    if (inputs.network.available) {
        info.networkPeerPressurePercent =
            inputs.config.maxPeers == 0
                ? 0
                : (inputs.network.stats.totalPeers * 100ULL / inputs.config.maxPeers);
        info.networkInboundPressurePercent =
            inputs.config.maxInbound == 0
                ? 0
                : (inputs.network.stats.inboundPeers * 100ULL / inputs.config.maxInbound);
        info.networkOutboundPressurePercent =
            inputs.config.maxOutbound == 0
                ? 0
                : (inputs.network.stats.outboundPeers * 100ULL / inputs.config.maxOutbound);
        info.networkOverloadMode = inputs.network.stats.overloadMode;
        info.networkBufferedRxBytes = inputs.network.stats.bufferedRxBytes;
        info.networkRejectedConnections = inputs.network.stats.rejectedConnections;
        info.networkEvictedPeers = inputs.network.stats.evictedPeers;
        info.networkTempBans = inputs.network.stats.tempBans;
        info.networkMalformedMessages = inputs.network.stats.malformedMessages;
        info.networkRateLimitedEvents = inputs.network.stats.rateLimitedEvents;
        info.networkOverloadTransitions = inputs.network.stats.overloadTransitions;
        info.networkInvBackpressureDrops = inputs.network.invBackpressureDrops;
        info.networkGetDataBackpressureDrops = inputs.network.getDataBackpressureDrops;
        info.networkGossipSuppressed = inputs.network.gossipSuppressed;
        info.networkGossipSubsetRouted = inputs.network.gossipSubsetRouted;
    }

    info.ledgerHeight = inputs.ledger.height;
    info.ledgerTipHash = inputs.ledger.tipHash;
    info.ledgerLastBlockTime = inputs.ledger.lastBlockTime;
    info.networkConsensusLag =
        inputs.config.networkHeight > inputs.ledger.height
            ? (inputs.config.networkHeight - inputs.ledger.height)
            : 0;

    info.miningActive = inputs.mining.active;
    info.miningHashAttemptsTotal = inputs.mining.hashAttemptsTotal;
    info.miningHashAttemptsLast = inputs.mining.hashAttemptsLast;
    info.miningLastSolvedAt = inputs.mining.lastSolvedAt;
    info.miningWorkTarget = inputs.mining.workTarget;
    info.miningCandidateHash = inputs.mining.candidateHash;

    info.storageAuditSegments = inputs.storage.audit.segmentCount;
    info.storageAuditRetainedEvents = inputs.storage.audit.retainedEvents;
    info.storageAuditRecoveredLines = inputs.storage.recoveredLines;
    info.storageAuditDroppedSegments = inputs.storage.droppedSegments;
    info.storageIndexRecoveryRuns = inputs.storage.indexRecoveryRuns;
    info.storageIndexRecoveryLastAt = inputs.storage.indexRecoveryLastAt;
    info.storageConsistencyChecks = inputs.storage.consistencyChecks;
    info.storageConsistencyRepairs = inputs.storage.consistencyRepairs;
    info.storageConsistencyLastAt = inputs.storage.consistencyLastAt;

    info.quarantineReason = "none";
    info.quarantineReasonSince = 0;
    if (inputs.score.quarantined) {
        info.quarantineReasonSince = inputs.connectorAbuseLastAt;
        if (info.quarantineReasonSince > 0) {
            info.quarantineReason = "connector_auto_quarantine";
        } else if (inputs.score.localDraftOnly) {
            info.quarantineReason = "score_policy_local_draft_only";
        } else if (inputs.score.reviewOnly) {
            info.quarantineReason = "score_policy_review_only";
        } else if (inputs.score.throttled) {
            info.quarantineReason = "score_policy_throttled";
        } else {
            info.quarantineReason = "score_policy";
        }
    }

    info.connectorAvailable = inputs.web.connectorAvailable;
    if (inputs.web.connectorAvailable) {
        info.connectorClearnetState =
            web::connectorHealthStateToString(inputs.web.connectorHealth.clearnet.state);
        info.connectorTorState =
            web::connectorHealthStateToString(inputs.web.connectorHealth.tor.state);
        info.connectorOnionState =
            web::connectorHealthStateToString(inputs.web.connectorHealth.onion.state);
        info.connectorPolicyBlocks =
            inputs.web.connectorHealth.clearnet.policyBlocks +
            inputs.web.connectorHealth.tor.policyBlocks +
            inputs.web.connectorHealth.onion.policyBlocks;
        info.connectorFailures =
            inputs.web.connectorHealth.clearnet.failures +
            inputs.web.connectorHealth.tor.failures +
            inputs.web.connectorHealth.onion.failures;
        info.webTotalSearches = inputs.web.stats.totalSearches;
        info.webSuccessfulFetches = inputs.web.stats.successfulFetches;
        info.webFailedFetches = inputs.web.stats.failedFetches;
        info.webPagesExtracted = inputs.web.stats.pagesExtracted;
        info.webBytesDownloaded = inputs.web.stats.bytesDownloaded;
    } else {
        info.connectorClearnetState = "unavailable";
        info.connectorTorState = "unavailable";
        info.connectorOnionState = "unavailable";
    }

    info.webLastSearchAt = inputs.web.lastSearchAt;
    info.webLastQuery = inputs.web.query;
    info.webLastQueryType = inputs.web.queryType;
    info.webLastResultCount = inputs.web.resultCount;
    info.webLastClearnetResults = inputs.web.clearnetResults;
    info.webLastOnionResults = inputs.web.onionResults;
    info.webLastTopSites = inputs.web.topSites;
    info.webLastPostSaved = inputs.web.saved;
    info.webLastSkipReason = inputs.web.skipReason;
    info.webLastError = inputs.web.error;

    return info;
}

AttachedAgentStatusInfo collectAttachedAgentStatus(
    const TuiAttachedAgentStatusCollectorInputs& inputs) {
    TuiAttachedAgentStatusInputs built;
    built.available = inputs.available;
    built.runtimeInitialized = inputs.runtimeInitialized;
    built.runtimeState = core::failoverStateToString(inputs.runtimeState);
    built.crashState = inputs.crashState;
    built.schedulerState = inputs.schedulerState;
    built.adaptiveSchedule = inputs.adaptiveSchedule;
    built.score = inputs.score;
    if (inputs.draftQueue) {
        built.draftQueue = {
            inputs.draftQueue->size(),
            inputs.draftQueue->listByStatus(core::DraftStatus::QUEUED, 1000000).size(),
            inputs.draftQueue->listByStatus(core::DraftStatus::REVIEW_REQUIRED, 1000000).size(),
            inputs.draftQueue->listByStatus(core::DraftStatus::APPROVED, 1000000).size(),
            inputs.draftQueue->listByStatus(core::DraftStatus::REJECTED, 1000000).size(),
            inputs.draftQueue->listByStatus(core::DraftStatus::SUBMITTED, 1000000).size()};
    }
    built.pipeline = inputs.pipeline;
    built.timing = inputs.timing;
    built.taskRuns = inputs.taskRuns;
    built.tor = inputs.tor;
    built.config = inputs.config;

    if (inputs.network) {
        built.network.available = true;
        built.network.connected = inputs.network->peerCount();
        built.network.stats = inputs.network->getStats();
        built.network.invBackpressureDrops = inputs.invBackpressureDrops;
        built.network.getDataBackpressureDrops = inputs.getDataBackpressureDrops;
        built.network.gossipSuppressed = inputs.gossipSuppressed;
        built.network.gossipSubsetRouted = inputs.gossipSubsetRouted;
        for (const auto& peer : inputs.network->getPeers()) {
            if (peer.state != network::PeerState::CONNECTED) {
                continue;
            }
            if (peer.isOutbound) {
                built.network.outbound += 1;
            } else {
                built.network.inbound += 1;
            }
        }
    }

    if (inputs.ledger) {
        auto tip = inputs.ledger->tip();
        built.ledger.available = true;
        built.ledger.height = inputs.ledger->height();
        built.ledger.tipHash = crypto::toHex(tip.hash);
        built.ledger.lastBlockTime = tip.timestamp;
    }

    built.mining.active = inputs.miningActive;
    built.mining.hashAttemptsTotal = inputs.miningHashAttemptsTotal;
    built.mining.hashAttemptsLast = inputs.miningHashAttemptsLast;
    built.mining.lastSolvedAt = inputs.miningLastSolvedAt;
    built.mining.workTarget = inputs.miningWorkTarget;
    built.mining.candidateHash = inputs.miningCandidateHash;

    built.storage.audit = inputs.storageAudit;
    built.storage.recoveredLines = inputs.storageRecoveredLines;
    built.storage.droppedSegments = inputs.storageDroppedSegments;
    built.storage.indexRecoveryRuns = inputs.storageIndexRecoveryRuns;
    built.storage.indexRecoveryLastAt = inputs.storageIndexRecoveryLastAt;
    built.storage.consistencyChecks = inputs.storageConsistencyChecks;
    built.storage.consistencyRepairs = inputs.storageConsistencyRepairs;
    built.storage.consistencyLastAt = inputs.storageConsistencyLastAt;

    built.web.connectorAvailable = inputs.webConnectorAvailable;
    built.web.connectorHealth = inputs.webConnectorHealth;
    built.web.stats = inputs.webStats;
    built.web.lastSearchAt = inputs.webLastSearchAt;
    built.web.query = inputs.webQuery;
    built.web.queryType = inputs.webQueryType;
    built.web.resultCount = inputs.webResultCount;
    built.web.clearnetResults = inputs.webClearnetResults;
    built.web.onionResults = inputs.webOnionResults;
    built.web.topSites = inputs.webTopSites;
    built.web.saved = inputs.webSaved;
    built.web.skipReason = inputs.webSkipReason;
    built.web.error = inputs.webError;
    built.connectorAbuseLastAt = inputs.connectorAbuseLastAt;

    return buildAttachedAgentStatus(built);
}

} // namespace synapse::tui
