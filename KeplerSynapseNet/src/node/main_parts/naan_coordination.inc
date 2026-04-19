bool configureNaanTaskScheduler() {
    auto& cfg = utils::Config::instance();

    core::AgentTaskSchedulerPolicy policy;
    int64_t epochTicksRaw = cfg.getInt64("naan.scheduler.epoch_ticks", 300);
    if (epochTicksRaw < 1) epochTicksRaw = 1;
    if (epochTicksRaw > 86400) epochTicksRaw = 86400;
    policy.epochTicks = static_cast<uint32_t>(epochTicksRaw);

    int64_t starveRaw = cfg.getInt64("naan.scheduler.starvation_threshold_ticks", 120);
    if (starveRaw < 1) starveRaw = 1;
    if (starveRaw > 1000000) starveRaw = 1000000;
    policy.starvationThresholdTicks = static_cast<uint32_t>(starveRaw);

    auto readBudget = [&](const char* key, uint32_t fallback) {
        int64_t v = cfg.getInt64(key, static_cast<int64_t>(fallback));
        if (v < 1) v = 1;
        if (v > 1000000000LL) v = 1000000000LL;
        return static_cast<uint32_t>(v);
    };
    policy.epochBudget.cpu = readBudget("naan.scheduler.budget.cpu_tokens", 1200);
    policy.epochBudget.ram = readBudget("naan.scheduler.budget.ram_tokens", 1200);
    policy.epochBudget.network = readBudget("naan.scheduler.budget.network_tokens", 1200);

    policy.fixedPriority = {
        core::AgentTaskClass::SUBMIT,
        core::AgentTaskClass::REVIEW,
        core::AgentTaskClass::VERIFY,
        core::AgentTaskClass::DRAFT,
        core::AgentTaskClass::RESEARCH
    };

    auto setClassPolicy = [&](core::AgentTaskClass taskClass,
                              const std::string& taskName,
                              uint32_t defCpu,
                              uint32_t defRam,
                              uint32_t defNet,
                              uint32_t defInterval) {
        const size_t idx = core::agentTaskClassIndex(taskClass);
        auto& cls = policy.classes[idx];

        auto readClass = [&](const std::string& suffix, uint32_t fallback, uint32_t maxValue) {
            int64_t v = cfg.getInt64("naan.scheduler.task." + taskName + "." + suffix, static_cast<int64_t>(fallback));
            if (v < 1) v = 1;
            if (v > static_cast<int64_t>(maxValue)) v = static_cast<int64_t>(maxValue);
            return static_cast<uint32_t>(v);
        };

        cls.cost.cpu = readClass("cpu_tokens", defCpu, policy.epochBudget.cpu);
        cls.cost.ram = readClass("ram_tokens", defRam, policy.epochBudget.ram);
        cls.cost.network = readClass("network_tokens", defNet, policy.epochBudget.network);
        cls.minIntervalTicks = readClass("min_interval_ticks", defInterval, 1000000);
    };

    setClassPolicy(core::AgentTaskClass::RESEARCH, "research", 30, 16, 20, 15);
    setClassPolicy(core::AgentTaskClass::VERIFY, "verify", 20, 12, 8, 10);
    setClassPolicy(core::AgentTaskClass::REVIEW, "review", 18, 10, 8, 10);
    setClassPolicy(core::AgentTaskClass::DRAFT, "draft", 40, 24, 16, 15);
    setClassPolicy(core::AgentTaskClass::SUBMIT, "submit", 45, 28, 18, 8);

    naanTaskScheduler_.setPolicy(policy);
    naanSchedulerStatePath_ = config_.dataDir + "/naan/scheduler.state";

    std::string loadReason;
    if (!naanTaskScheduler_.loadCheckpoint(naanSchedulerStatePath_, &loadReason)) {
        utils::Logger::error("Failed to load NAAN scheduler checkpoint: " + loadReason);
        return false;
    }
    if (loadReason == "not_found") {
        (void)persistNaanSchedulerState("scheduler_seed");
    }
    return true;
}

void configureNaanConnectorAbuseGuard() {
    auto& cfg = utils::Config::instance();
    web::ConnectorAbusePolicy policy;

    auto readPolicy = [&](const char* key, int64_t fallback, int64_t minValue, int64_t maxValue) {
        int64_t v = cfg.getInt64(key, fallback);
        if (v < minValue) v = minValue;
        if (v > maxValue) v = maxValue;
        return static_cast<uint32_t>(v);
    };

    policy.policyBlockDeltaThreshold = readPolicy(
        "naan.connector_abuse.policy_block_delta_threshold", 12, 1, 1000000);
    policy.failureDeltaThreshold = readPolicy(
        "naan.connector_abuse.failure_delta_threshold", 60, 1, 1000000);
    policy.cooldownTicks = readPolicy(
        "naan.connector_abuse.cooldown_ticks", 30, 1, 1000000);
    policy.violationPenaltySteps = readPolicy(
        "naan.connector_abuse.violation_penalty_steps", 1, 1, 16);

    naanConnectorAbuseGuard_.setPolicy(policy);
}

void configureNaanScorePolicy() {
    auto& cfg = utils::Config::instance();

    auto readBoundedI64 = [&](const char* key, int64_t fallback, int64_t minValue, int64_t maxValue) {
        int64_t v = cfg.getInt64(key, fallback);
        if (v < minValue) v = minValue;
        if (v > maxValue) v = maxValue;
        return v;
    };
    auto readBoundedU32 = [&](const char* key, int64_t fallback, int64_t minValue, int64_t maxValue) {
        int64_t v = cfg.getInt64(key, fallback);
        if (v < minValue) v = minValue;
        if (v > maxValue) v = maxValue;
        return static_cast<uint32_t>(v);
    };

    core::AgentScorePolicy policy = agentScore_.policy();
    policy.scoreMin = readBoundedI64("naan.score.min", -1000, -1000000, 0);
    policy.scoreMax = readBoundedI64("naan.score.max", 1000, 0, 1000000);
    if (policy.scoreMax < policy.scoreMin) std::swap(policy.scoreMax, policy.scoreMin);
    policy.initialScore = readBoundedI64("naan.score.initial", 100, policy.scoreMin, policy.scoreMax);

    policy.decayNumerator = readBoundedU32("naan.score.decay_numerator", 95, 0, 1000000);
    policy.decayDenominator = readBoundedU32("naan.score.decay_denominator", 100, 1, 1000000);
    if (policy.decayNumerator > policy.decayDenominator) {
        policy.decayNumerator = policy.decayDenominator;
    }

    policy.acceptWeight = readBoundedI64("naan.score.accept_weight", 12, 0, 1000000);
    policy.rejectWeight = readBoundedI64("naan.score.reject_weight", 12, 0, 1000000);
    policy.violationWeight = readBoundedI64("naan.score.violation_weight", 40, 0, 1000000);

    policy.throttledBelowOrEqual = readBoundedI64(
        "naan.score.band.throttled_below_or_equal", -150, policy.scoreMin, policy.scoreMax);
    policy.reviewOnlyBelowOrEqual = readBoundedI64(
        "naan.score.band.review_only_below_or_equal", -280, policy.scoreMin, policy.scoreMax);
    policy.localDraftOnlyBelowOrEqual = readBoundedI64(
        "naan.score.band.local_draft_only_below_or_equal", -420, policy.scoreMin, policy.scoreMax);
    policy.localDraftRecoveryAbove = readBoundedI64(
        "naan.score.band.local_draft_recovery_above", -220, policy.scoreMin, policy.scoreMax);
    policy.localDraftRecoveryCleanSteps = readBoundedU32(
        "naan.score.band.local_draft_recovery_clean_steps", 2, 0, 1000000);

    policy.normalBatchLimit = readBoundedU32("naan.score.batch_limit.full", 16, 0, 500);
    policy.throttledBatchLimit = readBoundedU32("naan.score.batch_limit.throttled", 4, 0, 500);
    policy.reviewOnlyBatchLimit = readBoundedU32("naan.score.batch_limit.review_only", 0, 0, 500);
    policy.localDraftOnlyBatchLimit = readBoundedU32("naan.score.batch_limit.local_draft_only", 0, 0, 500);
    agentScore_.setPolicy(policy);

    naanScoreDecayIntervalSeconds_.store(readBoundedU32(
        "naan.score.decay_interval_seconds", 60, 0, 86400));

    naanAbuseSpamPenalty_.store(readBoundedU32(
        "naan.abuse_classifier.spam_loop_penalty", 1, 0, 64));
    naanAbuseCitationPenalty_.store(readBoundedU32(
        "naan.abuse_classifier.invalid_citation_penalty", 1, 0, 64));
    naanAbusePolicyPenalty_.store(readBoundedU32(
        "naan.abuse_classifier.policy_violation_penalty", 1, 0, 64));

    naanScoreStatePath_ = config_.dataDir + "/naan/score.state";
    naanScoreDecayStatePath_ = config_.dataDir + "/naan/score_decay.state";
}

void configureNaanAdaptiveScheduler() {
    auto& cfg = utils::Config::instance();
    auto policy = agentAdaptiveScheduler_.policy();

    auto readBoundedU32 = [&](const char* key, int64_t fallback, int64_t minValue, int64_t maxValue) {
        int64_t v = cfg.getInt64(key, fallback);
        if (v < minValue) v = minValue;
        if (v > maxValue) v = maxValue;
        return static_cast<uint32_t>(v);
    };

    policy.draftIntervalNormalSeconds = readBoundedU32(
        "naan.scheduler.adaptive.draft_interval_normal_seconds",
        policy.draftIntervalNormalSeconds, 1, 86400);
    policy.draftIntervalThrottledSeconds = readBoundedU32(
        "naan.scheduler.adaptive.draft_interval_throttled_seconds",
        policy.draftIntervalThrottledSeconds, 1, 86400);
    policy.draftIntervalQuarantinedSeconds = readBoundedU32(
        "naan.scheduler.adaptive.draft_interval_quarantined_seconds",
        policy.draftIntervalQuarantinedSeconds, 1, 86400);

    policy.pipelineIntervalNormalSeconds = readBoundedU32(
        "naan.scheduler.adaptive.pipeline_interval_normal_seconds",
        policy.pipelineIntervalNormalSeconds, 1, 86400);
    policy.pipelineIntervalThrottledSeconds = readBoundedU32(
        "naan.scheduler.adaptive.pipeline_interval_throttled_seconds",
        policy.pipelineIntervalThrottledSeconds, 1, 86400);
    policy.pipelineIntervalQuarantinedSeconds = readBoundedU32(
        "naan.scheduler.adaptive.pipeline_interval_quarantined_seconds",
        policy.pipelineIntervalQuarantinedSeconds, 1, 86400);

    policy.heartbeatIntervalNormalSeconds = readBoundedU32(
        "naan.scheduler.adaptive.heartbeat_interval_normal_seconds",
        policy.heartbeatIntervalNormalSeconds, 1, 86400);
    policy.heartbeatIntervalThrottledSeconds = readBoundedU32(
        "naan.scheduler.adaptive.heartbeat_interval_throttled_seconds",
        policy.heartbeatIntervalThrottledSeconds, 1, 86400);
    policy.heartbeatIntervalQuarantinedSeconds = readBoundedU32(
        "naan.scheduler.adaptive.heartbeat_interval_quarantined_seconds",
        policy.heartbeatIntervalQuarantinedSeconds, 1, 86400);

    agentAdaptiveScheduler_.setPolicy(policy);
}

void configureNaanSubmissionPipeline() {
    auto& cfg = utils::Config::instance();
    core::AgentSubmissionPipelineConfig policy = agentSubmissionPipeline_.config();

    auto readBoundedU32 = [&](const char* key, int64_t fallback, int64_t minValue, int64_t maxValue) {
        int64_t v = cfg.getInt64(key, fallback);
        if (v < minValue) v = minValue;
        if (v > maxValue) v = maxValue;
        return static_cast<uint32_t>(v);
    };

    int64_t minReviewersRaw = cfg.getInt64("naan.reviewer_diversity.min_distinct_reviewers", 1);
    if (minReviewersRaw < 0) minReviewersRaw = 0;
    if (minReviewersRaw > 64) minReviewersRaw = 64;
    policy.minDistinctReviewers = static_cast<uint32_t>(minReviewersRaw);

    policy.requireNonAuthorReviewer = cfg.getBool("naan.reviewer_diversity.require_non_author", false);
    policy.includeReviewRequired = cfg.getBool("naan.reviewer_diversity.include_review_required", true);
    policy.intentMinTitleBytes = readBoundedU32(
        "agent.intent.min_title_bytes", 4, 1, 512);
    policy.intentMinBodyBytes = readBoundedU32(
        "agent.intent.min_body_bytes", 24, 1, 65536);
    policy.intentRequireCitationForText = cfg.getBool(
        "agent.intent.require_citation_for_text", false);
    policy.intentMinCitationsForText = readBoundedU32(
        "agent.intent.min_citations_for_text", 0, 0, 1024);

    policy.duplicateGateContentId = cfg.getBool("naan.duplicate_gates.content_id", true);
    policy.duplicateGateNoveltyBuckets = cfg.getBool("naan.duplicate_gates.novelty_buckets", true);
    policy.duplicateGateCitationGraph = cfg.getBool("naan.duplicate_gates.citation_graph", true);
    policy.duplicateCitationGraphMaxHamming = readBoundedU32(
        "naan.duplicate_gates.citation_graph_max_hamming", 24, 0, 64);

    policy.citationSanityMaxCitations = readBoundedU32(
        "naan.citation_sanity.max_citations", 64, 1, 1024);
    policy.citationSanityRejectDuplicateCitations = cfg.getBool(
        "naan.citation_sanity.reject_duplicate_citations", true);
    policy.citationSanityRequireKnownCitations = cfg.getBool(
        "naan.citation_sanity.require_known_citations", true);
    policy.citationSanityRejectSelfReference = cfg.getBool(
        "naan.citation_sanity.reject_self_reference", true);
    policy.citationSanityRejectIntraSetCycles = cfg.getBool(
        "naan.citation_sanity.reject_intra_set_cycles", true);
    policy.citationSanityMaxIntraSetEdges = readBoundedU32(
        "naan.citation_sanity.max_intra_set_edges", 8, 0, 1000000);
    policy.citationSanityMinCorroboratingCitations = readBoundedU32(
        "naan.citation_sanity.min_corroborating_citations", 0, 0, 1024);
    policy.citationSanityMinDistinctCitationAuthors = readBoundedU32(
        "naan.citation_sanity.min_distinct_authors", 0, 0, 1024);

    agentSubmissionPipeline_ = core::AgentSubmissionPipeline(policy);
}

uint32_t classifyNaanBatchViolations(const std::vector<core::AgentSubmissionBatchItem>& batch) const {
    uint64_t spamEvents = 0;
    uint64_t citationEvents = 0;
    uint64_t policyEvents = 0;
    for (const auto& item : batch) {
        if (item.action == "submit_error") {
            policyEvents += 1;
            continue;
        }
        if (item.action != "rejected") continue;

        if (hasPrefix(item.reason, "citation_")) {
            citationEvents += 1;
        } else if (hasPrefix(item.reason, "duplicate_gate_") ||
                   item.reason == "review_diversity_insufficient") {
            spamEvents += 1;
        } else if (hasPrefix(item.reason, "submit_") ||
                   hasPrefix(item.reason, "policy_")) {
            policyEvents += 1;
        }
    }

    const uint64_t total = spamEvents * static_cast<uint64_t>(naanAbuseSpamPenalty_.load()) +
                           citationEvents * static_cast<uint64_t>(naanAbuseCitationPenalty_.load()) +
                           policyEvents * static_cast<uint64_t>(naanAbusePolicyPenalty_.load());
    if (total > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(total);
}

void applyNaanConnectorAbuseAutoAction(uint64_t now, uint64_t tickValue) {
    web::WebConnectorHealth health{};
    {
        std::lock_guard<std::mutex> lock(webMtx_);
        if (!webSearch_) return;
        health = webSearch_->getConnectorHealth();
    }

    const auto decision = naanConnectorAbuseGuard_.observe(tickValue, health);
    if (!decision.triggered) {
        return;
    }

    const uint64_t scaled = static_cast<uint64_t>(decision.violations) *
                            static_cast<uint64_t>(naanAbusePolicyPenalty_.load());
    const uint32_t violationSteps = scaled > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(scaled);
    applyNaanScoreStep(0, 0, violationSteps, now, "connector_abuse_auto");
    naanConnectorAbuseEvents_.fetch_add(1);
    naanConnectorAbuseLastAt_.store(now);
    naanConnectorAbuseLastPolicyDelta_.store(decision.deltaPolicyBlocks);
    naanConnectorAbuseLastFailureDelta_.store(decision.deltaFailures);
    naanConnectorAbuseLastViolations_.store(decision.violations);

    utils::Logger::warn("NAAN connector abuse auto-action: policy_delta=" +
                       std::to_string(decision.deltaPolicyBlocks) +
                       " failure_delta=" + std::to_string(decision.deltaFailures) +
                       " violations=" + std::to_string(decision.violations));
    json details;
    details["policyDelta"] = decision.deltaPolicyBlocks;
    details["failureDelta"] = decision.deltaFailures;
    details["violations"] = decision.violations;
    details["penaltySteps"] = violationSteps;
    emitSecurityEvent(now, "connector_abuse_auto_action", "high", "connector_health", details);
}

bool hasPendingNaanDrafts() const {
    if (!agentDraftQueue_.listByStatus(core::DraftStatus::QUEUED, 1).empty()) return true;
    if (!agentDraftQueue_.listByStatus(core::DraftStatus::REVIEW_REQUIRED, 1).empty()) return true;
    if (!agentDraftQueue_.listByStatus(core::DraftStatus::APPROVED, 1).empty()) return true;
    return false;
}

void reconcileNaanDraftQueueWithLedger(uint64_t nowTimestamp) {
    if (!poeV1_ || !attachedAgentIdentity_.valid()) return;

    std::vector<core::AgentDraftRecord> candidates;
    auto queued = agentDraftQueue_.listByStatus(core::DraftStatus::QUEUED, 1000000);
    auto review = agentDraftQueue_.listByStatus(core::DraftStatus::REVIEW_REQUIRED, 1000000);
    auto approved = agentDraftQueue_.listByStatus(core::DraftStatus::APPROVED, 1000000);
    candidates.reserve(queued.size() + review.size() + approved.size());
    candidates.insert(candidates.end(), queued.begin(), queued.end());
    candidates.insert(candidates.end(), review.begin(), review.end());
    candidates.insert(candidates.end(), approved.begin(), approved.end());

    std::sort(candidates.begin(), candidates.end(), [](const core::AgentDraftRecord& a, const core::AgentDraftRecord& b) {
        const std::string ah = crypto::toHex(a.proposal.draftId());
        const std::string bh = crypto::toHex(b.proposal.draftId());
        if (ah != bh) return ah < bh;
        return a.proposal.createdAt < b.proposal.createdAt;
    });

    uint32_t checked = 0;
    uint32_t repaired = 0;
    for (const auto& record : candidates) {
        core::poe_v1::KnowledgeEntryV1 entry;
        std::string convertReason;
        if (!agentSubmissionPipeline_.convertDraftToEntry(
                record.proposal,
                attachedAgentIdentity_.privateKey,
                &entry,
                &convertReason)) {
            continue;
        }

        checked += 1;
        if (!poeV1_->getSubmitIdByContentId(entry.contentId()).has_value()) {
            continue;
        }

        std::string statusReason;
        if (record.status == core::DraftStatus::QUEUED ||
            record.status == core::DraftStatus::REVIEW_REQUIRED) {
            if (!agentDraftQueue_.setStatus(
                    record.proposal.draftId(),
                    core::DraftStatus::APPROVED,
                    nowTimestamp,
                    "ledger_crosscheck",
                    &statusReason)) {
                continue;
            }
        }

        if (agentDraftQueue_.markSubmitted(record.proposal.draftId(), nowTimestamp, &statusReason)) {
            repaired += 1;
        }
    }

    if (checked > 0 || repaired > 0) {
        json payload;
        payload["checked"] = checked;
        payload["repaired"] = repaired;
        appendNaanAuditEvent(nowTimestamp, "draft_queue_ledger_crosscheck", "draft_queue", payload);
    }
}

void reconcileNaanReviewArtifactsWithDraftQueue(uint64_t nowTimestamp) {
    if (!attachedAgentIdentity_.valid()) return;

    auto* reviewRoom = agentCoordination_.getRoom("reviews/main");
    if (!reviewRoom) return;

    std::set<std::string> knownDraftIds;
    const auto artifacts = reviewRoom->getArtifacts(0, 1000000);
    for (const auto& artifact : artifacts) {
        const auto parsed = json::parse(artifact.message.payload, nullptr, false);
        if (parsed.is_discarded()) continue;
        if (!parsed.contains("draftId") || !parsed["draftId"].is_string()) continue;
        knownDraftIds.insert(parsed["draftId"].get<std::string>());
    }

    auto records = agentDraftQueue_.list(1000000, true);
    std::sort(records.begin(), records.end(), [](const core::AgentDraftRecord& a, const core::AgentDraftRecord& b) {
        const std::string ah = crypto::toHex(a.proposal.draftId());
        const std::string bh = crypto::toHex(b.proposal.draftId());
        if (ah != bh) return ah < bh;
        return a.proposal.createdAt < b.proposal.createdAt;
    });

    uint32_t checked = 0;
    uint32_t missing = 0;
    uint32_t repaired = 0;
    for (const auto& record : records) {
        if (record.reviews.empty()) continue;

        const std::string draftIdHex = crypto::toHex(record.proposal.draftId());
        checked += 1;
        if (knownDraftIds.count(draftIdHex) > 0) continue;
        missing += 1;

        json payload;
        payload["event"] = "draft_review_projection_recovery";
        payload["agentId"] = crypto::toHex(attachedAgentIdentity_.id);
        payload["draftId"] = draftIdHex;
        payload["status"] = core::draftStatusToString(record.status);
        payload["reviewCount"] = record.reviews.size();
        uint32_t approvedCount = 0;
        for (const auto& review : record.reviews) {
            if (review.approved) approvedCount += 1;
        }
        payload["reviewApprovedCount"] = approvedCount;
        payload["source"] = "draft_queue_consistency_check";
        if (!sanitizeNaanPayload(&payload, "review_consistency_repair_payload", nowTimestamp)) {
            continue;
        }
        const std::string payloadText = safeDump(payload);

        core::ToolInvocation call;
        call.toolName = "observatory.post";
        call.capability = core::AgentCapability::READ_NETWORK;
        call.keys = {"room", "kind", "payload"};
        call.payloadBytes = static_cast<uint32_t>(payloadText.size());
        call.hasSideEffects = true;
        call.explicitSideEffectFlag = true;
        if (authorizeNaanTool(call, "review_consistency_repair") &&
            agentCoordination_.postToRoom("reviews/main", attachedAgentIdentity_, core::RoomMessageType::REVIEW, payloadText, nowTimestamp)) {
            repaired += 1;
            knownDraftIds.insert(draftIdHex);
            appendNaanAuditEvent(nowTimestamp, "review_artifact_repair", draftIdHex, payload);
            if (repaired >= 8) break;
        }
    }

    if (checked > 0 || missing > 0 || repaired > 0) {
        json payload;
        payload["checked"] = checked;
        payload["missing"] = missing;
        payload["repaired"] = repaired;
        appendNaanAuditEvent(nowTimestamp, "review_artifact_crosscheck", "reviews/main", payload);
        naanConsistencyChecks_.fetch_add(1);
        naanConsistencyRepairs_.fetch_add(repaired);
        naanConsistencyLastAt_.store(nowTimestamp);
    }
}

void recoverNaanIndexesAndConsistency(uint64_t nowTimestamp, const std::string& context) {
    core::AgentDraftQueueRecoveryStats draftStats;
    core::CoordinationRecoveryStats coordinationStats;
    std::string draftReason;
    std::string coordinationReason;
    const bool draftOk = agentDraftQueue_.verifyAndRepairIndexes(&draftStats, &draftReason);
    const bool coordinationOk = agentCoordination_.verifyAndRepairIndexes(&coordinationStats, &coordinationReason);

    if (!draftOk) {
        utils::Logger::warn("NAAN draft queue index recovery failed (" + context + "): " + draftReason);
    }
    if (!coordinationOk) {
        utils::Logger::warn("NAAN coordination index recovery failed (" + context + "): " + coordinationReason);
    }

    reconcileNaanDraftQueueWithLedger(nowTimestamp);
    reconcileNaanReviewArtifactsWithDraftQueue(nowTimestamp);

    json payload;
    payload["context"] = context;
    payload["draftQueueReason"] = draftReason;
    payload["coordinationReason"] = coordinationReason;
    payload["draftQueueRecords"] = draftStats.records;
    payload["draftQueueDroppedDuplicates"] = draftStats.droppedDuplicateRecords;
    payload["coordinationRooms"] = coordinationStats.roomCount;
    payload["coordinationArtifacts"] = coordinationStats.roomArtifacts;
    payload["coordinationRebuiltEntries"] = coordinationStats.rebuiltEntries;
    appendNaanAuditEvent(nowTimestamp, "startup_recovery", "naan_indexes", payload);

    naanIndexRecoveryRuns_.fetch_add(1);
    naanIndexRecoveryLastAt_.store(nowTimestamp);
}

void configureNaanRuntimeSandbox() {
    core::SandboxPolicy policy;
    policy.allowSideEffects = true;
    policy.allowlist = {
        core::AgentCapability::READ_LEDGER,
        core::AgentCapability::READ_NETWORK,
        core::AgentCapability::PROPOSE_KNOWLEDGE,
        core::AgentCapability::FETCH_EXTERNAL
    };
    policy.workspaceRoot = config_.dataDir;
    policy.readRoots = {config_.dataDir, config_.dataDir + "/naan"};
    policy.writeRoots = {config_.dataDir + "/naan"};
    policy.deniedPathPrefixes = {
        config_.dataDir + "/wallet",
        config_.dataDir + "/keys",
        config_.dataDir + "/keystore"
    };
    policy.deniedFileNames = {
        "wallet.dat",
        "wallet.keys",
        "mnemonic.txt",
        "seed.txt",
        "id_rsa",
        "id_ed25519",
        ".env",
        "credentials.json",
        "private.key"
    };
    policy.deniedExtensions = {".pem", ".key", ".seed", ".mnemonic"};
    policy.maxToolPayloadBytes = 131072;
    agentRuntimeSandbox_.setPolicy(policy);

    naanToolSchemas_.clear();

    core::ToolSchemaRule observatory;
    observatory.toolName = "observatory.post";
    observatory.capability = core::AgentCapability::READ_NETWORK;
    observatory.requiredKeys = {"room", "kind", "payload"};
    observatory.maxPayloadBytes = 65536;
    observatory.allowSideEffects = true;
    observatory.requireExplicitSideEffectFlag = true;
    naanToolSchemas_.push_back(observatory);

    core::ToolSchemaRule enqueue;
    enqueue.toolName = "draft.enqueue";
    enqueue.capability = core::AgentCapability::PROPOSE_KNOWLEDGE;
    enqueue.requiredKeys = {"title", "body", "powBits", "powNonce"};
    enqueue.maxPayloadBytes = 65536;
    enqueue.allowSideEffects = true;
    enqueue.requireExplicitSideEffectFlag = true;
    naanToolSchemas_.push_back(enqueue);

    core::ToolSchemaRule pipelineDrain;
    pipelineDrain.toolName = "draft.submit_batch";
    pipelineDrain.capability = core::AgentCapability::PROPOSE_KNOWLEDGE;
    pipelineDrain.requiredKeys = {"atTimestamp", "maxBatch"};
    pipelineDrain.maxPayloadBytes = 256;
    pipelineDrain.allowSideEffects = true;
    pipelineDrain.requireExplicitSideEffectFlag = true;
    naanToolSchemas_.push_back(pipelineDrain);
}

std::vector<core::AgentSubmissionBatchItem> runNaanPipelineDrain(uint64_t atTimestamp,
                                                                 uint32_t overrideMaxBatch = 0,
                                                                 bool* skippedQuarantine = nullptr,
                                                                 uint32_t* effectiveBatchLimit = nullptr) {
    if (!naanRuntimeInitialized_.load() || !poeV1_ || !attachedAgentIdentity_.valid()) return {};

    std::lock_guard<std::mutex> lock(naanPipelineMtx_);
    const auto scoreBefore = agentScore_.snapshot();
    if (skippedQuarantine) {
        *skippedQuarantine = scoreBefore.quarantined;
    }

    uint32_t requestedBatch = scoreBefore.batchLimit;
    if (overrideMaxBatch > 0) {
        requestedBatch = std::min<uint32_t>(overrideMaxBatch, 500);
    }
    const uint32_t boundedBatch = std::min<uint32_t>(scoreBefore.batchLimit, requestedBatch);
    if (effectiveBatchLimit) {
        *effectiveBatchLimit = boundedBatch;
    }

    core::ToolInvocation drainCall;
    drainCall.toolName = "draft.submit_batch";
    drainCall.capability = core::AgentCapability::PROPOSE_KNOWLEDGE;
    drainCall.keys = {"atTimestamp", "maxBatch"};
    drainCall.payloadBytes = 32;
    drainCall.hasSideEffects = true;
    drainCall.explicitSideEffectFlag = true;
    if (!authorizeNaanTool(drainCall, "pipeline_drain")) {
        applyNaanScoreStep(0, 0, naanAbusePolicyPenalty_.load(), atTimestamp, "pipeline_drain_denied");
        naanLastPipelineTs_.store(atTimestamp);
        json payload;
        payload["result"] = "denied";
        payload["reason"] = "sandbox_denied";
        payload["requestedBatch"] = requestedBatch;
        payload["effectiveBatch"] = boundedBatch;
        appendNaanAuditEvent(atTimestamp, "pipeline_drain", "draft_queue", payload);
        return {};
    }

    if (scoreBefore.quarantined || boundedBatch == 0) {
        naanLastPipelineTs_.store(atTimestamp);
        json payload;
        payload["result"] = "skipped";
        payload["quarantined"] = scoreBefore.quarantined;
        payload["requestedBatch"] = requestedBatch;
        payload["effectiveBatch"] = boundedBatch;
        appendNaanAuditEvent(atTimestamp, "pipeline_drain", "draft_queue", payload);
        return {};
    }

    core::AgentSubmissionPipelineConfig cfg = agentSubmissionPipeline_.config();
    cfg.maxBatchSize = boundedBatch;
    core::AgentSubmissionPipeline pipeline(cfg);

    auto batch = pipeline.runDeterministicBatch(
        agentDraftQueue_, *poeV1_, attachedAgentIdentity_.privateKey, atTimestamp);

    uint32_t accepted = 0;
    uint32_t rejected = 0;
    uint32_t violations = 0;

    if (!batch.empty()) {
        naanPipelineRuns_.fetch_add(1);
        for (auto& item : batch) {
            if (item.submitId != crypto::Hash256{}) {
                item.acceptanceRewardCredited = transfer_
                    ? transfer_->hasTransaction(node::rewardIdForAcceptance(item.submitId))
                    : false;
            }
            if (item.action == "approved") {
                naanPipelineApproved_.fetch_add(1);
                accepted += 1;
            } else if (item.action == "submitted") {
                naanPipelineSubmitted_.fetch_add(1);
                accepted += 1;
            } else if (item.action == "rejected") {
                naanPipelineRejected_.fetch_add(1);
                rejected += 1;
            }
        }
        violations += classifyNaanBatchViolations(batch);
        naanLastActionTs_.store(atTimestamp);
    }

    applyNaanScoreStep(accepted, rejected, violations, atTimestamp, "pipeline_drain");
    naanLastPipelineTs_.store(atTimestamp);

    uint64_t prunedSubmitted = 0;
    {
        const int64_t keepRaw = utils::Config::instance().getInt64("agent.retention.max_submitted_drafts", 0);
        if (keepRaw > 0) {
            uint32_t keep = static_cast<uint32_t>(std::min<int64_t>(keepRaw, 1000000));
            prunedSubmitted = agentDraftQueue_.pruneSubmitted(keep);
        }
    }

    json payload;
    payload["result"] = "ok";
    payload["accepted"] = accepted;
    payload["rejected"] = rejected;
    payload["violations"] = violations;
    payload["items"] = batch.size();
    payload["requestedBatch"] = requestedBatch;
    payload["effectiveBatch"] = boundedBatch;
    payload["prunedSubmittedDrafts"] = prunedSubmitted;
    payload["rewardVisibility"] = json::array();
    for (const auto& item : batch) {
        if (item.submitId == crypto::Hash256{}) continue;
        json reward;
        reward["draftId"] = crypto::toHex(item.draftId);
        reward["submitId"] = crypto::toHex(item.submitId);
        reward["expectedAcceptanceRewardAtoms"] = item.expectedAcceptanceRewardAtoms;
        reward["acceptanceRewardCredited"] = item.acceptanceRewardCredited;
        payload["rewardVisibility"].push_back(std::move(reward));
    }
    appendNaanAuditEvent(atTimestamp, "pipeline_drain", "draft_queue", payload);
    return batch;
}

core::poe_v1::ContentType inferDraftContentTypeForPoe(const core::AgentDraftProposal& proposal) const {
    if (proposal.title.rfind("CODE:", 0) == 0 || proposal.title.rfind("code:", 0) == 0) {
        return core::poe_v1::ContentType::CODE;
    }
    if (proposal.body.find("\nQ:") != std::string::npos && proposal.body.find("\nA:") != std::string::npos) {
        return core::poe_v1::ContentType::QA;
    }
    if (proposal.body.rfind("```", 0) == 0) {
        return core::poe_v1::ContentType::CODE;
    }
    return core::poe_v1::ContentType::TEXT;
}

bool findPoePowNonceForDraft(const core::AgentDraftProposal& proposal,
                             uint64_t startNonce,
                             uint64_t maxAttempts,
                             uint64_t* outNonce) const {
    if (!outNonce) return false;
    if (proposal.title.empty() || proposal.body.empty()) return false;

    core::poe_v1::KnowledgeEntryV1 entry;
    entry.version = 1;
    entry.timestamp = proposal.createdAt;
    entry.authorPubKey = proposal.author;
    entry.contentType = inferDraftContentTypeForPoe(proposal);
    entry.title = proposal.title;
    entry.body = proposal.body;
    entry.citations = proposal.citations;
    entry.powBits = proposal.powBits;

    crypto::Hash256 cid = entry.contentId();
    std::array<uint8_t, crypto::SHA256_SIZE + 8> buf{};
    std::memcpy(buf.data(), cid.data(), crypto::SHA256_SIZE);

    uint64_t limit = maxAttempts == 0 ? std::numeric_limits<uint64_t>::max() : maxAttempts;
    uint64_t nonce = startNonce;
    for (uint64_t i = 0; i < limit; ++i, ++nonce) {
        for (int b = 0; b < 8; ++b) {
            buf[crypto::SHA256_SIZE + static_cast<size_t>(b)] =
                static_cast<uint8_t>((nonce >> (8 * b)) & 0xFF);
        }
        crypto::Hash256 sid = crypto::sha256(buf.data(), buf.size());
        if (core::poe_v1::hasLeadingZeroBits(sid, proposal.powBits)) {
            *outNonce = nonce;
            return true;
        }
    }

    return false;
}

bool initNaanCoordination() {
    bool expected = false;
    if (!naanRuntimeInitialized_.compare_exchange_strong(expected, true)) {
        utils::Logger::error("NAAN runtime already initialized (single-instance guard)");
        return false;
    }

    auto failInit = [&](const std::string& message) {
        utils::Logger::error(message);
        naanRuntimeInitialized_.store(false);
        return false;
    };

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    naanRuntimeStartedAt_.store(now);
    naanRuntimeCrashStatePath_ = config_.dataDir + "/naan/runtime.state";
    std::string crashLoadReason;
    if (!naanRuntimeSupervisor_.loadCrashState(naanRuntimeCrashStatePath_, &crashLoadReason)) {
        return failInit("Failed to load NAAN runtime crash state: " + crashLoadReason);
    }

    std::error_code naanDirEc;
    std::filesystem::create_directories(config_.dataDir + "/naan", naanDirEc);
    if (naanDirEc) {
        return failInit("Failed to create NAAN workspace directory");
    }

    if (!configureNaanStorage()) {
        return failInit("Failed to initialize NAAN storage namespace");
    }

    configureNaanRuntimeSandbox();
    if (!configureNaanTaskScheduler()) {
        return failInit("Failed to initialize NAAN deterministic task scheduler");
    }
    configureNaanScorePolicy();
    configureNaanAdaptiveScheduler();

    std::string scoreLoadReason;
    if (!loadNaanScoreState(&scoreLoadReason)) {
        return failInit("Failed to initialize NAAN score state: " + scoreLoadReason);
    }

    configureNaanConnectorAbuseGuard();
    configureNaanSubmissionPipeline();

    {
        auto& cfg = utils::Config::instance();
        core::CoordinationConfig coordCfg = agentCoordination_.config();
        int64_t pruneIntervalRaw = cfg.getInt64("naan.retention.prune_interval_seconds", 0);
        if (pruneIntervalRaw < 0) pruneIntervalRaw = 0;
        if (pruneIntervalRaw > 1000000) pruneIntervalRaw = 1000000;
        coordCfg.pruneIntervalSeconds = static_cast<uint32_t>(pruneIntervalRaw);

        int64_t maxArtifactsRaw = cfg.getInt64("agent.retention.max_observatory_history", 0);
        if (maxArtifactsRaw < 0) maxArtifactsRaw = 0;
        if (maxArtifactsRaw > 1000000) maxArtifactsRaw = 1000000;
        coordCfg.maxArtifactHistory = static_cast<uint32_t>(maxArtifactsRaw);
        agentCoordination_.setConfig(coordCfg);
    }
    refreshSecurityPolicyHashes("naan_runtime_init");
    refreshSecurityPolicyHashes("naan_init");

    if (crashLoadReason == "not_found") {
        (void)persistNaanCrashState("startup_seed");
    }
    if (scoreLoadReason == "not_found") {
        (void)persistNaanScoreState("startup_seed");
    }

    core::RoomConfig roomCfg;
    {
        auto& cfg = utils::Config::instance();
        int64_t maxMessagesRaw = cfg.getInt64("agent.retention.room.max_messages", 0);
        if (maxMessagesRaw < 0) maxMessagesRaw = 0;
        if (maxMessagesRaw > 1000000) maxMessagesRaw = 1000000;
        roomCfg.maxMessages = static_cast<uint32_t>(maxMessagesRaw);

        int64_t retentionRaw = cfg.getInt64("agent.retention.room.seconds", 0);
        if (retentionRaw < 0) retentionRaw = 0;
        if (retentionRaw > 315576000) retentionRaw = 315576000;
        roomCfg.retentionSeconds = static_cast<uint32_t>(retentionRaw);

        int64_t payloadRaw = cfg.getInt64("naan.rooms.max_payload_bytes", 8192);
        if (payloadRaw < 512) payloadRaw = 512;
        if (payloadRaw > 1048576) payloadRaw = 1048576;
        roomCfg.maxPayloadBytes = static_cast<uint32_t>(payloadRaw);
    }

    const std::array<std::pair<const char*, core::RoomType>, 4> defaultRooms = {{
        {"tasks/main", core::RoomType::TASKS},
        {"reviews/main", core::RoomType::REVIEWS},
        {"disputes/main", core::RoomType::DISPUTES},
        {"alerts/main", core::RoomType::ALERTS}
    }};

    for (const auto& room : defaultRooms) {
        if (!agentCoordination_.hasRoom(room.first)) {
            if (!agentCoordination_.createRoom(room.first, room.second, roomCfg)) {
                return failInit(std::string("Failed to initialize NAAN room: ") + room.first);
            }
        }
    }

    auto pathGate = agentRuntimeSandbox_.authorizePath(
        core::AgentCapability::PROPOSE_KNOWLEDGE,
        core::PathAccessMode::WRITE,
        config_.dataDir + "/naan/startup_draft.json",
        true
    );
    if (pathGate != core::RuntimeActionResult::ALLOWED) {
        return failInit("NAAN sandbox path policy denied startup workspace: " + core::runtimeActionResultToString(pathGate));
    }

    std::vector<uint8_t> seedMaterial;
    const std::string tag = "synapsenet:naan:agent:v1";
    seedMaterial.insert(seedMaterial.end(), tag.begin(), tag.end());

    if (keys_ && keys_->isValid()) {
        auto pub = keys_->getPublicKey();
        seedMaterial.insert(seedMaterial.end(), pub.begin(), pub.end());
    } else {
        seedMaterial.insert(seedMaterial.end(), config_.dataDir.begin(), config_.dataDir.end());
    }

    auto seed = crypto::sha256(seedMaterial.data(), seedMaterial.size());
    attachedAgentIdentity_ = core::AgentIdentity::fromSeed(seed);
    if (!attachedAgentIdentity_.valid()) {
        return failInit("Failed to initialize NAAN attached agent identity");
    }

    recoverNaanIndexesAndConsistency(now, "startup");

    json startup;
    startup["event"] = "attached_agent_started";
    startup["agentId"] = crypto::toHex(attachedAgentIdentity_.id);
    startup["nodeAddress"] = address_;
    startup["mode"] = "always_on";
    startup["startedAt"] = now;
    startup["pid"] = static_cast<uint64_t>(::getpid());
    if (!sanitizeNaanPayload(&startup, "startup_alert_payload", now)) {
        return failInit("Failed to sanitize startup alert payload");
    }
    const std::string startupText = safeDump(startup);

    core::ToolInvocation startupAlertCall;
    startupAlertCall.toolName = "observatory.post";
    startupAlertCall.capability = core::AgentCapability::READ_NETWORK;
    startupAlertCall.keys = {"room", "kind", "payload"};
    startupAlertCall.payloadBytes = static_cast<uint32_t>(startupText.size());
    startupAlertCall.hasSideEffects = true;
    startupAlertCall.explicitSideEffectFlag = true;

    if (authorizeNaanTool(startupAlertCall, "startup_alert") &&
        agentCoordination_.postToRoom("alerts/main", attachedAgentIdentity_, core::RoomMessageType::ALERT, startupText, now)) {
            naanLastActionTs_.store(now);
            appendNaanAuditEvent(now, "observatory_post", "alerts/main", startup);
    } else {
        utils::Logger::warn("NAAN startup alert was blocked or could not be published");
    }

    core::AgentDraftProposal startupDraft;
    startupDraft.createdAt = now;
    startupDraft.author = attachedAgentIdentity_.id;
    startupDraft.title = "NAAN startup report";
    startupDraft.body = startupText;
    const auto poeCfg = poeV1_ ? poeV1_->getConfig() : core::PoeV1Config{};
    startupDraft.powBits = poeCfg.powBits;
    uint64_t powNonce = 0;
    if (!findPoePowNonceForDraft(startupDraft, now, poeCfg.powMaxAttempts, &powNonce)) {
        return failInit("Failed to compute PoE v1 PoW for NAAN startup draft");
    }
    startupDraft.powNonce = powNonce;
    startupDraft.signature = crypto::sign(startupDraft.signatureHash(), attachedAgentIdentity_.privateKey);
    std::string draftReason;
    core::ToolInvocation startupDraftCall;
    startupDraftCall.toolName = "draft.enqueue";
    startupDraftCall.capability = core::AgentCapability::PROPOSE_KNOWLEDGE;
    startupDraftCall.keys = {"title", "body", "powBits", "powNonce"};
    startupDraftCall.payloadBytes = static_cast<uint32_t>(startupDraft.title.size() + startupDraft.body.size());
    startupDraftCall.hasSideEffects = true;
    startupDraftCall.explicitSideEffectFlag = true;

    if (authorizeNaanTool(startupDraftCall, "startup_draft") && agentDraftQueue_.enqueue(startupDraft, &draftReason)) {
        naanLastDraftTs_.store(now);
        json payload;
        payload["result"] = "accepted";
        payload["draftId"] = crypto::toHex(startupDraft.draftId());
        payload["reason"] = "accepted";
        appendNaanAuditEvent(now, "draft_enqueue", crypto::toHex(startupDraft.draftId()), payload);
    } else {
        if (draftReason.empty()) draftReason = "sandbox_denied";
        utils::Logger::warn("NAAN startup draft rejected: " + draftReason);
        json payload;
        payload["result"] = "rejected";
        payload["draftId"] = crypto::toHex(startupDraft.draftId());
        payload["reason"] = draftReason;
        appendNaanAuditEvent(now, "draft_enqueue", crypto::toHex(startupDraft.draftId()), payload);
    }

    naanLastHeartbeatTs_.store(now);
    const auto st = currentNaanFailoverState(now);
    utils::Logger::info("NAAN runtime state: " + core::failoverStateToString(st));
    return true;
}

bool runNaanResearchTask(uint64_t now, const core::AgentAdaptiveSchedule& schedule) {
    auto trimCopy = [](const std::string& value) {
        size_t start = value.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return std::string{};
        size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(start, end - start + 1);
    };
    auto lowerCopy = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };
    auto queryTypeToString = [](web::QueryType type) {
        switch (type) {
            case web::QueryType::CLEARNET: return "clearnet";
            case web::QueryType::DARKNET: return "darknet";
            case web::QueryType::BOTH: return "both";
            case web::QueryType::KNOWLEDGE_NETWORK: return "knowledge_network";
            case web::QueryType::DIRECT_LINK: return "direct_link";
        }
        return "both";
    };
    auto truncateCopy = [](const std::string& value, size_t limit) {
        if (value.size() <= limit) return value;
        return value.substr(0, limit);
    };

    const bool subsystemReady = ensureWebSubsystem();
    web::WebSearch* webSearch = nullptr;
    web::AIWrapper* webAi = nullptr;
    web::QueryDetector* webDetector = nullptr;
    web::SearchConfig cfg = web::defaultSearchConfig();
    if (subsystemReady) {
        std::lock_guard<std::mutex> lock(webMtx_);
        if (webSearch_) {
            webSearch = webSearch_.get();
            webAi = webAi_.get();
            webDetector = webDetector_.get();
            cfg = webSearch_->getConfig();
        }
    }

    std::vector<std::string> queries;
    for (const auto& raw : cfg.naanAutoSearchQueries) {
        std::string q = trimCopy(raw);
        if (!q.empty()) queries.push_back(q);
    }
    if (queries.empty()) {
        queries = {
            "latest space engineering research",
            "latest ai research papers",
            "open source systems engineering best practices"
        };
    }

    const uint64_t tick = naanTickCount_.load();
    const uint64_t entropy = static_cast<uint64_t>(attachedAgentIdentity_.id[0]) ^
                             (static_cast<uint64_t>(attachedAgentIdentity_.id[7]) << 8) ^
                             (static_cast<uint64_t>(attachedAgentIdentity_.id[15]) << 16);
    const size_t queryIndex = queries.empty() ? 0 : static_cast<size_t>((tick + entropy) % queries.size());
    std::string selectedQuery = queries.empty() ? std::string{} : queries[queryIndex];

    const auto route = refreshTorRoutePolicy(true);
    const bool torRequired = agentTorRequired_.load();
    const bool torReachable = agentTorReachable_.load();
    const bool torReadyForWeb = agentTorWebReady_.load();
    const bool forceTorMode = cfg.naanForceTorMode || torRequired;

    std::string mode = lowerCopy(trimCopy(cfg.naanAutoSearchMode));
    if (mode != "clearnet" && mode != "darknet" && mode != "both" && mode != "auto") {
        mode = "both";
    }

    if (cfg.routeClearnetThroughTor || forceTorMode) {
        if (cfg.tor.socksHost.empty()) cfg.tor.socksHost = configuredTorSocksHost();
        if (cfg.tor.socksPort == 0) cfg.tor.socksPort = configuredTorSocksPort();
        if (cfg.tor.socksPort == 0) cfg.tor.socksPort = 9050;
    }

    if (forceTorMode) {
        cfg.routeClearnetThroughTor = true;
        cfg.enableClearnet = torReachable && route.allowWebClearnet;
        cfg.enableDarknet = torReachable && route.allowWebOnion;
    }

    web::QueryType queryType = web::QueryType::BOTH;
    if (mode == "clearnet") {
        queryType = web::QueryType::CLEARNET;
    } else if (mode == "darknet") {
        queryType = web::QueryType::DARKNET;
    } else if (mode == "auto" && webDetector) {
        auto analysis = webDetector->analyze(selectedQuery);
        queryType = analysis.type;
        if (queryType == web::QueryType::DIRECT_LINK || queryType == web::QueryType::KNOWLEDGE_NETWORK) {
            queryType = web::QueryType::BOTH;
        }
    } else {
        queryType = web::QueryType::BOTH;
    }

    if (queryType == web::QueryType::BOTH) {
        if (!cfg.enableClearnet && cfg.enableDarknet) queryType = web::QueryType::DARKNET;
        if (!cfg.enableDarknet && cfg.enableClearnet) queryType = web::QueryType::CLEARNET;
    } else if (queryType == web::QueryType::CLEARNET && !cfg.enableClearnet && cfg.enableDarknet) {
        queryType = web::QueryType::DARKNET;
    } else if (queryType == web::QueryType::DARKNET && !cfg.enableDarknet && cfg.enableClearnet) {
        queryType = web::QueryType::CLEARNET;
    }

    const uint32_t maxResults = std::max<uint32_t>(1, std::min<uint32_t>(cfg.naanAutoSearchMaxResults, 32));
    std::vector<web::SearchResult> results;
    uint64_t clearnetResults = 0;
    uint64_t onionResults = 0;
    std::string topSitesSummary;
    std::string context;
    std::string skipReason;
    std::string searchError;

    if (!subsystemReady || !webSearch || !cfg.naanAutoSearchEnabled) {
        skipReason = !cfg.naanAutoSearchEnabled ? "auto_search_disabled" : "web_subsystem_unavailable";
    } else if (selectedQuery.empty()) {
        skipReason = "empty_query";
    } else if (forceTorMode && torRequired && !torReadyForWeb) {
        skipReason = torReachable ? "tor_bootstrap_incomplete" : "tor_socks_unreachable";
    } else if (!cfg.enableClearnet && !cfg.enableDarknet) {
        skipReason = forceTorMode ? "tor_route_unavailable" : "search_routes_disabled";
    } else {
        {
            std::lock_guard<std::mutex> lock(webMtx_);
            if (!webSearch_) {
                skipReason = "web_subsystem_unavailable";
            } else {
                webSearch_->setConfig(cfg);
                webSearch = webSearch_.get();
                webAi = webAi_.get();
            }
        }
        if (skipReason.empty() && webSearch) {
            try {
                results = webSearch->search(selectedQuery, queryType);
                if (results.size() > maxResults) {
                    results.resize(maxResults);
                }
                for (const auto& item : results) {
                    if (item.isOnion) onionResults += 1;
                    else clearnetResults += 1;
                }
                {
                    std::vector<std::string> topSites;
                    std::unordered_set<std::string> seenSites;
                    for (const auto& item : results) {
                        std::string host = trimCopy(item.domain);
                        if (host.empty()) host = trimCopy(web::extractDomain(item.url));
                        if (host.empty()) continue;
                        if (!seenSites.insert(host).second) continue;
                        topSites.push_back(host);
                        if (topSites.size() >= 3) break;
                    }
                    if (!topSites.empty()) {
                        std::ostringstream joined;
                        for (size_t i = 0; i < topSites.size(); ++i) {
                            if (i > 0) joined << ", ";
                            joined << topSites[i];
                        }
                        topSitesSummary = joined.str();
                    }
                }
                if (webAi && !results.empty()) {
                    context = webAi->injectContext(selectedQuery, results);
                }
            } catch (const std::exception& e) {
                searchError = e.what();
            } catch (...) {
                searchError = "unknown_error";
            }
        }
    }
    if (forceTorMode &&
        (skipReason == "tor_route_unavailable" ||
         skipReason == "tor_bootstrap_incomplete" ||
         skipReason == "tor_socks_unreachable")) {
        naanWebFailClosedSkips_.fetch_add(1);
    }

    json payload;
    payload["event"] = "attached_agent_research_auto_search";
    payload["agentId"] = crypto::toHex(attachedAgentIdentity_.id);
    payload["tick"] = tick;
    payload["state"] = core::schedulingStateToString(schedule.state);
    payload["observatoryMessages"] = agentCoordination_.totalMessages();
    payload["autoSearchEnabled"] = cfg.naanAutoSearchEnabled;
    payload["mode"] = mode;
    payload["query"] = selectedQuery;
    payload["queryIndex"] = static_cast<uint64_t>(queryIndex);
    payload["queryType"] = queryTypeToString(queryType);
    payload["maxResults"] = maxResults;
    payload["torRequired"] = torRequired;
    payload["torReachable"] = torReachable;
    payload["torReadyForWeb"] = torReadyForWeb;
    payload["forceTorMode"] = forceTorMode;
    payload["routeClearnetThroughTor"] = cfg.routeClearnetThroughTor;
    payload["enableClearnet"] = cfg.enableClearnet;
    payload["enableDarknet"] = cfg.enableDarknet;
    payload["resultCount"] = results.size();
    payload["clearnetResults"] = clearnetResults;
    payload["onionResults"] = onionResults;
    if (!skipReason.empty()) payload["skipReason"] = skipReason;
    if (!searchError.empty()) payload["error"] = searchError;
    if (!context.empty()) {
        payload["context"] = truncateCopy(context, 4096);
    }
    payload["results"] = json::array();
    for (const auto& result : results) {
        json item;
        item["title"] = truncateCopy(result.title, 160);
        item["url"] = truncateCopy(result.url, 512);
        item["domain"] = truncateCopy(result.domain, 120);
        item["onion"] = result.isOnion;
        item["relevance"] = result.relevanceScore;
        payload["results"].push_back(std::move(item));
    }

    if (!sanitizeNaanPayload(&payload, "research_tick_payload", now)) {
        naanLastResearchTs_.store(now);
        return false;
    }
    const std::string payloadText = safeDump(payload);

    core::ToolInvocation researchCall;
    researchCall.toolName = "observatory.post";
    researchCall.capability = core::AgentCapability::READ_NETWORK;
    researchCall.keys = {"room", "kind", "payload"};
    researchCall.payloadBytes = static_cast<uint32_t>(payloadText.size());
    researchCall.hasSideEffects = true;
    researchCall.explicitSideEffectFlag = true;

    bool posted = false;
    if (authorizeNaanTool(researchCall, "research_tick") &&
        agentCoordination_.postToRoom("tasks/main", attachedAgentIdentity_, core::RoomMessageType::TASK, payloadText, now)) {
        posted = true;
        naanLastActionTs_.store(now);
        appendNaanAuditEvent(now, "observatory_post", "tasks/main", payload);
    }
    {
        std::lock_guard<std::mutex> lock(naanWebResearchMtx_);
        naanWebResearchSnapshot_.lastSearchAt = now;
        naanWebResearchSnapshot_.query = truncateCopy(selectedQuery, 256);
        naanWebResearchSnapshot_.queryType = queryTypeToString(queryType);
        naanWebResearchSnapshot_.resultCount = static_cast<uint64_t>(results.size());
        naanWebResearchSnapshot_.clearnetResults = clearnetResults;
        naanWebResearchSnapshot_.onionResults = onionResults;
        naanWebResearchSnapshot_.topSites = topSitesSummary;
        naanWebResearchSnapshot_.saved = posted;
        naanWebResearchSnapshot_.skipReason = skipReason;
        naanWebResearchSnapshot_.error = searchError;
    }
    naanLastResearchTs_.store(now);
    return posted;
}

bool runNaanVerifyTask(uint64_t now, const core::AgentAdaptiveSchedule& schedule) {
    (void)schedule;
    if (!poeV1_) {
        naanLastVerifyTs_.store(now);
        return false;
    }

    std::vector<core::AgentDraftRecord> records;
    auto queued = agentDraftQueue_.listByStatus(core::DraftStatus::QUEUED, 1000000);
    auto review = agentDraftQueue_.listByStatus(core::DraftStatus::REVIEW_REQUIRED, 1000000);
    auto approved = agentDraftQueue_.listByStatus(core::DraftStatus::APPROVED, 1000000);
    records.reserve(queued.size() + review.size() + approved.size());
    records.insert(records.end(), queued.begin(), queued.end());
    records.insert(records.end(), review.begin(), review.end());
    records.insert(records.end(), approved.begin(), approved.end());
    std::sort(records.begin(), records.end(), [](const core::AgentDraftRecord& a, const core::AgentDraftRecord& b) {
        const std::string ah = crypto::toHex(a.proposal.draftId());
        const std::string bh = crypto::toHex(b.proposal.draftId());
        if (ah != bh) return ah < bh;
        return a.proposal.createdAt < b.proposal.createdAt;
    });

    if (records.empty()) {
        naanLastVerifyTs_.store(now);
        return false;
    }

    auto dry = agentSubmissionPipeline_.dryRun(records.front(), attachedAgentIdentity_.privateKey, *poeV1_);
    std::string reviewStoreReason;
    const bool reviewStored = agentDraftQueue_.upsertReview(
        dry.draftId,
        attachedAgentIdentity_.id,
        dry.ok,
        now,
        dry.reason,
        &reviewStoreReason
    );
    uint32_t reviewCount = 0;
    uint32_t approvalCount = 0;
    auto reviewedRecord = agentDraftQueue_.get(dry.draftId);
    if (reviewedRecord.has_value()) {
        reviewCount = static_cast<uint32_t>(reviewedRecord->reviews.size());
        for (const auto& review : reviewedRecord->reviews) {
            if (review.approved) approvalCount += 1;
        }
    }

    json reviewPayload;
    reviewPayload["event"] = "attached_agent_verify_tick";
    reviewPayload["agentId"] = crypto::toHex(attachedAgentIdentity_.id);
    reviewPayload["draftId"] = crypto::toHex(dry.draftId);
    reviewPayload["ok"] = dry.ok;
    reviewPayload["reason"] = dry.reason;
    reviewPayload["nextStatus"] = dry.nextStatus;
    reviewPayload["reviewRecorded"] = reviewStored;
    reviewPayload["reviewRecordReason"] = reviewStoreReason;
    reviewPayload["reviewCount"] = reviewCount;
    reviewPayload["reviewApprovedCount"] = approvalCount;
    if (!sanitizeNaanPayload(&reviewPayload, "verify_tick_payload", now)) {
        naanLastVerifyTs_.store(now);
        return false;
    }
    const std::string reviewPayloadText = safeDump(reviewPayload);

    appendNaanAuditEvent(now, "draft_review_upsert", crypto::toHex(dry.draftId), reviewPayload);

    core::ToolInvocation verifyCall;
    verifyCall.toolName = "observatory.post";
    verifyCall.capability = core::AgentCapability::READ_NETWORK;
    verifyCall.keys = {"room", "kind", "payload"};
    verifyCall.payloadBytes = static_cast<uint32_t>(reviewPayloadText.size());
    verifyCall.hasSideEffects = true;
    verifyCall.explicitSideEffectFlag = true;

    bool posted = false;
    if (authorizeNaanTool(verifyCall, "verify_tick") &&
        agentCoordination_.postToRoom("reviews/main", attachedAgentIdentity_, core::RoomMessageType::REVIEW, reviewPayloadText, now)) {
        posted = true;
        naanLastActionTs_.store(now);
        appendNaanAuditEvent(now, "observatory_post", "reviews/main", reviewPayload);
    }
    naanLastVerifyTs_.store(now);
    return posted;
}

bool runNaanReviewTask(uint64_t now, const core::AgentAdaptiveSchedule& schedule) {
    json heartbeat;
    heartbeat["event"] = "attached_agent_heartbeat";
    heartbeat["agentId"] = crypto::toHex(attachedAgentIdentity_.id);
    heartbeat["tick"] = naanTickCount_.load();
    heartbeat["state"] = core::schedulingStateToString(schedule.state);
    heartbeat["failover"] = core::failoverStateToString(currentNaanFailoverState(now));
    if (!sanitizeNaanPayload(&heartbeat, "heartbeat_payload", now)) {
        naanLastReviewTs_.store(now);
        return false;
    }
    const std::string heartbeatText = safeDump(heartbeat);

    core::ToolInvocation heartbeatCall;
    heartbeatCall.toolName = "observatory.post";
    heartbeatCall.capability = core::AgentCapability::READ_NETWORK;
    heartbeatCall.keys = {"room", "kind", "payload"};
    heartbeatCall.payloadBytes = static_cast<uint32_t>(heartbeatText.size());
    heartbeatCall.hasSideEffects = true;
    heartbeatCall.explicitSideEffectFlag = true;

    bool posted = false;
    if (authorizeNaanTool(heartbeatCall, "heartbeat_alert") &&
        agentCoordination_.postToRoom("alerts/main", attachedAgentIdentity_, core::RoomMessageType::ALERT, heartbeatText, now)) {
        posted = true;
        naanLastActionTs_.store(now);
        naanLastHeartbeatTs_.store(now);
        appendNaanAuditEvent(now, "observatory_post", "alerts/main", heartbeat);
    }
    naanLastReviewTs_.store(now);
    return posted;
}

bool runNaanDraftTask(uint64_t now, const core::AgentAdaptiveSchedule& schedule) {
    json draftBody;
    draftBody["event"] = "periodic_research_snapshot";
    draftBody["agentId"] = crypto::toHex(attachedAgentIdentity_.id);
    draftBody["tick"] = naanTickCount_.load();
    draftBody["observatoryMessages"] = agentCoordination_.totalMessages();
    draftBody["schedulerState"] = core::schedulingStateToString(schedule.state);
    if (!sanitizeNaanPayload(&draftBody, "periodic_draft_payload", now)) {
        return false;
    }
    const std::string draftBodyText = safeDump(draftBody);

    core::AgentDraftProposal proposal;
    proposal.createdAt = now;
    proposal.author = attachedAgentIdentity_.id;
    proposal.title = "NAAN periodic research draft";
    proposal.body = draftBodyText;
    const auto poeCfg = poeV1_ ? poeV1_->getConfig() : core::PoeV1Config{};
    proposal.powBits = poeCfg.powBits;
    uint64_t powNonce = 0;
    if (!findPoePowNonceForDraft(proposal, now, poeCfg.powMaxAttempts, &powNonce)) {
        utils::Logger::warn("NAAN periodic draft PoW failed");
        return false;
    }
    proposal.powNonce = powNonce;
    proposal.signature = crypto::sign(proposal.signatureHash(), attachedAgentIdentity_.privateKey);

    std::string reason;
    core::ToolInvocation periodicDraftCall;
    periodicDraftCall.toolName = "draft.enqueue";
    periodicDraftCall.capability = core::AgentCapability::PROPOSE_KNOWLEDGE;
    periodicDraftCall.keys = {"title", "body", "powBits", "powNonce"};
    periodicDraftCall.payloadBytes = static_cast<uint32_t>(proposal.title.size() + proposal.body.size());
    periodicDraftCall.hasSideEffects = true;
    periodicDraftCall.explicitSideEffectFlag = true;

    bool posted = false;
    if (authorizeNaanTool(periodicDraftCall, "periodic_draft") && agentDraftQueue_.enqueue(proposal, &reason)) {
        posted = true;
        naanLastDraftTs_.store(now);
        naanLastActionTs_.store(now);
        json payload;
        payload["result"] = "accepted";
        payload["draftId"] = crypto::toHex(proposal.draftId());
        payload["reason"] = "accepted";
        appendNaanAuditEvent(now, "draft_enqueue", crypto::toHex(proposal.draftId()), payload);
    } else if (reason != "queue_full") {
        if (reason.empty()) reason = "sandbox_denied";
        utils::Logger::warn("NAAN periodic draft rejected: " + reason);
        naanLastDraftTs_.store(now);
        json payload;
        payload["result"] = "rejected";
        payload["draftId"] = crypto::toHex(proposal.draftId());
        payload["reason"] = reason;
        appendNaanAuditEvent(now, "draft_enqueue", crypto::toHex(proposal.draftId()), payload);
    }
    return posted;
}

bool runNaanSubmitTask(uint64_t now, const core::AgentAdaptiveSchedule& schedule) {
    (void)schedule;
    if (!poeV1_) return false;
    auto batch = runNaanPipelineDrain(now);
    return !batch.empty();
}

void tickNaanCoordination(uint64_t now) {
    if (!attachedAgentIdentity_.valid()) return;
    const uint64_t tickValue = naanTickCount_.fetch_add(1) + 1;
    agentCoordination_.periodicMaintenance(now);
    applyNaanScoreDecayTick(now);
    applyNaanConnectorAbuseAutoAction(now, tickValue);
    if (tickValue % 60 == 0) {
        recoverNaanIndexesAndConsistency(now, "periodic_tick");
    }

    const auto schedule = currentNaanSchedule();
    const uint64_t lastResearch = naanLastResearchTs_.load();
    const uint64_t lastVerify = naanLastVerifyTs_.load();
    const uint64_t lastReview = naanLastReviewTs_.load();
    const uint64_t lastDraft = naanLastDraftTs_.load();
    const uint64_t lastPipeline = naanLastPipelineTs_.load();

    const uint32_t researchInterval = std::max<uint32_t>(30, schedule.draftIntervalSeconds / 2);
    const uint32_t verifyInterval = std::max<uint32_t>(10, schedule.pipelineIntervalSeconds);

    const bool researchReady = (lastResearch == 0 || now >= lastResearch + researchInterval);
    const bool verifyReady = poeV1_ && hasPendingNaanDrafts() && (lastVerify == 0 || now >= lastVerify + verifyInterval);
    const bool reviewReady = (lastReview == 0 || now >= lastReview + schedule.heartbeatIntervalSeconds);
    const bool draftReady = (lastDraft == 0 || now >= lastDraft + schedule.draftIntervalSeconds);
    const bool submitReady = poeV1_ && (lastPipeline == 0 || now >= lastPipeline + schedule.pipelineIntervalSeconds);

    std::array<core::AgentTaskRequest, core::kAgentTaskClassCount> requests = {{
        {core::AgentTaskClass::RESEARCH, researchReady},
        {core::AgentTaskClass::VERIFY, verifyReady},
        {core::AgentTaskClass::REVIEW, reviewReady},
        {core::AgentTaskClass::DRAFT, draftReady},
        {core::AgentTaskClass::SUBMIT, submitReady}
    }};

    auto decision = naanTaskScheduler_.tick(requests);
    if (decision.scheduled) {
        switch (decision.taskClass) {
            case core::AgentTaskClass::RESEARCH:
                if (runNaanResearchTask(now, schedule)) naanTaskResearchRuns_.fetch_add(1);
                break;
            case core::AgentTaskClass::VERIFY:
                if (runNaanVerifyTask(now, schedule)) naanTaskVerifyRuns_.fetch_add(1);
                break;
            case core::AgentTaskClass::REVIEW:
                if (runNaanReviewTask(now, schedule)) naanTaskReviewRuns_.fetch_add(1);
                break;
            case core::AgentTaskClass::DRAFT:
                if (runNaanDraftTask(now, schedule)) naanTaskDraftRuns_.fetch_add(1);
                break;
            case core::AgentTaskClass::SUBMIT:
                if (runNaanSubmitTask(now, schedule)) naanTaskSubmitRuns_.fetch_add(1);
                break;
        }
    }

    const bool scheduledResearch =
        decision.scheduled && decision.taskClass == core::AgentTaskClass::RESEARCH;
    if (researchReady && !scheduledResearch) {
        if (runNaanResearchTask(now, schedule)) naanTaskResearchRuns_.fetch_add(1);
    }

    if (decision.scheduled || (tickValue % 30 == 0)) {
        (void)persistNaanSchedulerState("tick");
    }
}

void tickNaanCoordinationSupervised(uint64_t now) {
    if (!naanRuntimeInitialized_.load()) return;

    if (naanRuntimeSupervisor_.inRecovery(now)) {
        naanRecoverySkips_.fetch_add(1);
        return;
    }

    const auto crashBefore = naanRuntimeSupervisor_.crashState();
    try {
        tickNaanCoordination(now);
        if (crashBefore.consecutiveCrashes > 0 || crashBefore.recoveryUntilTimestamp > 0) {
            naanRuntimeSupervisor_.markSuccess();
            (void)persistNaanCrashState("recovered");
        }
    } catch (const std::exception& e) {
        uint64_t backoff = naanRuntimeSupervisor_.markFailure(now);
        (void)persistNaanCrashState("failure");
        utils::Logger::error("NAAN runtime failure: " + std::string(e.what()) +
                             " (backoff=" + std::to_string(backoff) + "s)");
    } catch (...) {
        uint64_t backoff = naanRuntimeSupervisor_.markFailure(now);
        (void)persistNaanCrashState("failure_unknown");
        utils::Logger::error("NAAN runtime failure: unknown_exception"
                             " (backoff=" + std::to_string(backoff) + "s)");
    }
}
