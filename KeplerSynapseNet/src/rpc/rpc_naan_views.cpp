#include "rpc/rpc_naan_views.h"

#include "crypto/crypto.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <ctime>

namespace synapse::rpc {

namespace {

using json = nlohmann::json;

std::string maybeRedact(const NaanRedactFn& redact, const std::string& value) {
    return redact ? redact(value) : value;
}

json parseJsonObjectOrEmpty(const std::string& raw) {
    if (raw.empty()) {
        return json::object();
    }
    try {
        auto parsed = json::parse(raw);
        if (parsed.is_object()) {
            return parsed;
        }
    } catch (...) {
    }
    return json::object();
}

json stringArray(const std::vector<std::string>& values) {
    json out = json::array();
    for (const auto& value : values) {
        out.push_back(value);
    }
    return out;
}

const char* searchEngineToString(web::SearchEngine engine) {
    switch (engine) {
        case web::SearchEngine::GOOGLE: return "google";
        case web::SearchEngine::BING: return "bing";
        case web::SearchEngine::DUCKDUCKGO: return "duckduckgo";
        case web::SearchEngine::BRAVE: return "brave";
        case web::SearchEngine::AHMIA: return "ahmia";
        case web::SearchEngine::TORCH: return "torch";
        case web::SearchEngine::NOTEVIL: return "notevil";
        case web::SearchEngine::DARKSEARCH: return "darksearch";
        case web::SearchEngine::DEEPSEARCH: return "deepsearch";
        case web::SearchEngine::CUSTOM: return "custom";
    }
    return "custom";
}

json connectorHealthToJson(const web::ConnectorHealthSnapshot& snapshot) {
    json out;
    out["state"] = web::connectorHealthStateToString(snapshot.state);
    out["transitions"] = snapshot.transitions;
    out["successes"] = snapshot.successes;
    out["failures"] = snapshot.failures;
    out["policyBlocks"] = snapshot.policyBlocks;
    out["consecutiveSuccesses"] = snapshot.consecutiveSuccesses;
    out["consecutiveFailures"] = snapshot.consecutiveFailures;
    return out;
}

json searchEngineArray(const std::vector<web::SearchEngine>& engines) {
    json out = json::array();
    for (auto engine : engines) {
        out.push_back(searchEngineToString(engine));
    }
    return out;
}

json buildTorRouteJson(const RpcNodeTorStatusInputs& inputs) {
    const json torStatus = parseJsonObjectOrEmpty(buildRpcNodeTorStatusJson(inputs));
    json out;
    out["mode"] = inputs.routeMode;
    out["required"] = inputs.torRequired;
    out["socksReachable"] = inputs.torSocksReachable;
    out["reachable"] = inputs.torSocksReachable;
    out["webReady"] = inputs.torReadyForWeb;
    out["managed"] = inputs.torManaged;
    out["managedPid"] = inputs.torManagedPid;
    out["runtimeMode"] = inputs.torRuntimeMode;
    out["socksPort"] = inputs.torSocksPort;
    out["controlPort"] = inputs.torControlPort;
    out["controlReachable"] = inputs.torControlReachable;
    out["conflictHint9050"] = inputs.torConflictHint9050;
    out["bootstrapState"] = torStatus.value("torBootstrapState", std::string("offline"));
    out["bootstrapPercent"] = torStatus.value("torBootstrapPercent", 0U);
    out["readyForOnion"] = torStatus.value("torReadyForOnion", false);
    out["readyForOnionService"] = torStatus.value("torReadyForOnionService", false);
    out["onionServiceActive"] = inputs.onionServiceActive;
    out["onionServiceState"] = torStatus.value("torOnionServiceState", std::string("offline"));
    out["webProbeConsecutiveFailures"] = inputs.torWebProbeConsecutiveFailures;
    out["webProbeConsecutiveSuccesses"] = inputs.torWebProbeConsecutiveSuccesses;
    out["reasonCode"] = torStatus.value("torBootstrapReasonCode", std::string());
    out["lastBootstrapError"] = torStatus.value("torLastBootstrapError", std::string());
    out["degraded"] = inputs.torDegraded;
    out["bridgeSubsetPersistCount"] = inputs.torBridgeSubsetPersistCount;
    out["bridgeSubsetLastPersistAt"] = inputs.torBridgeSubsetLastPersistAt;
    out["bridgeSubsetLastEpoch"] = inputs.torBridgeSubsetLastEpoch;
    out["bridgeSubsetLastCount"] = inputs.torBridgeSubsetLastCount;
    out["bridgeSubsetPath"] = inputs.torBridgeSubsetPath;
    out["bridgeRemoteLastFetchAt"] = inputs.torBridgeRemoteLastFetchAt;
    out["bridgeRemoteFetchAttempts"] = inputs.torBridgeRemoteFetchAttempts;
    out["bridgeRemoteFetchSuccesses"] = inputs.torBridgeRemoteFetchSuccesses;
    out["bridgeRemoteRateLimitedSkips"] = inputs.torBridgeRemoteRateLimitedSkips;
    out["bridgeProvider"] = parseJsonObjectOrEmpty(inputs.torBridgeProviderJson);
    out["bridgeProviderUpdatedAt"] = inputs.torBridgeProviderUpdatedAt;
    out["bridgeCacheAgeSeconds"] = torStatus.value("torBridgeCacheAgeSeconds", static_cast<uint64_t>(0));
    out["allowWebClearnet"] = inputs.allowWebClearnet;
    out["allowWebOnion"] = inputs.allowWebOnion;
    out["allowP2PDiscovery"] = inputs.allowP2PDiscovery;
    out["clearnetFallbackAllowed"] = inputs.clearnetFallbackAllowed;
    out["p2pFallbackAllowed"] = inputs.p2pFallbackAllowed;
    out["failClosedActive"] = inputs.torRequired && (!inputs.allowP2PDiscovery || !inputs.torReadyForWeb);
    return out;
}

json observatoryEntryToJson(const core::ObservatoryEntry& entry, const NaanRedactFn& redact) {
    json out;
    out["hash"] = crypto::toHex(entry.hash);
    out["roomId"] = entry.roomId;
    out["type"] = core::roomMessageTypeToString(entry.type);
    out["author"] = crypto::toHex(entry.author);
    out["payloadPreview"] = maybeRedact(redact, entry.payloadPreview);
    out["timestamp"] = entry.timestamp;
    return out;
}

json signedArtifactToJson(
    const core::SignedArtifact& artifact,
    bool includePayload,
    const NaanRedactFn& redact) {
    json out;
    out["hash"] = crypto::toHex(artifact.hash);
    out["insertedAt"] = artifact.insertedAt;

    json msg;
    msg["sequence"] = artifact.message.sequence;
    msg["timestamp"] = artifact.message.timestamp;
    msg["type"] = core::roomMessageTypeToString(artifact.message.type);
    msg["author"] = crypto::toHex(artifact.message.author);
    msg["roomId"] = artifact.message.roomId;
    msg["signature"] = crypto::toHex(artifact.message.signature);
    if (includePayload) {
        msg["payload"] = maybeRedact(redact, artifact.message.payload);
    } else {
        msg["payloadBytes"] = artifact.message.payload.size();
    }
    out["message"] = std::move(msg);
    return out;
}

json draftProposalToJson(
    const core::AgentDraftProposal& proposal,
    bool includeBody,
    const NaanRedactFn& redact) {
    json out;
    out["version"] = proposal.version;
    out["draftId"] = crypto::toHex(proposal.draftId());
    out["contentHash"] = crypto::toHex(proposal.contentHash());
    out["createdAt"] = proposal.createdAt;
    out["author"] = crypto::toHex(proposal.author);
    out["title"] = maybeRedact(redact, proposal.title);
    out["powBits"] = proposal.powBits;
    out["powNonce"] = proposal.powNonce;
    out["signature"] = crypto::toHex(proposal.signature);

    json citations = json::array();
    for (const auto& citation : proposal.citations) {
        citations.push_back(crypto::toHex(citation));
    }
    out["citations"] = std::move(citations);

    if (includeBody) {
        out["body"] = maybeRedact(redact, proposal.body);
    } else {
        out["bodyBytes"] = proposal.body.size();
    }
    return out;
}

json draftRecordToJson(
    const core::AgentDraftRecord& record,
    bool includeBody,
    const NaanRedactFn& redact) {
    json out;
    out["proposal"] = draftProposalToJson(record.proposal, includeBody, redact);
    out["status"] = core::draftStatusToString(record.status);
    out["rejectReason"] = maybeRedact(redact, record.rejectReason);
    out["reviewCount"] = record.reviews.size();
    json reviews = json::array();
    for (const auto& review : record.reviews) {
        json item;
        item["reviewer"] = crypto::toHex(review.reviewer);
        item["approved"] = review.approved;
        item["reviewedAt"] = review.reviewedAt;
        item["reason"] = maybeRedact(redact, review.reason);
        reviews.push_back(std::move(item));
    }
    out["reviews"] = std::move(reviews);
    out["updatedAt"] = record.updatedAt;
    return out;
}

json draftDryRunToJson(const core::AgentSubmissionDryRunResult& result, const NaanRedactFn& redact) {
    json out;
    out["draftId"] = crypto::toHex(result.draftId);
    out["ok"] = result.ok;
    out["reason"] = maybeRedact(redact, result.reason);
    out["nextStatus"] = result.nextStatus;
    if (result.ok) {
        out["submitId"] = crypto::toHex(result.entry.submitId());
        out["contentId"] = crypto::toHex(result.entry.contentId());
    }
    return out;
}

json draftBatchItemToJson(const core::AgentSubmissionBatchItem& result, const NaanRedactFn& redact) {
    json out;
    out["draftId"] = crypto::toHex(result.draftId);
    out["ok"] = result.ok;
    out["action"] = result.action;
    out["reason"] = maybeRedact(redact, result.reason);
    if (result.submitId != crypto::Hash256{}) {
        out["submitId"] = crypto::toHex(result.submitId);
    }
    if (result.contentId != crypto::Hash256{}) {
        out["contentId"] = crypto::toHex(result.contentId);
    }
    out["expectedAcceptanceRewardAtoms"] = result.expectedAcceptanceRewardAtoms;
    out["acceptanceRewardCredited"] = result.acceptanceRewardCredited;
    return out;
}

} // namespace

std::string buildRpcNaanObservatoryArtifactsResponse(
    const std::vector<core::ObservatoryEntry>& entries,
    const NaanRedactFn& redact) {
    json out;
    out["status"] = "ok";
    out["count"] = entries.size();
    out["items"] = json::array();
    for (const auto& entry : entries) {
        out["items"].push_back(observatoryEntryToJson(entry, redact));
    }
    return out.dump();
}

std::string buildRpcNaanObservatoryArtifactResponse(
    const core::SignedArtifact& artifact,
    bool includePayload,
    const NaanRedactFn& redact) {
    json out = signedArtifactToJson(artifact, includePayload, redact);
    out["status"] = "ok";
    return out.dump();
}

std::string buildRpcNaanDraftsResponse(
    const std::vector<core::AgentDraftRecord>& records,
    bool includeBody,
    const NaanRedactFn& redact) {
    json out;
    out["status"] = "ok";
    out["count"] = records.size();
    out["items"] = json::array();
    for (const auto& record : records) {
        out["items"].push_back(draftRecordToJson(record, includeBody, redact));
    }
    return out.dump();
}

std::string buildRpcNaanDraftResponse(
    const core::AgentDraftRecord& record,
    bool includeBody,
    const NaanRedactFn& redact) {
    json out = draftRecordToJson(record, includeBody, redact);
    out["status"] = "ok";
    return out.dump();
}

std::string buildRpcNaanPipelineDryRunResponse(
    const RpcNaanPipelineDryRunInputs& inputs,
    const NaanRedactFn& redact) {
    json out;
    out["status"] = "ok";
    out["count"] = inputs.items.size();
    out["items"] = json::array();
    for (const auto& item : inputs.items) {
        out["items"].push_back(draftDryRunToJson(item, redact));
    }
    out["agentScore"] = inputs.agentScore;
    out["agentThrottled"] = inputs.agentThrottled;
    out["agentQuarantined"] = inputs.agentQuarantined;
    out["pipelineBatchLimit"] = inputs.pipelineBatchLimit;
    out["schedulerState"] = inputs.schedulerState;
    out["draftIntervalSec"] = inputs.draftIntervalSec;
    out["pipelineIntervalSec"] = inputs.pipelineIntervalSec;
    out["heartbeatIntervalSec"] = inputs.heartbeatIntervalSec;
    return out.dump();
}

std::string buildRpcNaanPipelineDrainResponse(
    const RpcNaanPipelineDrainInputs& inputs,
    const NaanRedactFn& redact) {
    json out;
    out["status"] = "ok";
    out["count"] = inputs.items.size();
    if (inputs.requestedLimitSet) {
        out["requestedLimit"] = inputs.requestedLimit;
    } else {
        out["requestedLimit"] = nullptr;
    }
    out["effectiveLimit"] = inputs.effectiveLimit;
    out["skipped"] = inputs.skippedQuarantine ? "quarantined" : "no";
    out["items"] = json::array();
    for (const auto& item : inputs.items) {
        out["items"].push_back(draftBatchItemToJson(item, redact));
    }
    out["pipelineRuns"] = inputs.pipelineRuns;
    out["pipelineApproved"] = inputs.pipelineApproved;
    out["pipelineSubmitted"] = inputs.pipelineSubmitted;
    out["pipelineRejected"] = inputs.pipelineRejected;
    out["lastPipelineAt"] = inputs.lastPipelineAt;
    out["agentScore"] = inputs.agentScore;
    out["agentThrottled"] = inputs.agentThrottled;
    out["agentQuarantined"] = inputs.agentQuarantined;
    out["pipelineBatchLimit"] = inputs.pipelineBatchLimit;
    out["schedulerState"] = inputs.schedulerState;
    out["draftIntervalSec"] = inputs.draftIntervalSec;
    out["pipelineIntervalSec"] = inputs.pipelineIntervalSec;
    out["heartbeatIntervalSec"] = inputs.heartbeatIntervalSec;
    return out.dump();
}

std::string buildRpcNaanStatusResponse(const RpcNaanStatusInputs& inputs) {
    json out;
    out["status"] = "ok";
    out["agentId"] = inputs.agentId;
    out["runtimeState"] = inputs.runtimeState;
    out["runtimeInitialized"] = inputs.runtimeInitialized;
    out["runtimeStartedAt"] = inputs.runtimeStartedAt;
    out["runtimeCrashCount"] = inputs.runtimeCrashCount;
    out["runtimeConsecutiveCrashes"] = inputs.runtimeConsecutiveCrashes;
    out["runtimeLastCrashAt"] = inputs.runtimeLastCrashAt;
    out["runtimeRecoveryUntil"] = inputs.runtimeRecoveryUntil;
    out["runtimeRecoverySkips"] = inputs.runtimeRecoverySkips;
    out["schedulerTick"] = inputs.schedulerTick;
    out["schedulerEpochStartTick"] = inputs.schedulerEpochStartTick;
    out["schedulerEpochIndex"] = inputs.schedulerEpochIndex;
    out["schedulerRoundRobinCursor"] = inputs.schedulerRoundRobinCursor;
    out["schedulerBudgetRemaining"] = {
        {"cpu", inputs.schedulerBudgetRemaining.cpu},
        {"ram", inputs.schedulerBudgetRemaining.ram},
        {"network", inputs.schedulerBudgetRemaining.network}
    };
    out["humanWritable"] = inputs.humanWritable;
    out["rooms"] = {
        {"tasks", stringArray(inputs.rooms.tasks)},
        {"reviews", stringArray(inputs.rooms.reviews)},
        {"disputes", stringArray(inputs.rooms.disputes)},
        {"alerts", stringArray(inputs.rooms.alerts)}
    };
    out["totalMessages"] = inputs.totalMessages;
    out["draftQueueSize"] = inputs.draftQueueSize;
    out["queuedDrafts"] = inputs.queuedDrafts;
    out["reviewDrafts"] = inputs.reviewDrafts;
    out["approvedDrafts"] = inputs.approvedDrafts;
    out["rejectedDrafts"] = inputs.rejectedDrafts;
    out["submittedDrafts"] = inputs.submittedDrafts;
    out["pipelineRuns"] = inputs.pipelineRuns;
    out["pipelineApproved"] = inputs.pipelineApproved;
    out["pipelineSubmitted"] = inputs.pipelineSubmitted;
    out["pipelineRejected"] = inputs.pipelineRejected;
    out["lastPipelineAt"] = inputs.lastPipelineAt;
    out["agentScore"] = inputs.agentScore;
    out["agentScoreBand"] = inputs.agentScoreBand;
    out["agentThrottled"] = inputs.agentThrottled;
    out["agentReviewOnly"] = inputs.agentReviewOnly;
    out["agentLocalDraftOnly"] = inputs.agentLocalDraftOnly;
    out["agentQuarantined"] = inputs.agentQuarantined;
    out["agentQuarantineReason"] = inputs.agentQuarantineReason;
    out["agentQuarantineReasonSince"] = inputs.agentQuarantineReasonSince;
    out["pipelineBatchLimit"] = inputs.pipelineBatchLimit;
    out["reviewDiversityMinDistinctReviewers"] = inputs.pipelineConfig.minDistinctReviewers;
    out["reviewDiversityRequireNonAuthor"] = inputs.pipelineConfig.requireNonAuthorReviewer;
    out["reviewDiversityIncludeReviewRequired"] = inputs.pipelineConfig.includeReviewRequired;
    out["duplicateGateContentId"] = inputs.pipelineConfig.duplicateGateContentId;
    out["duplicateGateNoveltyBuckets"] = inputs.pipelineConfig.duplicateGateNoveltyBuckets;
    out["duplicateGateCitationGraph"] = inputs.pipelineConfig.duplicateGateCitationGraph;
    out["duplicateGateCitationGraphMaxHamming"] = inputs.pipelineConfig.duplicateCitationGraphMaxHamming;
    out["citationSanityMaxCitations"] = inputs.pipelineConfig.citationSanityMaxCitations;
    out["citationSanityRejectDuplicateCitations"] = inputs.pipelineConfig.citationSanityRejectDuplicateCitations;
    out["citationSanityRequireKnownCitations"] = inputs.pipelineConfig.citationSanityRequireKnownCitations;
    out["citationSanityRejectSelfReference"] = inputs.pipelineConfig.citationSanityRejectSelfReference;
    out["citationSanityRejectIntraSetCycles"] = inputs.pipelineConfig.citationSanityRejectIntraSetCycles;
    out["citationSanityMaxIntraSetEdges"] = inputs.pipelineConfig.citationSanityMaxIntraSetEdges;
    out["citationSanityMinCorroboratingCitations"] = inputs.pipelineConfig.citationSanityMinCorroboratingCitations;
    out["citationSanityMinDistinctAuthors"] =
        inputs.pipelineConfig.citationSanityMinDistinctCitationAuthors;
    out["schedulerState"] = inputs.schedulerState;
    out["draftIntervalSec"] = inputs.draftIntervalSec;
    out["pipelineIntervalSec"] = inputs.pipelineIntervalSec;
    out["heartbeatIntervalSec"] = inputs.heartbeatIntervalSec;
    out["agentScoreSteps"] = inputs.agentScoreSteps;
    out["agentScoreCleanSteps"] = inputs.agentScoreCleanSteps;
    out["agentAcceptedTotal"] = inputs.agentAcceptedTotal;
    out["agentRejectedTotal"] = inputs.agentRejectedTotal;
    out["agentViolationsTotal"] = inputs.agentViolationsTotal;
    out["agentScoreDecayIntervalSec"] = inputs.agentScoreDecayIntervalSec;
    out["agentScoreLastDecayAt"] = inputs.agentScoreLastDecayAt;
    out["agentScoreLastViolationTick"] = inputs.agentScoreLastViolationTick;
    out["agentScoreBandTransitions"] = inputs.agentScoreBandTransitions;
    out["agentQuarantineRecoveries"] = inputs.agentQuarantineRecoveries;
    out["agentQuarantineRecoveryLastAt"] = inputs.agentQuarantineRecoveryLastAt;
    out["agentAbuseSpamPenalty"] = inputs.agentAbuseSpamPenalty;
    out["agentAbuseCitationPenalty"] = inputs.agentAbuseCitationPenalty;
    out["agentAbusePolicyPenalty"] = inputs.agentAbusePolicyPenalty;
    out["policyHashNaan"] = inputs.policyHashNaan;
    out["policyHashImplant"] = inputs.policyHashImplant;
    out["securityEvents"] = inputs.securityEvents;
    out["securityHighSeverityEvents"] = inputs.securityHighSeverityEvents;
    out["securityLastEventAt"] = inputs.securityLastEventAt;
    out["storageRoot"] = inputs.storage.root;
    out["storageAuditSegments"] = inputs.storage.auditSegments;
    out["storageAuditRetainedEvents"] = inputs.storage.auditRetainedEvents;
    out["storageAuditLastSeq"] = inputs.storage.auditLastSeq;
    out["storageAuditLastHash"] = inputs.storage.auditLastHash;
    out["storageAuditRecoveredLines"] = inputs.storage.auditRecoveredLines;
    out["storageAuditDroppedSegments"] = inputs.storage.auditDroppedSegments;
    out["storageIndexRecoveryRuns"] = inputs.storage.indexRecoveryRuns;
    out["storageIndexRecoveryLastAt"] = inputs.storage.indexRecoveryLastAt;
    out["storageConsistencyChecks"] = inputs.storage.consistencyChecks;
    out["storageConsistencyRepairs"] = inputs.storage.consistencyRepairs;
    out["storageConsistencyLastAt"] = inputs.storage.consistencyLastAt;
    out["connectorAutoQuarantineEvents"] = inputs.connectorAutoQuarantineEvents;
    out["connectorAutoQuarantineLastAt"] = inputs.connectorAutoQuarantineLastAt;
    out["connectorAutoQuarantineLastPolicyDelta"] = inputs.connectorAutoQuarantineLastPolicyDelta;
    out["connectorAutoQuarantineLastFailureDelta"] = inputs.connectorAutoQuarantineLastFailureDelta;
    out["connectorAutoQuarantineLastViolations"] = inputs.connectorAutoQuarantineLastViolations;
    out["taskRunsResearch"] = inputs.taskRunsResearch;
    out["taskRunsVerify"] = inputs.taskRunsVerify;
    out["taskRunsReview"] = inputs.taskRunsReview;
    out["taskRunsDraft"] = inputs.taskRunsDraft;
    out["taskRunsSubmit"] = inputs.taskRunsSubmit;
    out["lastResearchAt"] = inputs.lastResearchAt;
    out["lastVerifyAt"] = inputs.lastVerifyAt;
    out["lastReviewAt"] = inputs.lastReviewAt;
    out["lastActionAt"] = inputs.lastActionAt;
    out["lastHeartbeatAt"] = inputs.lastHeartbeatAt;
    out["lastDraftAt"] = inputs.lastDraftAt;
    out["ticks"] = inputs.ticks;

    json activeEngines;
    activeEngines["clearnet"] = searchEngineArray(inputs.connector.config.clearnetEngines);
    activeEngines["darknet"] = searchEngineArray(inputs.connector.config.darknetEngines);
    activeEngines["customClearnetCount"] = inputs.connector.config.customClearnetUrls.size();
    activeEngines["customDarknetCount"] = inputs.connector.config.customDarknetUrls.size();
    activeEngines["directOnionCount"] = inputs.connector.config.directOnionLinks.size();

    json fetchCounts;
    fetchCounts["totalSearches"] = inputs.connector.stats.totalSearches;
    fetchCounts["clearnetSearches"] = inputs.connector.stats.clearnetSearches;
    fetchCounts["darknetSearches"] = inputs.connector.stats.darknetSearches;
    fetchCounts["successfulFetches"] = inputs.connector.stats.successfulFetches;
    fetchCounts["failedFetches"] = inputs.connector.stats.failedFetches;
    fetchCounts["pagesExtracted"] = inputs.connector.stats.pagesExtracted;
    fetchCounts["bytesDownloaded"] = inputs.connector.stats.bytesDownloaded;
    fetchCounts["avgResponseTime"] = inputs.connector.stats.avgResponseTime;
    fetchCounts["routePolicyChecks"] = inputs.connector.stats.routePolicyChecks;
    fetchCounts["routePolicyPassed"] = inputs.connector.stats.routePolicyPassed;
    fetchCounts["routePolicyBlocked"] = inputs.connector.stats.routePolicyBlocked;
    if (inputs.connector.stats.routePolicyChecks == 0) {
        fetchCounts["routePolicyPassRate"] = 0.0;
    } else {
        fetchCounts["routePolicyPassRate"] =
            static_cast<double>(inputs.connector.stats.routePolicyPassed) /
            static_cast<double>(inputs.connector.stats.routePolicyChecks);
    }

    json policyBlocks;
    policyBlocks["clearnet"] = inputs.connector.health.clearnet.policyBlocks;
    policyBlocks["tor"] = inputs.connector.health.tor.policyBlocks;
    policyBlocks["onion"] = inputs.connector.health.onion.policyBlocks;
    policyBlocks["total"] =
        inputs.connector.health.clearnet.policyBlocks +
        inputs.connector.health.tor.policyBlocks +
        inputs.connector.health.onion.policyBlocks;

    json configValidation;
    configValidation["totalLines"] = inputs.connector.validation.totalLines;
    configValidation["appliedLines"] = inputs.connector.validation.appliedLines;
    configValidation["invalidLines"] = inputs.connector.validation.invalidLines;
    configValidation["unknownKeys"] = inputs.connector.validation.unknownKeys;
    configValidation["sanitizedWrites"] = inputs.connector.validation.sanitizedWrites;
    configValidation["unknownKeySamples"] = stringArray(inputs.connector.validation.unknownKeySamples);

    const json torStatus = parseJsonObjectOrEmpty(buildRpcNodeTorStatusJson(inputs.tor));
    out["torRequired"] = inputs.tor.torRequired;
    out["torSocksReachable"] = inputs.tor.torSocksReachable;
    out["torReachable"] = inputs.tor.torSocksReachable;
    out["torReadyForWeb"] = inputs.tor.torReadyForWeb;
    out["torManaged"] = inputs.tor.torManaged;
    out["torManagedPid"] = inputs.tor.torManagedPid;
    out["torRuntimeMode"] = inputs.tor.torRuntimeMode;
    out["torSocksPort"] = inputs.tor.torSocksPort;
    out["torControlPort"] = inputs.tor.torControlPort;
    out["torControlReachable"] = inputs.tor.torControlReachable;
    out["onionAddress"] = inputs.tor.onionAddress;
    out["torSeedAddress"] = inputs.tor.torSeedAddress;
    out["torConflictHint9050"] = inputs.tor.torConflictHint9050;
    out["torBootstrapState"] = torStatus.value("torBootstrapState", std::string("offline"));
    out["torBootstrapPercent"] = torStatus.value("torBootstrapPercent", 0U);
    out["torReadyForOnion"] = torStatus.value("torReadyForOnion", false);
    out["torReadyForOnionService"] = torStatus.value("torReadyForOnionService", false);
    out["torOnionServiceActive"] = inputs.tor.onionServiceActive;
    out["torOnionServiceState"] = torStatus.value("torOnionServiceState", std::string("offline"));
    out["torBootstrapReasonCode"] = torStatus.value("torBootstrapReasonCode", std::string());
    out["torLastBootstrapError"] = torStatus.value("torLastBootstrapError", std::string());
    out["torDegraded"] = inputs.tor.torDegraded;
    out["torWebProbeLastAt"] = inputs.tor.torWebProbeLastAt;
    out["torWebProbeLastOkAt"] = inputs.tor.torWebProbeLastOkAt;
    out["torWebProbeExitCode"] = inputs.tor.torWebProbeExitCode;
    out["torWebProbeLastError"] = inputs.tor.torWebProbeLastError;
    out["torWebProbeConsecutiveFailures"] = inputs.tor.torWebProbeConsecutiveFailures;
    out["torWebProbeConsecutiveSuccesses"] = inputs.tor.torWebProbeConsecutiveSuccesses;
    out["torBridgeSubsetPersistCount"] = inputs.tor.torBridgeSubsetPersistCount;
    out["torBridgeSubsetLastPersistAt"] = inputs.tor.torBridgeSubsetLastPersistAt;
    out["torBridgeSubsetLastEpoch"] = inputs.tor.torBridgeSubsetLastEpoch;
    out["torBridgeSubsetLastCount"] = inputs.tor.torBridgeSubsetLastCount;
    out["torBridgeSubsetPath"] = inputs.tor.torBridgeSubsetPath;
    out["torBridgeRemoteLastFetchAt"] = inputs.tor.torBridgeRemoteLastFetchAt;
    out["torBridgeRemoteFetchAttempts"] = inputs.tor.torBridgeRemoteFetchAttempts;
    out["torBridgeRemoteFetchSuccesses"] = inputs.tor.torBridgeRemoteFetchSuccesses;
    out["torBridgeRemoteRateLimitedSkips"] = inputs.tor.torBridgeRemoteRateLimitedSkips;
    out["torBridgeProvider"] = torStatus.contains("torBridgeProvider")
        ? torStatus["torBridgeProvider"]
        : json::object();
    out["torBridgeProviderUpdatedAt"] = inputs.tor.torBridgeProviderUpdatedAt;
    out["torBridgeCacheAgeSeconds"] =
        torStatus.value("torBridgeCacheAgeSeconds", static_cast<uint64_t>(0));
    out["routeMode"] = inputs.tor.routeMode;
    out["clearnetFallbackAllowed"] = inputs.tor.clearnetFallbackAllowed;
    out["p2pFallbackAllowed"] = inputs.tor.p2pFallbackAllowed;
    const json torRoute = buildTorRouteJson(inputs.tor);
    out["torRoute"] = torRoute;

    json connectorStatus;
    connectorStatus["available"] = inputs.connector.available;
    if (inputs.connector.available) {
        connectorStatus["clearnet"] = connectorHealthToJson(inputs.connector.health.clearnet);
        connectorStatus["tor"] = connectorHealthToJson(inputs.connector.health.tor);
        connectorStatus["onion"] = connectorHealthToJson(inputs.connector.health.onion);
        connectorStatus["allowlistCount"] = inputs.connector.config.fetchAllowlistRoutes.size();
        connectorStatus["denylistCount"] = inputs.connector.config.fetchDenylistRoutes.size();
        connectorStatus["clearnetSiteAllowCount"] = inputs.connector.config.clearnetSiteAllowlist.size();
        connectorStatus["clearnetSiteDenyCount"] = inputs.connector.config.clearnetSiteDenylist.size();
        connectorStatus["onionSiteAllowCount"] = inputs.connector.config.onionSiteAllowlist.size();
        connectorStatus["onionSiteDenyCount"] = inputs.connector.config.onionSiteDenylist.size();
        connectorStatus["clearnetBypassHostCount"] = inputs.connector.config.clearnetRouteBypassHosts.size();
        connectorStatus["onionBypassHostCount"] = inputs.connector.config.onionRouteBypassHosts.size();
        connectorStatus["bypassOnionHttpsFallback"] = inputs.connector.config.bypassOnionHttpsFallback;
        connectorStatus["sitePolicyPath"] = inputs.connector.sitePolicyPath;
    }
    connectorStatus["activeEngines"] = activeEngines;
    connectorStatus["fetchCounts"] = fetchCounts;
    connectorStatus["policyBlocks"] = policyBlocks;
    connectorStatus["configValidation"] = configValidation;
    connectorStatus["torRoute"] = torRoute;

    out["activeEngines"] = activeEngines;
    out["fetchCounts"] = fetchCounts;
    out["policyBlocks"] = policyBlocks;
    out["webConfigValidation"] = configValidation;
    out["webFailClosedSkips"] = inputs.webFailClosedSkips;
    out["miningFailClosedSkips"] = inputs.miningFailClosedSkips;
    out["connectorStatus"] = std::move(connectorStatus);
    out["networkHealth"] = parseJsonObjectOrEmpty(buildRpcNodeNetworkHealthJson(inputs.networkHealth));
    out["agentRewardVisibilityDeterministic"] = inputs.rewardVisibility.deterministic;
    out["agentRewardObservedSubmissions"] = inputs.rewardVisibility.observedSubmissions;
    out["agentRewardFinalizedSubmissions"] = inputs.rewardVisibility.finalizedSubmissions;
    out["agentRewardCreditedSubmissions"] = inputs.rewardVisibility.creditedSubmissions;
    out["agentRewardExpectedAtoms"] = inputs.rewardVisibility.expectedAtoms;
    out["agentRewardCreditedAtoms"] = inputs.rewardVisibility.creditedAtoms;
    out["redactionCount"] = inputs.redactionCount;
    return out.dump();
}

} // namespace synapse::rpc
