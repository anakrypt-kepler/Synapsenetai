json currentPoeSelfValidatorBootstrapStatus() const {
    core::PoeSelfValidatorBootstrapPolicyState state;
    std::string mode;
    std::string statusReason;
    bool effectiveAllow = false;
    bool configuredAllow = false;
    uint64_t lastEvaluatedAt = 0;
    {
        std::lock_guard<std::mutex> lock(poeSelfValidatorBootstrapMtx_);
        state = poeSelfValidatorBootstrapState_;
        mode = poeSelfValidatorBootstrapMode_;
        statusReason = poeSelfValidatorBootstrapStatusReason_;
        effectiveAllow = poeSelfValidatorBootstrapEffectiveAllow_;
        configuredAllow = poeSelfValidatorBootstrapConfiguredAllow_;
        lastEvaluatedAt = poeSelfValidatorBootstrapLastEvaluatedAt_;
    }

    uint32_t connectedPeers = 0;
    uint32_t knownPeers = 0;
    uint32_t validatorCount = 0;
    if (network_) {
        connectedPeers = static_cast<uint32_t>(std::min<size_t>(network_->peerCount(), std::numeric_limits<uint32_t>::max()));
        knownPeers = connectedPeers;
    }
    if (discovery_) {
        auto discStats = discovery_->getStats();
        knownPeers = static_cast<uint32_t>(std::min<uint64_t>(discStats.knownPeersCount, std::numeric_limits<uint32_t>::max()));
    }
    if (poeV1_) {
        validatorCount = static_cast<uint32_t>(std::min<size_t>(poeV1_->getDeterministicValidators().size(), 64));
    }

    const auto policyCfg = currentPoeSelfValidatorBootstrapPolicyConfig();
    json out;
    out["configuredAllowSelfValidatorBootstrap"] = configuredAllow;
    out["effectiveAllowSelfValidatorBootstrap"] = effectiveAllow;
    out["persistentStrictMode"] = state.persistentStrictMode;
    out["mode"] = mode;
    out["statusReason"] = statusReason;
    out["connectedPeers"] = connectedPeers;
    out["knownPeers"] = knownPeers;
    out["validatorCount"] = validatorCount;
    out["thresholdConnectedPeers"] = policyCfg.strictConnectedPeers;
    out["thresholdKnownPeers"] = policyCfg.strictKnownPeers;
    out["thresholdValidatorCount"] = policyCfg.strictValidatorCount;
    out["activationChecks"] = policyCfg.activationChecks;
    out["consecutiveEligibleChecks"] = state.consecutiveEligibleChecks;
    out["strictModeActivatedAt"] = state.strictModeActivatedAt;
    out["strictModeReason"] = state.strictModeReason;
    out["temporaryOverrideUntil"] = policyCfg.temporaryOverrideUntil;
    out["temporaryOverrideActive"] =
        policyCfg.temporaryOverrideUntil != 0 &&
        lastEvaluatedAt != 0 &&
        lastEvaluatedAt < policyCfg.temporaryOverrideUntil;
    out["lastEvaluatedAt"] = lastEvaluatedAt;
    return out;
}

void applyPoeSelfValidatorBootstrapPolicy(uint64_t now, bool logVisibility = false) {
    if (!poeV1_) return;

    uint32_t connectedPeers = 0;
    uint32_t knownPeers = 0;
    if (network_) {
        connectedPeers = static_cast<uint32_t>(std::min<size_t>(network_->peerCount(), std::numeric_limits<uint32_t>::max()));
        knownPeers = connectedPeers;
    }
    if (discovery_) {
        auto discStats = discovery_->getStats();
        knownPeers = static_cast<uint32_t>(std::min<uint64_t>(discStats.knownPeersCount, std::numeric_limits<uint32_t>::max()));
    }
    const uint32_t validatorCount = static_cast<uint32_t>(
        std::min<size_t>(poeV1_->getDeterministicValidators().size(), 64));

    core::PoeSelfValidatorBootstrapPolicyState currentState;
    std::string previousMode;
    std::string previousReason;
    bool previousEffectiveAllow = false;
    bool configuredAllow = false;
    {
        std::lock_guard<std::mutex> lock(poeSelfValidatorBootstrapMtx_);
        currentState = poeSelfValidatorBootstrapState_;
        previousMode = poeSelfValidatorBootstrapMode_;
        previousReason = poeSelfValidatorBootstrapStatusReason_;
        previousEffectiveAllow = poeSelfValidatorBootstrapEffectiveAllow_;
        configuredAllow = utils::Config::instance().getBool(
            "poe.allow_self_validator_bootstrap",
            poeSelfValidatorBootstrapConfiguredAllow_);
        poeSelfValidatorBootstrapConfiguredAllow_ = configuredAllow;
    }

    const auto policyCfg = currentPoeSelfValidatorBootstrapPolicyConfig();
    core::PoeSelfValidatorBootstrapPolicyInput input;
    input.configuredAllowSelfBootstrap = configuredAllow;
    input.connectedPeers = connectedPeers;
    input.knownPeers = knownPeers;
    input.validatorCount = validatorCount;
    input.now = now;

    const auto decision = core::evaluatePoeSelfValidatorBootstrapPolicy(
        policyCfg,
        currentState,
        input);

    {
        std::lock_guard<std::mutex> lock(poeSelfValidatorBootstrapMtx_);
        poeSelfValidatorBootstrapState_ = decision.state;
        poeSelfValidatorBootstrapMode_ = decision.mode;
        poeSelfValidatorBootstrapStatusReason_ = decision.statusReason;
        poeSelfValidatorBootstrapEffectiveAllow_ = decision.effectiveAllowSelfBootstrap;
        poeSelfValidatorBootstrapLastEvaluatedAt_ = now;
    }

    auto poeCfg = poeV1_->getConfig();
    const bool oldAllowSelfBootstrap = poeCfg.allowSelfBootstrapValidator;
    if (oldAllowSelfBootstrap != decision.effectiveAllowSelfBootstrap) {
        poeCfg.allowSelfBootstrapValidator = decision.effectiveAllowSelfBootstrap;
        poeV1_->setConfig(poeCfg);
    }

    if (!decision.effectiveAllowSelfBootstrap && !decision.temporaryOverrideActive && config_.poeValidators.empty()) {
        crypto::PublicKey selfPub{};
        if (tryGetSelfValidatorPublicKey(selfPub)) {
            auto validators = poeV1_->getStaticValidators();
            if (validators.size() == 1 && validators.front() == selfPub) {
                poeV1_->setStaticValidators({});
            }
        }
    }

    if (decision.shouldPersistState) {
        persistPoeSelfValidatorBootstrapState(decision.state);
    }

    if (decision.transitionedToStrictMode ||
        previousEffectiveAllow != decision.effectiveAllowSelfBootstrap ||
        previousMode != decision.mode ||
        previousReason != decision.statusReason ||
        logVisibility) {
        std::ostringstream oss;
        oss << "PoE self-validator bootstrap mode=" << decision.mode
            << " effectiveAllow=" << (decision.effectiveAllowSelfBootstrap ? "true" : "false")
            << " configuredAllow=" << (configuredAllow ? "true" : "false")
            << " connectedPeers=" << connectedPeers
            << " knownPeers=" << knownPeers
            << " validators=" << validatorCount
            << " reason=" << decision.statusReason;
        if (decision.temporaryOverrideActive) {
            oss << " overrideUntil=" << policyCfg.temporaryOverrideUntil;
        }
        if (decision.transitionedToStrictMode) {
            utils::Logger::warn(oss.str());
        } else {
            utils::Logger::info(oss.str());
        }
    }
}

void updatePoeValidatorsFromStake() {
    if (!poeV1_ || !transfer_) return;
    if (config_.poeValidatorMode != "stake") return;
    const bool allowSelfBootstrap = poeV1_->getConfig().allowSelfBootstrapValidator;

    crypto::PublicKey selfPub{};
    bool hasSelfPub = false;
    if (keys_ && keys_->isValid()) {
        auto pubV = keys_->getPublicKey();
        if (pubV.size() >= crypto::PUBLIC_KEY_SIZE) {
            std::memcpy(selfPub.data(), pubV.data(), selfPub.size());
            hasSelfPub = true;
        }
    }

    std::vector<crypto::PublicKey> candidates;
    candidates.reserve(256);

    auto addPk = [&](const crypto::PublicKey& pk) {
        if (std::all_of(pk.begin(), pk.end(), [](uint8_t b) { return b == 0; })) return;
        candidates.push_back(pk);
    };

    auto addValidatorHex = [&](const std::string& token) {
        std::string t = token;
        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!t.empty() && isSpace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
        while (!t.empty() && isSpace(static_cast<unsigned char>(t.back()))) t.pop_back();
        if (t.empty()) return;

        if (t == "self") {
            if (hasSelfPub) addPk(selfPub);
            return;
        }

        if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) t = t.substr(2);
        auto bytes = crypto::fromHex(t);
        if (bytes.size() != crypto::PUBLIC_KEY_SIZE) return;
        crypto::PublicKey pk{};
        std::memcpy(pk.data(), bytes.data(), pk.size());
        addPk(pk);
    };

    if (!config_.poeValidators.empty()) {
        std::string raw = config_.poeValidators;
        for (char& c : raw) {
            if (c == ';') c = ',';
        }
        std::string cur;
        for (char c : raw) {
            if (c == ',') {
                addValidatorHex(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        addValidatorHex(cur);
    }

    for (const auto& pk : poeV1_->getStaticValidators()) addPk(pk);

    for (const auto& sid : poeV1_->listEntryIds(0)) {
        auto e = poeV1_->getEntry(sid);
        if (e) addPk(e->authorPubKey);
    }
    for (const auto& vid : poeV1_->listVoteIds(0)) {
        auto v = poeV1_->getVoteById(vid);
        if (v) addPk(v->validatorPubKey);
    }

    std::sort(candidates.begin(), candidates.end(), [](const crypto::PublicKey& a, const crypto::PublicKey& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const crypto::PublicKey& a, const crypto::PublicKey& b) {
        return a == b;
    }), candidates.end());

    if (candidates.empty() && hasSelfPub && allowSelfBootstrap) candidates.push_back(selfPub);
    if (candidates.empty()) return;

    poeV1_->setStaticValidators(candidates);
    for (const auto& pk : candidates) {
        std::string addr = addressFromPubKey(pk);
        uint64_t bal = walletBalanceAtomsForPubKey(pk);
        poeV1_->setValidatorIdentity(pk, true);
        poeV1_->setValidatorStake(pk, bal);
    }
}

std::string handleRpcPoeSubmit(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    std::string typeStr = params.value("type", "");
    std::string question = params.value("question", "");
    std::string answer = params.value("answer", "");
    std::string source = params.value("source", "");
    bool autoFinalize = params.value("auto_finalize", true);

    if (!poeV1_ || !transfer_ || !keys_ || !keys_->isValid() || address_.empty()) {
        throw std::runtime_error("PoE or wallet not ready");
    }

    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    core::poe_v1::ContentType type = core::poe_v1::ContentType::QA;
    if (!typeStr.empty()) {
        std::string t = lower(typeStr);
        if (t == "qa" || t == "qna") type = core::poe_v1::ContentType::QA;
        else if (t == "text") type = core::poe_v1::ContentType::TEXT;
        else if (t == "code" || t == "patch") type = core::poe_v1::ContentType::CODE;
        else if (t == "linklist" || t == "links") type = core::poe_v1::ContentType::LINKLIST;
        else if (t == "other") type = core::poe_v1::ContentType::OTHER;
        else throw std::runtime_error("unknown type");
    }

    std::vector<crypto::Hash256> citations;
    if (params.contains("citations")) {
        if (!params["citations"].is_array()) {
            throw std::runtime_error("citations must be array");
        }
        for (const auto& item : params["citations"]) {
            if (!item.is_string()) throw std::runtime_error("citation must be hex string");
            citations.push_back(parseHash256Hex(item.get<std::string>()));
        }
    }

    crypto::PrivateKey pk{};
    auto pkv = keys_->getPrivateKey();
    if (pkv.size() < pk.size()) {
        throw std::runtime_error("Invalid private key");
    }
    std::memcpy(pk.data(), pkv.data(), pk.size());

    std::string title;
    std::string body;
    if (type == core::poe_v1::ContentType::QA) {
        if (question.empty() || answer.empty()) {
            throw std::runtime_error("question and answer are required");
        }
        title = question;
        body = answer;
        if (!source.empty()) {
            body += "\nsource: " + source;
        }
    } else if (type == core::poe_v1::ContentType::CODE) {
        title = params.value("title", "");
        body = params.value("patch", "");
        if (body.empty()) body = params.value("body", "");
        if (title.empty() || body.empty()) {
            throw std::runtime_error("title and patch/body are required");
        }
    } else {
        title = params.value("title", "");
        body = params.value("body", "");
        if (title.empty() || body.empty()) {
            throw std::runtime_error("title and body are required");
        }
    }

    updatePoeValidatorsFromStake();
    auto submitRes = poeV1_->submit(type, title, body, citations, pk, autoFinalize);
    if (!submitRes.ok) {
        throw std::runtime_error("PoE submit failed: " + submitRes.error);
    }

    crypto::PublicKey authorPub = crypto::derivePublicKey(pk);
    std::string authorAddr = addressFromPubKey(authorPub);
    if (authorAddr.empty()) authorAddr = address_;

    uint64_t credited = 0;
    if (submitRes.finalized && submitRes.acceptanceReward > 0) {
        crypto::Hash256 rewardId = node::rewardIdForAcceptance(submitRes.submitId);
        if (transfer_->creditRewardDeterministic(authorAddr, rewardId, submitRes.acceptanceReward)) {
            credited = submitRes.acceptanceReward;
        }
    }

    {
        std::lock_guard<std::mutex> lock(invMtx_);
        knownPoeEntries_.insert(crypto::toHex(submitRes.submitId));
    }
    broadcastInv(synapse::InvType::POE_ENTRY, submitRes.submitId);
    for (const auto& v : poeV1_->getVotesForSubmit(submitRes.submitId)) {
        crypto::Hash256 vid = v.payloadHash();
        {
            std::lock_guard<std::mutex> lock(invMtx_);
                knownPoeVotes_.insert(crypto::toHex(vid));
        }
        broadcastInv(synapse::InvType::POE_VOTE, vid);
    }

    if (ledger_) {
        auto pub = keys_->getPublicKey();
        auto entry = poeV1_->getEntry(submitRes.submitId);
        if (entry) {
            core::Event ev{};
            ev.timestamp = entry->timestamp;
            ev.type = core::EventType::POE_ENTRY;
            ev.data = entry->serialize();
            if (pub.size() >= ev.author.size()) {
                std::memcpy(ev.author.data(), pub.data(), ev.author.size());
            }
            ledger_->append(ev);
        }

        for (const auto& v : poeV1_->getVotesForSubmit(submitRes.submitId)) {
            core::Event ev{};
            ev.timestamp = std::time(nullptr);
            ev.type = core::EventType::POE_VOTE;
            ev.data = v.serialize();
            if (pub.size() >= ev.author.size()) {
                std::memcpy(ev.author.data(), pub.data(), ev.author.size());
            }
            ledger_->append(ev);
        }
    }

    auto entry = poeV1_->getEntry(submitRes.submitId);
    uint64_t expectedAtoms = entry ? poeV1_->calculateAcceptanceReward(*entry) : submitRes.acceptanceReward;
    uint32_t voteCount = static_cast<uint32_t>(poeV1_->getVotesForSubmit(submitRes.submitId).size());
    uint32_t requiredVotes = poeV1_->effectiveRequiredVotes();
    if (requiredVotes == 0) {
        core::PoeV1Config cfg = poeV1_->getConfig();
        requiredVotes = cfg.validatorsM == 0 ? 1U : cfg.validatorsM;
    }
    bool rewardCredited = transfer_
        ? transfer_->hasTransaction(node::rewardIdForAcceptance(submitRes.submitId))
        : false;

    return rpc::buildRpcPoeSubmitResponse(
        submitRes,
        type,
        expectedAtoms,
        atomsToNgt(expectedAtoms),
        credited,
        atomsToNgt(credited),
        voteCount,
        requiredVotes,
        rewardCredited);
}

std::string handleRpcPoeSubmitCode(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    params["type"] = "code";
    if (!params.contains("body") && params.contains("patch")) {
        params["body"] = params["patch"];
    }
    return handleRpcPoeSubmit(params.dump());
}

std::string handleRpcPoeListCode(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    size_t limit = 25;
    if (params.contains("limit")) {
        limit = static_cast<size_t>(std::max(1, params.value("limit", 25)));
    }
    if (!poeV1_) {
        throw std::runtime_error("PoE not ready");
    }

    std::vector<rpc::RpcPoeCodeListItem> items;
    auto ids = poeV1_->listEntryIds(0);
    for (auto it = ids.rbegin(); it != ids.rend() && items.size() < limit; ++it) {
        auto e = poeV1_->getEntry(*it);
        if (!e) continue;
        if (e->contentType != core::poe_v1::ContentType::CODE) continue;
        items.push_back({
            *it,
            e->contentId(),
            e->timestamp,
            e->title,
            poeV1_->isFinalized(*it)});
    }
    return rpc::buildRpcPoeCodeListResponse(items);
}

std::string handleRpcPoeFetchCode(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    std::string idHex = params.value("id", "");
    if (idHex.empty()) idHex = params.value("submitId", "");
    if (idHex.empty()) idHex = params.value("contentId", "");
    if (idHex.empty()) {
        throw std::runtime_error("id required");
    }
    if (!poeV1_) {
        throw std::runtime_error("PoE not ready");
    }

    crypto::Hash256 id = parseHash256Hex(idHex);
    auto entry = poeV1_->getEntry(id);
    crypto::Hash256 submitId = id;
    if (!entry) {
        entry = poeV1_->getEntryByContentId(id);
        if (entry) submitId = entry->submitId();
    }
    if (!entry) {
        throw std::runtime_error("not_found");
    }
    if (entry->contentType != core::poe_v1::ContentType::CODE) {
        throw std::runtime_error("not_code_entry");
    }

    uint64_t expected = poeV1_->calculateAcceptanceReward(*entry);
    return rpc::buildRpcPoeCodeFetchResponse(
        submitId,
        *entry,
        poeV1_->isFinalized(submitId),
        expected,
        atomsToNgt(expected));
}

std::string handleRpcPoeVote(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    if (!poeV1_ || !keys_ || !keys_->isValid()) {
        throw std::runtime_error("PoE or wallet not ready");
    }
    std::string submitHex = params.value("submitId", "");
    if (submitHex.empty()) {
        throw std::runtime_error("submitId required");
    }
    crypto::Hash256 submitId = parseHash256Hex(submitHex);

    crypto::PrivateKey pk{};
    auto pkv = keys_->getPrivateKey();
    if (pkv.size() < pk.size()) {
        throw std::runtime_error("Invalid private key");
    }
    std::memcpy(pk.data(), pkv.data(), pk.size());

    core::poe_v1::ValidationVoteV1 vote;
    vote.version = 1;
    vote.submitId = submitId;
    vote.prevBlockHash = poeV1_->chainSeed();
    vote.flags = params.value("flags", 0);
    if (params.contains("scores")) {
        const auto& scores = params["scores"];
        if (!scores.is_array() || scores.size() != 3) {
            throw std::runtime_error("scores must be array of 3 integers");
        }
        for (size_t i = 0; i < 3; ++i) {
            vote.scores[i] = static_cast<uint16_t>(scores[i].get<int>());
        }
    } else {
        vote.scores = {100, 100, 100};
    }
    core::poe_v1::signValidationVoteV1(vote, pk);
    bool added = poeV1_->addVote(vote);

    crypto::Hash256 voteId = vote.payloadHash();
    if (added) {
        {
            std::lock_guard<std::mutex> lock(invMtx_);
            knownPoeVotes_.insert(crypto::toHex(voteId));
        }
        broadcastInv(synapse::InvType::POE_VOTE, voteId);

        if (ledger_) {
            core::Event ev{};
            ev.timestamp = std::time(nullptr);
            ev.type = core::EventType::POE_VOTE;
            ev.data = vote.serialize();
            auto pub = keys_->getPublicKey();
            if (pub.size() >= ev.author.size()) {
                std::memcpy(ev.author.data(), pub.data(), ev.author.size());
            }
            ledger_->append(ev);
        }
    }

    uint64_t credited = maybeCreditAcceptanceReward(submitId);

    return rpc::buildRpcPoeVoteResponse(
        added,
        voteId,
        credited,
        atomsToNgt(credited));
}

std::string handleRpcPoeFinalize(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    if (!poeV1_) {
        throw std::runtime_error("PoE not ready");
    }
    std::string submitHex = params.value("submitId", "");
    if (submitHex.empty()) {
        throw std::runtime_error("submitId required");
    }
    crypto::Hash256 submitId = parseHash256Hex(submitHex);
    auto fin = poeV1_->finalize(submitId);
    uint64_t credited = maybeCreditAcceptanceReward(submitId);

    return rpc::buildRpcPoeFinalizeResponse(
        fin,
        credited,
        atomsToNgt(credited));
}

	    std::string handleRpcPoeEpoch(const std::string& paramsJson) {
	        auto params = parseRpcParams(paramsJson);
	        if (!poeV1_ || !transfer_) {
	            throw std::runtime_error("PoE/transfer not ready");
    }

    uint64_t budget = 0;
    if (params.contains("budget_atoms")) {
        budget = params["budget_atoms"].get<uint64_t>();
    } else if (params.contains("budget")) {
        if (params["budget"].is_number()) {
            double v = params["budget"].get<double>();
            budget = static_cast<uint64_t>(std::llround(v * 100000000.0));
        } else if (params["budget"].is_string()) {
            budget = parseNgtAtomic(params["budget"].get<std::string>());
        } else {
            throw std::runtime_error("budget must be number or string");
        }
    } else {
        int64_t cfgBudget = utils::Config::instance().getInt64(
            "poe.epoch_budget",
            config_.dev ? 100000000LL : 1000000000LL);
        if (cfgBudget > 0) budget = static_cast<uint64_t>(cfgBudget);
    }

    uint32_t iterations = params.value("iterations",
        static_cast<uint32_t>(std::max(1, utils::Config::instance().getInt(
            "poe.epoch_iterations",
            config_.dev ? 10 : 20))));

    bool creditRewards = params.value("credit_rewards", true);

	    auto epochRes = poeV1_->runEpoch(budget, iterations);
	    if (!epochRes.ok) {
	        throw std::runtime_error("PoE epoch failed: " + epochRes.error);
	    }

	    {
	        crypto::Hash256 hid = poeEpochInvHash(epochRes.epochId);
	        std::lock_guard<std::mutex> lock(invMtx_);
	        knownPoeEpochs_.insert(crypto::toHex(hid));
	    }
	    broadcastInv(synapse::InvType::POE_EPOCH, poeEpochInvHash(epochRes.epochId));

	    uint64_t mintedTotal = 0;
	    uint64_t mintedMine = 0;
	    uint64_t mintedCount = 0;
        std::vector<rpc::RpcPoeEpochAllocationView> allocations;

    for (const auto& alloc : epochRes.allocations) {
        std::string addr = addressFromPubKey(alloc.authorPubKey);
        crypto::Hash256 rid = node::rewardIdForEpoch(epochRes.epochId, alloc.contentId);
        bool credited = false;
        if (creditRewards && !addr.empty()) {
            if (transfer_->creditRewardDeterministic(addr, rid, alloc.amount)) {
                credited = true;
                mintedTotal += alloc.amount;
                mintedCount += 1;
                if (!address_.empty() && addr == address_) mintedMine += alloc.amount;
            }
        }
        allocations.push_back({
            alloc.submitId,
            alloc.contentId,
            addr,
            alloc.score,
            alloc.amount,
            atomsToNgt(alloc.amount),
            credited});
    }

    return rpc::buildRpcPoeEpochResponse(
        epochRes.epochId,
        epochRes.allocationHash,
        mintedTotal,
        atomsToNgt(mintedTotal),
        mintedCount,
        mintedMine,
        atomsToNgt(mintedMine),
        allocations);
}
