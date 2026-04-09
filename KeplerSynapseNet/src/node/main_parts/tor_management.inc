std::string configuredTorSocksHost() const {
    auto& cfg = utils::Config::instance();
    std::string host = cfg.getString("tor.socks.host", "");
    if (host.empty()) host = cfg.getString("agent.tor.socks_host", "127.0.0.1");
    if (host.empty()) host = "127.0.0.1";
    return host;
}

uint16_t configuredTorSocksPort() const {
    auto& cfg = utils::Config::instance();
    int port = cfg.has("tor.socks.port")
        ? cfg.getInt("tor.socks.port", 9050)
        : cfg.getInt("agent.tor.socks_port", 9050);
    if (port <= 0 || port > 65535) port = 9050;
    return static_cast<uint16_t>(port);
}

uint16_t configuredTorControlPort() const {
    int port = utils::Config::instance().getInt("tor.control.port", 9051);
    if (port <= 0 || port > 65535) port = 9051;
    return static_cast<uint16_t>(port);
}

std::string configuredTorRuntimeMode() const {
    return core::normalizeTorRuntimeMode(utils::Config::instance().getString("agent.tor.mode", "auto"));
}

bool allowManagedTorAutostart() const {
    return core::isManagedTorAutostartAllowedForMode(configuredTorRuntimeMode());
}

std::filesystem::path managedTorDirPath() const {
    return std::filesystem::path(config_.dataDir) / "tor";
}

std::filesystem::path managedTorPidFilePath() const {
    return managedTorDirPath() / "tor.pid";
}

bool readManagedTorPidFile(int64_t* outPid = nullptr) const {
    std::ifstream in(managedTorPidFilePath());
    if (!in.is_open()) return false;
    int64_t pid = 0;
    in >> pid;
    if (!in || pid <= 0) return false;
    if (outPid) *outPid = pid;
    return true;
}

#ifndef _WIN32
std::string readCommandOutput(const std::string& cmd, size_t maxBytes = 8192) const {
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return {};
    std::string out;
    out.reserve(512);
    char buf[512];
    while (!feof(fp) && out.size() < maxBytes) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        const size_t take = std::min(n, maxBytes - out.size());
        out.append(buf, take);
    }
    (void)pclose(fp);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return out;
}

bool managedTorProcessMatchesOwnership(int64_t pid) const {
    if (pid <= 0) return false;
    const std::string torDir = managedTorDirPath().string();
    std::ostringstream cmd;
    cmd << "ps -ww -p " << pid << " -o command= 2>/dev/null";
    const std::string line = readCommandOutput(cmd.str(), 16384);
    return core::isOwnedManagedTorCommandLine(line, torDir);
}

std::vector<int64_t> findOwnedManagedTorPidsByDataDir() const {
    const std::string torDir = managedTorDirPath().string();
    const std::string out = readCommandOutput("ps -axww -o pid= -o command= 2>/dev/null", 262144);
    return core::parseOwnedManagedTorPidsFromPsOutput(out, torDir);
}

bool stopManagedTorPidAndWait(int64_t pid) {
    if (pid <= 0) return false;
    if (kill(static_cast<pid_t>(pid), SIGTERM) != 0) {
        utils::Logger::warn("Managed Tor stop failed (SIGTERM): pid=" + std::to_string(pid) +
                            " errno=" + std::to_string(errno));
        return false;
    }

    bool exited = false;
    for (int i = 0; i < 20; ++i) {
        if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
            exited = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!exited) {
        utils::Logger::warn("Managed Tor did not exit after SIGTERM (pid=" + std::to_string(pid) + ")");
        return false;
    }
    return true;
}

bool spawnManagedTorProcess(const std::string& torBin,
                            const std::string& socksHost,
                            uint16_t socksPort,
                            uint16_t controlPort,
                            const std::filesystem::path& torDir,
                            const std::filesystem::path& pidFile) {
    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd >= 0) {
            (void)dup2(nullfd, STDIN_FILENO);
            (void)dup2(nullfd, STDOUT_FILENO);
            (void)dup2(nullfd, STDERR_FILENO);
        }
        long maxFd = sysconf(_SC_OPEN_MAX);
        if (maxFd < 0) maxFd = 256;
        for (int fd = 3; fd < maxFd; ++fd) {
            if (fd == nullfd) continue;
            close(fd);
        }
        if (nullfd > 2) close(nullfd);

        const std::string socksSpec = socksHost + ":" + std::to_string(socksPort);
        const std::string controlSpec = std::string("127.0.0.1:") + std::to_string(controlPort);
        execlp(torBin.c_str(),
               torBin.c_str(),
               "--quiet",
               "--SocksPort", socksSpec.c_str(),
               "--ControlPort", controlSpec.c_str(),
               "--DataDirectory", torDir.string().c_str(),
               "--PidFile", pidFile.string().c_str(),
               "--RunAsDaemon", "1",
               static_cast<char*>(nullptr));
        _exit(127);
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        return false;
    }
    if (!WIFEXITED(status)) return false;
    return WEXITSTATUS(status) == 0;
}

bool stopManagedTorRuntimeIfOwned(bool logOnNoop = false) {
    int64_t pid = managedTorPid_.load();
    bool hadPidCandidate = (pid > 0);
    if (pid <= 0) {
        (void)readManagedTorPidFile(&pid);
        hadPidCandidate = (pid > 0);
    }

    std::vector<int64_t> candidates;
    if (pid > 0 && managedTorProcessMatchesOwnership(pid)) {
        candidates.push_back(pid);
    } else {
        if (pid > 0 && logOnNoop) {
            utils::Logger::warn("Managed Tor stop pid ownership mismatch (" + std::to_string(pid) +
                                "); trying commandline/dataDir fallback scan");
        }
        candidates = findOwnedManagedTorPidsByDataDir();
    }

    if (candidates.empty()) {
        if (logOnNoop) {
            if (hadPidCandidate) utils::Logger::info("Managed Tor stop skipped: no owned runtime found");
            else utils::Logger::info("Managed Tor stop skipped: no pid file and no owned runtime found");
        }
        agentTorManaged_.store(false);
        managedTorPid_.store(0);
        return false;
    }

    bool allStopped = true;
    for (int64_t candidate : candidates) {
        if (!stopManagedTorPidAndWait(candidate)) {
            allStopped = false;
        }
    }
    if (!allStopped) {
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(managedTorPidFilePath(), ec);
    agentTorManaged_.store(false);
    agentTorReachable_.store(false);
    agentTorWebReady_.store(false);
    agentTorWebProbeConsecutiveFailures_.store(0);
    agentTorWebProbeConsecutiveSuccesses_.store(0);
    resetManagedTorRestartBackoffState();
    managedTorPid_.store(0);
    if (candidates.size() == 1) {
        utils::Logger::info("Stopped managed Tor runtime (pid=" + std::to_string(candidates.front()) + ")");
    } else {
        utils::Logger::warn("Stopped multiple owned managed Tor runtimes matched by dataDir (" +
                           std::to_string(candidates.size()) + ")");
    }
    return true;
}
#else
bool stopManagedTorRuntimeIfOwned(bool  = false) { return false; }
#endif

bool probeTorSocks() const {
    return canConnectTcp(configuredTorSocksHost(), configuredTorSocksPort(), 700);
}

bool probeTorControl() const {
    return canConnectTcp("127.0.0.1", configuredTorControlPort(), 350);
}

bool isOnionServiceActive() const {
    return privacy_ && !privacy_->getOnionAddress().empty();
}

bool isLoopbackPeerAddress(const std::string& address) const {
    return address == "127.0.0.1" || address == "::1" || address.rfind("127.", 0) == 0;
}

bool isDiscoveryEligiblePeerAddress(const std::string& address) const {
    if (isLoopbackPeerAddress(address)) return false;
    in_addr ipv4{};
    return inet_pton(AF_INET, address.c_str(), &ipv4) == 1;
}

std::string activeOnionAddress() const {
    return privacy_ ? privacy_->getOnionAddress() : std::string();
}

std::string activeTorSeedAddress() const {
    const std::string onion = activeOnionAddress();
    if (onion.empty()) return {};
    const uint16_t port = network_ ? network_->getPort() : config_.port;
    return onion + ":" + std::to_string(port);
}

std::optional<core::PeerHelloInfo> getPeerHelloInfo(const std::string& peerId) const {
    std::lock_guard<std::mutex> lock(peerHelloMtx_);
    auto it = peerHelloById_.find(peerId);
    if (it == peerHelloById_.end()) return std::nullopt;
    return it->second;
}

core::PeerDisplayInfo getPeerDisplayInfo(const network::Peer& peer) const {
    return core::selectPeerDisplayInfo(peer.address, peer.port, getPeerHelloInfo(peer.id));
}

core::PeerHelloInfo buildLocalPeerHello(const network::Peer& peer) const {
    core::PeerHelloInfo hello;
    const std::string onion = activeOnionAddress();
    const bool hasOnion = core::isValidOnionHost(onion);
    const bool rawOnion = core::isValidOnionHost(peer.address);
    const bool loopbackInbound = !peer.isOutbound && isLoopbackPeerAddress(peer.address);
    const bool torPreferred = agentTorRequired_.load();
    const uint16_t advertisedPort = network_ ? network_->getPort() : config_.port;

    if (hasOnion && (rawOnion || loopbackInbound || torPreferred)) {
        hello.transport = "tor-onion";
        hello.advertisedHost = onion;
        hello.advertisedPort = advertisedPort;
        hello.displayId = torSessionDisplayId_;
    } else if (torPreferred) {
        hello.transport = "tor-socks";
        hello.displayId = torSessionDisplayId_;
    } else {
        hello.transport = "clearnet";
    }

    return hello;
}

void sendPeerHello(const std::string& peerId) {
    if (!network_) return;
    const auto peer = network_->getPeer(peerId);
    if (peer.id.empty()) return;
    auto msg = makeMessage("peer_hello", core::serializePeerHelloPayload(buildLocalPeerHello(peer)));
    network_->send(peerId, msg);
}

bool likelyTor9050vs9150ConflictHint(bool torSocksReachable) const {
    return core::evaluateTor9050vs9150ConflictHint(
        configuredTorRuntimeMode(),
        configuredTorSocksPort(),
        torSocksReachable,
        canConnectTcp("127.0.0.1", 9150, 120));
}

void setTorWebProbeLastError(const std::string& err) {
    std::lock_guard<std::mutex> lock(agentTorWebProbeMtx_);
    agentTorWebProbeLastError_ = err;
}

std::string getTorWebProbeLastError() const {
    std::lock_guard<std::mutex> lock(agentTorWebProbeMtx_);
    return agentTorWebProbeLastError_;
}

void setTorBridgeProviderMetaSnapshot(const json& meta) {
    std::lock_guard<std::mutex> lock(torBridgeProviderMetaMtx_);
    torBridgeProviderMeta_ = meta;
    torBridgeProviderMetaUpdatedAt_.store(static_cast<uint64_t>(std::time(nullptr)));
}

json getTorBridgeProviderMetaSnapshot() const {
    std::lock_guard<std::mutex> lock(torBridgeProviderMetaMtx_);
    return torBridgeProviderMeta_;
}

std::filesystem::path torLastKnownGoodBridgeSubsetPath() const {
    return managedTorDirPath() / "last_known_good_bridge_subset.json";
}

node::TorStatusRuntimeInputs makeTorStatusRuntimeInputs() const {
    node::TorStatusRuntimeInputs inputs;
    inputs.privacyMode = config_.privacyMode;
    inputs.agentTorRequired = &agentTorRequired_;
    inputs.agentTorReachable = &agentTorReachable_;
    inputs.agentTorWebReady = &agentTorWebReady_;
    inputs.agentTorManaged = &agentTorManaged_;
    inputs.managedTorPid = &managedTorPid_;
    inputs.agentAllowClearnetFallback = &agentAllowClearnetFallback_;
    inputs.agentAllowP2PFallback = &agentAllowP2PFallback_;
    inputs.agentTorWebProbeLastAt = &agentTorWebProbeLastAt_;
    inputs.agentTorWebProbeLastOkAt = &agentTorWebProbeLastOkAt_;
    inputs.agentTorWebProbeExitCode = &agentTorWebProbeExitCode_;
    inputs.agentTorWebProbeConsecutiveFailures = &agentTorWebProbeConsecutiveFailures_;
    inputs.agentTorWebProbeConsecutiveSuccesses = &agentTorWebProbeConsecutiveSuccesses_;
    inputs.agentTorBridgeSubsetPersistCount = &agentTorBridgeSubsetPersistCount_;
    inputs.agentTorBridgeSubsetLastPersistAt = &agentTorBridgeSubsetLastPersistAt_;
    inputs.agentTorBridgeSubsetLastEpoch = &agentTorBridgeSubsetLastEpoch_;
    inputs.agentTorBridgeSubsetLastCount = &agentTorBridgeSubsetLastCount_;
    inputs.agentTorBridgeRemoteLastFetchAt = &agentTorBridgeRemoteLastFetchAt_;
    inputs.agentTorBridgeRemoteFetchAttempts = &agentTorBridgeRemoteFetchAttempts_;
    inputs.agentTorBridgeRemoteFetchSuccesses = &agentTorBridgeRemoteFetchSuccesses_;
    inputs.agentTorBridgeRemoteRateLimitedSkips = &agentTorBridgeRemoteRateLimitedSkips_;
    inputs.torBridgeProviderMetaUpdatedAt = &torBridgeProviderMetaUpdatedAt_;
    inputs.configuredTorRuntimeMode = [this]() { return configuredTorRuntimeMode(); };
    inputs.configuredTorSocksHost = [this]() { return configuredTorSocksHost(); };
    inputs.configuredTorSocksPort = [this]() { return configuredTorSocksPort(); };
    inputs.configuredTorControlPort = [this]() { return configuredTorControlPort(); };
    inputs.probeTorControl = [this]() { return probeTorControl(); };
    inputs.activeOnionAddress = [this]() { return activeOnionAddress(); };
    inputs.activeTorSeedAddress = [this]() { return activeTorSeedAddress(); };
    inputs.likelyTor9050vs9150ConflictHint =
        [this](bool torSocksReachable) { return likelyTor9050vs9150ConflictHint(torSocksReachable); };
    inputs.isOnionServiceActive = [this]() { return isOnionServiceActive(); };
    inputs.getTorWebProbeLastError = [this]() { return getTorWebProbeLastError(); };
    inputs.torLastKnownGoodBridgeSubsetPath =
        [this]() { return torLastKnownGoodBridgeSubsetPath().string(); };
    inputs.getTorBridgeProviderMetaSnapshotJson =
        [this]() { return getTorBridgeProviderMetaSnapshot().dump(); };
    return inputs;
}

node::NodeQuantumStatusRuntimeInputs makeNodeQuantumStatusRuntimeInputs() const {
    node::NodeQuantumStatusRuntimeInputs inputs;
    inputs.enabled = config_.quantumSecurity;
    inputs.requestedLevel = config_.securityLevel;
    inputs.quantumManager = quantumManager_.get();
    return inputs;
}

node::NodeNetworkHealthRuntimeInputs makeNodeNetworkHealthRuntimeInputs() const {
    node::NodeNetworkHealthRuntimeInputs inputs;
    inputs.network = network_.get();
    inputs.maxPeers = config_.maxPeers;
    inputs.maxInbound = config_.maxInbound;
    inputs.maxOutbound = config_.maxOutbound;
    inputs.ledgerHeight = ledger_ ? ledger_->height() : 0;
    inputs.networkHeight = &networkHeight_;
    inputs.invBackpressureDrops = &invBackpressureDrops_;
    inputs.getDataBackpressureDrops = &getDataBackpressureDrops_;
    inputs.gossipSuppressed = &gossipSuppressed_;
    inputs.gossipSubsetRouted = &gossipSubsetRouted_;
    return inputs;
}

rpc::RpcUpdateRuntimeInputs makeRpcUpdateRuntimeInputs() {
    rpc::RpcUpdateRuntimeInputs inputs;
    inputs.acceptUpdateManifest =
        [this](const core::UpdateManifest& manifest, bool relay, std::string* reason) {
            const auto status = acceptUpdateManifest(manifest, relay, reason);
            switch (status) {
                case UpdateManifestAccept::REJECTED:
                    return rpc::RpcUpdateManifestAccept::Rejected;
                case UpdateManifestAccept::DUPLICATE:
                    return rpc::RpcUpdateManifestAccept::Duplicate;
                case UpdateManifestAccept::ACCEPTED:
                default:
                    return rpc::RpcUpdateManifestAccept::Accepted;
            }
        };
    inputs.buildPolicyJson = [this]() { return implantUpdatePolicyToJson(); };
    inputs.buildPolicyHashes = [this]() {
        auto hashes = this->securityPolicyHashes();
        return rpc::RpcSecurityPolicyHashes{hashes.first, hashes.second};
    };
    inputs.buildDetachedSignerApprovalsJson =
        [this](const crypto::Hash256& bundleId) {
            return detachedSignerApprovalsToJson(bundleId);
        };
    inputs.listStoredUpdateManifests =
        [this](size_t limit) {
            std::vector<core::UpdateManifest> manifests;
            {
                std::lock_guard<std::mutex> lock(invMtx_);
                manifests.reserve(updateManifestsById_.size());
                for (const auto& [_, manifest] : updateManifestsById_) {
                    manifests.push_back(manifest);
                }
            }

            std::sort(
                manifests.begin(),
                manifests.end(),
                [](const core::UpdateManifest& a, const core::UpdateManifest& b) {
                    return std::lexicographical_compare(
                        a.bundleId.begin(),
                        a.bundleId.end(),
                        b.bundleId.begin(),
                        b.bundleId.end());
                });

            if (manifests.size() > limit) {
                manifests.erase(
                    manifests.begin(),
                    manifests.end() - static_cast<std::ptrdiff_t>(limit));
            }
            return manifests;
        };
    inputs.fetchStoredUpdateManifest =
        [this](const crypto::Hash256& bundleId, core::UpdateManifest& manifest) {
            return fetchStoredUpdateManifest(bundleId, manifest);
        };
    inputs.upsertDetachedSignerApproval =
        [this](const crypto::Hash256& bundleId, const core::DetachedSignerApproval& approval) {
            upsertDetachedSignerApproval(bundleId, approval);
        };
    inputs.validateUpdateSignerThreshold =
        [this](const core::UpdateManifest& manifest, std::string* reason) {
            return validateUpdateSignerThreshold(manifest, reason);
        };
    inputs.emitSecurityEvent =
        [this](uint64_t atTimestamp,
               const std::string& kind,
               const std::string& severity,
               const std::string& subject,
               const json& details) {
            emitSecurityEvent(atTimestamp, kind, severity, subject, details);
        };
    inputs.installerPolicyForManifest =
        [this](const core::UpdateManifest& manifest) {
            return installerPolicyForManifest(manifest);
        };
    inputs.readInstallerState = [this]() {
        std::lock_guard<std::mutex> lock(updateInstallMtx_);
        return updateInstaller_.state();
    };
    inputs.installManifest =
        [this](const core::UpdateManifest& manifest,
               const core::UpdateInstaller::UpdatePolicy& policy,
               std::string* reason,
               core::UpdateInstallerState* installerState) {
            std::lock_guard<std::mutex> lock(updateInstallMtx_);
            if (!updateInstaller_.installManifest(manifest, policy, reason)) {
                return false;
            }
            if (installerState) {
                *installerState = updateInstaller_.state();
            }
            return true;
        };
    inputs.advanceRollout =
        [this](const crypto::Hash256& bundleId,
               std::string* reason,
               core::UpdateInstallerState* installerState) {
            std::lock_guard<std::mutex> lock(updateInstallMtx_);
            if (!updateInstaller_.advanceRollout(bundleId, reason)) {
                return false;
            }
            if (installerState) {
                *installerState = updateInstaller_.state();
            }
            return true;
        };
    inputs.commitPending =
        [this](const crypto::Hash256& bundleId,
               std::string* reason,
               core::UpdateInstallerState* installerState) {
            std::lock_guard<std::mutex> lock(updateInstallMtx_);
            if (!updateInstaller_.commitPending(bundleId, reason)) {
                return false;
            }
            if (installerState) {
                *installerState = updateInstaller_.state();
            }
            return true;
        };
    inputs.rollback =
        [this](std::string* reason, core::UpdateInstallerState* installerState) {
            std::lock_guard<std::mutex> lock(updateInstallMtx_);
            if (!updateInstaller_.rollback(reason)) {
                return false;
            }
            if (installerState) {
                *installerState = updateInstaller_.state();
            }
            return true;
        };
    inputs.validateImplantDistributionManifest =
        [this](const core::UpdateManifest& manifest,
               bool safetyGatePassed,
               std::string* reason) {
            return validateImplantDistributionManifest(
                manifest,
                safetyGatePassed,
                reason);
        };
    inputs.markImplantPrepare =
        [this](const crypto::Hash256& bundleId,
               bool deterministicTestsPassed,
               bool sandboxBoundariesPassed,
               std::string* reason) {
            std::lock_guard<std::mutex> lock(implantSafetyMtx_);
            return implantSafetyPipeline_.markPrepare(
                bundleId,
                deterministicTestsPassed,
                sandboxBoundariesPassed,
                reason);
        };
    inputs.markImplantCanaryHealth =
        [this](const crypto::Hash256& bundleId,
               bool canaryHealthPassed,
               std::string* reason) {
            std::lock_guard<std::mutex> lock(implantSafetyMtx_);
            return implantSafetyPipeline_.markCanaryHealth(
                bundleId,
                canaryHealthPassed,
                reason);
        };
    inputs.markImplantWideHealth =
        [this](const crypto::Hash256& bundleId,
               bool wideHealthPassed,
               std::string* reason) {
            std::lock_guard<std::mutex> lock(implantSafetyMtx_);
            return implantSafetyPipeline_.markWideHealth(
                bundleId,
                wideHealthPassed,
                reason);
        };
    inputs.canCommitImplant =
        [this](const crypto::Hash256& bundleId, std::string* reason) {
            std::lock_guard<std::mutex> lock(implantSafetyMtx_);
            return implantSafetyPipeline_.canCommit(bundleId, reason);
        };
    inputs.getImplantSafetyRecord =
        [this](const crypto::Hash256& bundleId) {
            return getImplantSafetyRecord(bundleId);
        };
    inputs.clearImplantSafetyRecord =
        [this](const crypto::Hash256& bundleId, std::string* reason) {
            std::lock_guard<std::mutex> lock(implantSafetyMtx_);
            return implantSafetyPipeline_.clearRecord(bundleId, reason);
        };
    return inputs;
}

rpc::RpcAiCompletionInputs makeRpcAiCompletionInputs() {
    rpc::RpcAiCompletionInputs inputs;
    inputs.modelLoader = modelLoader_.get();
    inputs.modelMtx = &modelMtx_;
    inputs.modelRequests = &modelRequests_;
    inputs.agentTorRequired = &agentTorRequired_;
    inputs.agentTorReachable = &agentTorReachable_;
    inputs.getDefaultWebInject =
        []() { return utils::Config::instance().getBool("web.inject.enabled", false); };
    inputs.getDefaultWebOnion =
        []() { return utils::Config::instance().getBool("web.inject.onion", false); };
    inputs.getDefaultWebTor =
        []() { return utils::Config::instance().getBool("web.inject.tor_clearnet", false); };
    inputs.refreshTorRoutePolicy =
        [this](bool allowStartManagedTor) { return refreshTorRoutePolicy(allowStartManagedTor); };
    inputs.ensureWebSubsystem = [this]() { return ensureWebSubsystem(); };
    inputs.webMtx = &webMtx_;
    inputs.getWebAi = [this]() { return webAi_.get(); };
    inputs.getWebSearch = [this]() { return webSearch_.get(); };
    inputs.getWebDetector = [this]() { return webDetector_.get(); };
    inputs.onFailClosedSkip = [this]() { naanWebFailClosedSkips_.fetch_add(1); };
    inputs.runRemoteCompletion =
        [this](const json& params,
               const std::string& prompt,
               const model::GenerationParams& generationParams) {
            return handleRpcAiCompleteRemote(params, prompt, generationParams);
        };
    return inputs;
}

rpc::RpcNaanRuntimeInputs makeRpcNaanRuntimeInputs() {
    rpc::RpcNaanRuntimeInputs inputs;
    inputs.runtimeInitialized = naanRuntimeInitialized_.load();
    inputs.poeAvailable = static_cast<bool>(poeV1_);
    inputs.agentIdentityAvailable = attachedAgentIdentity_.valid();
    inputs.redact =
        [this](const std::string& value) { return redactPotentialSecrets(value); };
    inputs.getObservatoryFeed =
        [this](uint64_t sinceTimestamp, uint32_t limit) {
            return agentCoordination_.getObservatoryFeed(sinceTimestamp, limit);
        };
    inputs.getArtifact =
        [this](const crypto::Hash256& hash) {
            return agentCoordination_.getArtifact(hash);
        };
    inputs.listDrafts =
        [this](uint32_t limit, bool includeRejected) {
            return agentDraftQueue_.list(limit, includeRejected);
        };
    inputs.listDraftsByStatus =
        [this](core::DraftStatus status, uint32_t limit) {
            return agentDraftQueue_.listByStatus(status, limit);
        };
    inputs.getDraft =
        [this](const crypto::Hash256& draftId) {
            return agentDraftQueue_.get(draftId);
        };
    inputs.dryRunDraft =
        [this](const core::AgentDraftRecord& record) {
            return agentSubmissionPipeline_.dryRun(
                record,
                attachedAgentIdentity_.privateKey,
                *poeV1_);
        };
    inputs.runPipelineDrain =
        [this](uint64_t atTimestamp,
               uint32_t limit,
               bool* skippedQuarantine,
               uint32_t* effectiveLimit) {
            return runNaanPipelineDrain(
                atTimestamp,
                limit,
                skippedQuarantine,
                effectiveLimit);
        };
    inputs.buildPipelineSummary =
        [this]() {
            rpc::RpcNaanPipelineSummary summary;
            auto score = agentScore_.snapshot();
            const auto schedule =
                agentAdaptiveScheduler_.schedule(score.throttled, score.quarantined);
            summary.pipelineRuns = naanPipelineRuns_.load();
            summary.pipelineApproved = naanPipelineApproved_.load();
            summary.pipelineSubmitted = naanPipelineSubmitted_.load();
            summary.pipelineRejected = naanPipelineRejected_.load();
            summary.lastPipelineAt = naanLastPipelineTs_.load();
            summary.agentScore = score.score;
            summary.agentThrottled = score.throttled;
            summary.agentQuarantined = score.quarantined;
            summary.pipelineBatchLimit = score.batchLimit;
            summary.schedulerState = core::schedulingStateToString(schedule.state);
            summary.draftIntervalSec = schedule.draftIntervalSeconds;
            summary.pipelineIntervalSec = schedule.pipelineIntervalSeconds;
            summary.heartbeatIntervalSec = schedule.heartbeatIntervalSeconds;
            return summary;
        };
    return inputs;
}

rpc::RpcNaanStatusRuntimeInputs makeRpcNaanStatusRuntimeInputs() {
    rpc::RpcNaanStatusRuntimeInputs inputs;
    inputs.agentIdHex = crypto::toHex(attachedAgentIdentity_.id);
    inputs.runtimeInitialized = &naanRuntimeInitialized_;
    inputs.runtimeStartedAt = &naanRuntimeStartedAt_;
    inputs.getRuntimeState =
        [this](uint64_t now) {
            return currentNaanFailoverState(now);
        };
    inputs.getCrashState =
        [this]() {
            return naanRuntimeSupervisor_.crashState();
        };
    inputs.runtimeRecoverySkips = &naanRecoverySkips_;
    inputs.getSchedulerSnapshot =
        [this]() {
            return naanTaskScheduler_.snapshot();
        };
    inputs.listRooms =
        [this](core::RoomType type) {
            return agentCoordination_.listRooms(type);
        };
    inputs.totalMessages =
        [this]() {
            return agentCoordination_.totalMessages();
        };
    inputs.draftQueueSize =
        [this]() {
            return agentDraftQueue_.size();
        };
    inputs.countDraftsByStatus =
        [this](core::DraftStatus status) {
            return static_cast<uint64_t>(
                agentDraftQueue_.listByStatus(status, 1000000).size());
        };
    inputs.pipelineRuns = &naanPipelineRuns_;
    inputs.pipelineApproved = &naanPipelineApproved_;
    inputs.pipelineSubmitted = &naanPipelineSubmitted_;
    inputs.pipelineRejected = &naanPipelineRejected_;
    inputs.lastPipelineAt = &naanLastPipelineTs_;
    inputs.getScore =
        [this]() {
            return agentScore_.snapshot();
        };
    inputs.pipelineConfig = agentSubmissionPipeline_.config();
    inputs.getAdaptiveSchedule =
        [this](bool throttled, bool quarantined) {
            return agentAdaptiveScheduler_.schedule(throttled, quarantined);
        };
    inputs.getScoreDecayIntervalSec =
        [this]() {
            return static_cast<uint64_t>(naanScoreDecayIntervalSeconds_.load());
        };
    inputs.scoreLastDecayAt = &naanScoreLastDecayTs_;
    inputs.scoreLastViolationTick = &naanScoreLastViolationTick_;
    inputs.scoreBandTransitions = &naanScoreBandTransitions_;
    inputs.quarantineRecoveries = &naanQuarantineRecoveryTransitions_;
    inputs.quarantineRecoveryLastAt = &naanQuarantineRecoveryLastAt_;
    inputs.getAbuseSpamPenalty =
        [this]() {
            return static_cast<uint64_t>(naanAbuseSpamPenalty_.load());
        };
    inputs.getAbuseCitationPenalty =
        [this]() {
            return static_cast<uint64_t>(naanAbuseCitationPenalty_.load());
        };
    inputs.getAbusePolicyPenalty =
        [this]() {
            return static_cast<uint64_t>(naanAbusePolicyPenalty_.load());
        };
    inputs.getPolicyHashes =
        [this]() {
            return securityPolicyHashes();
        };
    inputs.securityEvents = &naanSecurityEvents_;
    inputs.securityHighSeverityEvents = &naanSecurityHighSeverityEvents_;
    inputs.securityLastEventAt = &naanSecurityLastEventAt_;
    inputs.getStorageInputs =
        [this]() {
            rpc::RpcNaanStorageInputs storage;
            const auto stats = naanAuditLog_.stats();
            storage.root = stats.rootDir;
            storage.auditSegments = stats.segmentCount;
            storage.auditRetainedEvents = stats.retainedEvents;
            storage.auditLastSeq = stats.lastSequence;
            storage.auditLastHash = crypto::toHex(stats.lastHash);
            storage.auditRecoveredLines = naanStorageRecoveredLines_.load();
            storage.auditDroppedSegments = naanStorageDroppedSegments_.load();
            storage.indexRecoveryRuns = naanIndexRecoveryRuns_.load();
            storage.indexRecoveryLastAt = naanIndexRecoveryLastAt_.load();
            storage.consistencyChecks = naanConsistencyChecks_.load();
            storage.consistencyRepairs = naanConsistencyRepairs_.load();
            storage.consistencyLastAt = naanConsistencyLastAt_.load();
            return storage;
        };
    inputs.connectorAutoQuarantineEvents = &naanConnectorAbuseEvents_;
    inputs.connectorAutoQuarantineLastAt = &naanConnectorAbuseLastAt_;
    inputs.connectorAutoQuarantineLastPolicyDelta =
        &naanConnectorAbuseLastPolicyDelta_;
    inputs.connectorAutoQuarantineLastFailureDelta =
        &naanConnectorAbuseLastFailureDelta_;
    inputs.getConnectorAutoQuarantineLastViolations =
        [this]() {
            return static_cast<uint64_t>(naanConnectorAbuseLastViolations_.load());
        };
    inputs.taskRunsResearch = &naanTaskResearchRuns_;
    inputs.taskRunsVerify = &naanTaskVerifyRuns_;
    inputs.taskRunsReview = &naanTaskReviewRuns_;
    inputs.taskRunsDraft = &naanTaskDraftRuns_;
    inputs.taskRunsSubmit = &naanTaskSubmitRuns_;
    inputs.lastResearchAt = &naanLastResearchTs_;
    inputs.lastVerifyAt = &naanLastVerifyTs_;
    inputs.lastReviewAt = &naanLastReviewTs_;
    inputs.lastActionAt = &naanLastActionTs_;
    inputs.lastHeartbeatAt = &naanLastHeartbeatTs_;
    inputs.lastDraftAt = &naanLastDraftTs_;
    inputs.ticks = &naanTickCount_;
    inputs.getConnectorStatus =
        [this]() {
            rpc::RpcNaanConnectorStatusInputs connector;
            connector.validation.totalLines = naanWebConfigTotalLines_.load();
            connector.validation.appliedLines = naanWebConfigAppliedLines_.load();
            connector.validation.invalidLines = naanWebConfigInvalidLines_.load();
            connector.validation.unknownKeys = naanWebConfigUnknownKeys_.load();
            connector.validation.sanitizedWrites =
                naanWebConfigSanitizedWrites_.load();
            std::lock_guard<std::mutex> lock(webMtx_);
            connector.validation.unknownKeySamples =
                naanWebConfigUnknownKeySamples_;
            if (webSearch_) {
                connector.available = true;
                connector.health = webSearch_->getConnectorHealth();
                connector.config = webSearch_->getConfig();
                connector.stats = webSearch_->getStats();
                connector.sitePolicyPath = config_.dataDir + "/naan_agent_web.conf";
            }
            return connector;
        };
    inputs.webFailClosedSkips = &naanWebFailClosedSkips_;
    inputs.miningFailClosedSkips = &miningFailClosedSkips_;
    const auto route = refreshTorRoutePolicy(true);
    inputs.tor = node::collectTorStatusInputs(makeTorStatusRuntimeInputs(), route);
    inputs.networkHealth =
        node::collectNodeNetworkHealthInputs(makeNodeNetworkHealthRuntimeInputs());
    inputs.getRewardVisibility =
        [this]() {
            rpc::RpcNaanRewardVisibilityInputs visibility;
            uint64_t rewardObserved = 0;
            uint64_t rewardFinalized = 0;
            uint64_t rewardCredited = 0;
            uint64_t rewardExpectedAtoms = 0;
            uint64_t rewardCreditedAtoms = 0;
            if (poeV1_ && attachedAgentIdentity_.valid()) {
                const auto submitted =
                    agentDraftQueue_.listByStatus(core::DraftStatus::SUBMITTED, 1000000);
                for (const auto& rec : submitted) {
                    const auto dry = agentSubmissionPipeline_.dryRun(
                        rec,
                        attachedAgentIdentity_.privateKey,
                        *poeV1_);
                    if (!dry.ok) {
                        continue;
                    }
                    rewardObserved += 1;
                    const uint64_t expectedAtoms =
                        poeV1_->calculateAcceptanceReward(dry.entry);
                    if (UINT64_MAX - rewardExpectedAtoms < expectedAtoms) {
                        rewardExpectedAtoms = UINT64_MAX;
                    } else {
                        rewardExpectedAtoms += expectedAtoms;
                    }
                    if (poeV1_->isFinalized(dry.entry.submitId())) {
                        rewardFinalized += 1;
                    }
                    const bool credited =
                        transfer_
                        ? transfer_->hasTransaction(
                              node::rewardIdForAcceptance(dry.entry.submitId()))
                        : false;
                    if (credited) {
                        rewardCredited += 1;
                        if (UINT64_MAX - rewardCreditedAtoms < expectedAtoms) {
                            rewardCreditedAtoms = UINT64_MAX;
                        } else {
                            rewardCreditedAtoms += expectedAtoms;
                        }
                    }
                }
            }
            visibility.deterministic = true;
            visibility.observedSubmissions = rewardObserved;
            visibility.finalizedSubmissions = rewardFinalized;
            visibility.creditedSubmissions = rewardCredited;
            visibility.expectedAtoms = rewardExpectedAtoms;
            visibility.creditedAtoms = rewardCreditedAtoms;
            return visibility;
        };
    inputs.redactionCount = &naanRedactionCount_;
    return inputs;
}

std::string torBridgeSubsetNodeIdHint() const {
    if (!address_.empty()) return address_;
    if (keys_ && keys_->isValid()) {
        const auto pub = keys_->getPublicKey();
        if (!pub.empty()) {
            return crypto::toHex(crypto::sha256(pub.data(), pub.size()));
        }
    }
    return crypto::toHex(crypto::sha256("tor_bridge_subset_node|" + config_.dataDir));
}

bool persistLastKnownGoodBridgeSubset(const std::string& trigger) {
    if (!agentTorWebReady_.load()) return false;

    const std::string naanWebCfgPath = config_.dataDir + "/naan_agent_web.conf";
    if (!std::filesystem::exists(naanWebCfgPath)) return false;

    web::SearchConfig cfg;
    (void)web::loadSearchConfig(config_.dataDir + "/web_search.conf", cfg);
    web::SearchConfigValidationStats validation{};
    (void)web::loadSearchConfigOverlay(naanWebCfgPath, cfg, &validation);
    web::sanitizeSearchConfig(cfg);
    const auto bridges = core::sanitizeAndDedupeObfs4BridgeLines(cfg.tor.bridgeManualLines);
    if (cfg.tor.bridgeTransport != "obfs4" || bridges.empty()) {
        return false;
    }

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    auto& runtimeCfg = utils::Config::instance();
    const uint64_t epochWindowSec = static_cast<uint64_t>(std::max<int64_t>(
        60, runtimeCfg.getInt64("agent.tor.bridge_epoch_window_seconds", 86400)));
    const uint64_t epoch = now / epochWindowSec;
    const size_t desiredSubsetCount = static_cast<size_t>(std::max<uint32_t>(1, cfg.tor.bridgeMinPoolSize == 0 ? 2u : cfg.tor.bridgeMinPoolSize));
    auto subset = core::selectDeterministicObfs4BridgeSubset(
        bridges, torBridgeSubsetNodeIdHint(), epoch, desiredSubsetCount);
    if (subset.empty()) {
        const size_t take = std::min(bridges.size(), desiredSubsetCount);
        subset.assign(bridges.begin(), bridges.begin() + take);
    }
    if (subset.empty()) return false;

    json snapshot;
    snapshot["version"] = 1;
    snapshot["trigger"] = trigger;
    snapshot["savedAt"] = now;
    snapshot["epochWindowSeconds"] = epochWindowSec;
    snapshot["epoch"] = epoch;
    snapshot["runtimeMode"] = configuredTorRuntimeMode();
    snapshot["socksHost"] = configuredTorSocksHost();
    snapshot["socksPort"] = configuredTorSocksPort();
    snapshot["controlPort"] = configuredTorControlPort();
    snapshot["bridgeTransport"] = cfg.tor.bridgeTransport;
    snapshot["bridgeSource"] = cfg.tor.bridgeSource;
    snapshot["bridgePoolCount"] = bridges.size();
    snapshot["selectedCount"] = subset.size();
    snapshot["validationTotalLines"] = validation.totalLines;
    snapshot["validationInvalidLines"] = validation.invalidLines;
    snapshot["validationMalformedBridgeLines"] = validation.malformedBridgeLines;
    snapshot["nodeIdHash"] = crypto::toHex(crypto::sha256(torBridgeSubsetNodeIdHint()));
    snapshot["selectedBridges"] = subset;

    std::error_code ec;
    std::filesystem::create_directories(torLastKnownGoodBridgeSubsetPath().parent_path(), ec);
    std::ofstream out(torLastKnownGoodBridgeSubsetPath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        utils::Logger::warn("Failed to persist last-known-good Tor bridge subset snapshot (open failed)");
        return false;
    }
    out << snapshot.dump(2) << "\n";
    out.close();
    if (!out) {
        utils::Logger::warn("Failed to persist last-known-good Tor bridge subset snapshot (write failed)");
        return false;
    }

    agentTorBridgeSubsetPersistCount_.fetch_add(1);
    agentTorBridgeSubsetLastPersistAt_.store(now);
    agentTorBridgeSubsetLastEpoch_.store(epoch);
    agentTorBridgeSubsetLastCount_.store(static_cast<uint32_t>(subset.size()));
    utils::Logger::info("Persisted last-known-good Tor bridge subset: count=" +
                       std::to_string(subset.size()) + " epoch=" + std::to_string(epoch) +
                       " trigger=" + trigger);
    return true;
}

void updateAndLogTorReadinessState(bool torRequired, bool torReachable, bool torReadyForWeb, bool torDegraded) {
    core::TorBootstrapStateInput in;
    in.torRequired = torRequired;
    in.torSocksReachable = torReachable;
    in.torWebReady = torReadyForWeb;
    in.torDegraded = torDegraded;

    const std::string webProbeError = getTorWebProbeLastError();
    const std::string state = core::evaluateTorBootstrapState(in);
    const std::string reason = core::evaluateTorBootstrapReasonCode(in, webProbeError);
    const uint32_t percent = core::evaluateTorBootstrapPercent(in);
    const bool onionReady = core::evaluateTorReadyForOnion(in);

    agentTorBootstrapPercent_.store(percent);
    agentTorOnionReady_.store(onionReady);

    std::string prevState;
    std::string prevReason;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(agentTorReadinessMtx_);
        prevState = agentTorReadinessState_;
        prevReason = agentTorBootstrapReasonCode_;
        changed = (prevState != state) || (prevReason != reason);
        agentTorReadinessState_ = state;
        agentTorBootstrapReasonCode_ = reason;
    }
    if (!changed) return;

    const std::string detail =
        "state=" + state +
        " reason=" + reason +
        " percent=" + std::to_string(percent) +
        " socks=" + std::string(torReachable ? "yes" : "no") +
        " web=" + std::string(torReadyForWeb ? "yes" : "no") +
        " onion=" + std::string(onionReady ? "yes" : "no") +
        (webProbeError.empty() ? std::string() : (" webProbeError=" + webProbeError));

    if (prevState.empty() && prevReason.empty()) {
        utils::Logger::info("Tor readiness state initialized: " + detail);
    } else {
        utils::Logger::info("Tor readiness transition: " +
                            prevState + "/" + prevReason + " -> " +
                            state + "/" + reason + " (" + detail + ")");
    }

    if (torReadyForWeb && (state == "WEB_READY" || state == "WEB_READY_DEGRADED")) {
        (void)persistLastKnownGoodBridgeSubset("tor_readiness_transition");
    }
}

std::string getTorBootstrapReasonCodeCached() const {
    std::lock_guard<std::mutex> lock(agentTorReadinessMtx_);
    return agentTorBootstrapReasonCode_;
}

bool refreshTorWebReadiness(bool torSocksReachable, bool force = false) {
    if (!torSocksReachable) {
        agentTorWebReady_.store(false);
        agentTorWebProbeConsecutiveFailures_.store(0);
        agentTorWebProbeConsecutiveSuccesses_.store(0);
        agentTorWebProbeExitCode_.store(-1);
        setTorWebProbeLastError("socks_unreachable");
        return false;
    }

    auto& cfg = utils::Config::instance();
    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    const uint64_t intervalSec = static_cast<uint64_t>(std::max<int64_t>(
        3, cfg.getInt64("agent.tor.web_probe_interval_seconds", 15)));
    const uint64_t lastProbeAt = agentTorWebProbeLastAt_.load();
    if (!force && lastProbeAt != 0 && now >= lastProbeAt && (now - lastProbeAt) < intervalSec) {
        return agentTorWebReady_.load();
    }

    bool expected = false;
    if (!agentTorWebProbeInFlight_.compare_exchange_strong(expected, true)) {
        return agentTorWebReady_.load();
    }

    const auto parseProbeUrls = [](const std::string& raw) {
        std::vector<std::string> out;
        std::string cur;
        auto flush = [&]() {
            std::string v = cur;
            cur.clear();
            const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            v.erase(v.begin(), std::find_if(v.begin(), v.end(), notSpace));
            v.erase(std::find_if(v.rbegin(), v.rend(), notSpace).base(), v.end());
            if (v.empty()) return;
            if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
        };
        for (char ch : raw) {
            if (ch == ',' || ch == ';' || ch == '\n' || ch == '\r') {
                flush();
            } else {
                cur.push_back(ch);
            }
        }
        flush();
        return out;
    };

    const std::string probeUrlSingle =
        cfg.getString("agent.tor.web_probe_url", "http://detectportal.firefox.com/success.txt");
    std::vector<std::string> probeUrls = parseProbeUrls(
        cfg.getString("agent.tor.web_probe_urls",
                      "http://detectportal.firefox.com/success.txt,http://neverssl.com/,https://check.torproject.org/api/ip,https://example.com/"));
    if (probeUrls.empty()) probeUrls.push_back(probeUrlSingle);

    const uint32_t probeRetries = static_cast<uint32_t>(std::clamp<int64_t>(
        cfg.getInt64("agent.tor.web_probe_retries", 2), 1, 8));
    const uint32_t retryDelayMs = static_cast<uint32_t>(std::clamp<int64_t>(
        cfg.getInt64("agent.tor.web_probe_retry_delay_ms", 350), 0, 5000));
    const uint32_t failStreakToDegrade = static_cast<uint32_t>(std::clamp<int64_t>(
        cfg.getInt64("agent.tor.web_probe_fail_streak_to_degrade", 2), 1, 12));
    const uint32_t successStreakToReady = static_cast<uint32_t>(std::clamp<int64_t>(
        cfg.getInt64("agent.tor.web_probe_success_streak_to_ready", 1), 1, 12));
    const uint32_t timeoutSec = static_cast<uint32_t>(std::clamp<int64_t>(
        cfg.getInt64("agent.tor.web_probe_timeout_seconds", 15), 2, 60));

    web::CurlFetchOptions opts;
    opts.socksProxyHostPort = configuredTorSocksHost() + ":" + std::to_string(configuredTorSocksPort());
    opts.timeoutSeconds = timeoutSec;
    opts.maxBytes = 1024;
    opts.followRedirects = true;
    opts.userAgent = "Mozilla/5.0 (compatible; SynapseNet-TorProbe/0.1)";

    const uint64_t rotation = agentTorWebProbeUrlRotation_.fetch_add(1);
    const size_t startIdx = probeUrls.empty() ? 0 : static_cast<size_t>(rotation % probeUrls.size());

    bool ok = false;
    int lastExitCode = -1;
    std::string lastError;

    for (uint32_t attempt = 0; attempt < probeRetries && !ok; ++attempt) {
        for (size_t i = 0; i < probeUrls.size(); ++i) {
            const size_t idx = (startIdx + i) % probeUrls.size();
            const auto probeResult = web::curlFetch(probeUrls[idx], opts);
            lastExitCode = probeResult.exitCode;
            if (probeResult.exitCode == 0) {
                ok = true;
                lastError.clear();
                break;
            }
            lastError = probeResult.error.empty()
                ? ("curl_exit=" + std::to_string(probeResult.exitCode))
                : probeResult.error;
        }
        if (!ok && attempt + 1 < probeRetries && retryDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
        }
    }

    const bool prevReady = agentTorWebReady_.load();
    bool effectiveReady = false;
    if (ok) {
        agentTorWebProbeConsecutiveFailures_.store(0);
        const uint32_t successStreak = agentTorWebProbeConsecutiveSuccesses_.fetch_add(1) + 1;
        effectiveReady = prevReady || successStreak >= successStreakToReady;
        if (effectiveReady) {
            agentTorWebProbeLastOkAt_.store(now);
            setTorWebProbeLastError("");
        } else {
            setTorWebProbeLastError("warming_up");
        }
    } else {
        agentTorWebProbeConsecutiveSuccesses_.store(0);
        const uint32_t failStreak = agentTorWebProbeConsecutiveFailures_.fetch_add(1) + 1;
        effectiveReady = prevReady && failStreak < failStreakToDegrade;
        if (effectiveReady) {
            setTorWebProbeLastError("");
        } else {
            setTorWebProbeLastError(lastError.empty() ? "curl_exit=-1" : lastError);
        }
    }

    agentTorWebReady_.store(effectiveReady);
    agentTorWebProbeLastAt_.store(now);
    agentTorWebProbeExitCode_.store(lastExitCode);
    agentTorWebProbeInFlight_.store(false);
    return effectiveReady;
}

bool startManagedTorRuntime() {
    const std::string torBin = utils::Config::instance().getString("agent.tor.binary", "tor");
    const std::string host = configuredTorSocksHost();
    const uint16_t socksPort = configuredTorSocksPort();
    const uint16_t controlPort = configuredTorControlPort();
    std::filesystem::path torDir = managedTorDirPath();
    std::filesystem::path pidFile = managedTorPidFilePath();

    const auto startup = core::runManagedTorStartup(
        allowManagedTorAutostart(),
        40,
        [&]() { return probeTorSocks(); },
        [&]() {
            const std::string checkCmd = torBin + " --version > /dev/null 2>&1";
            return std::system(checkCmd.c_str()) == 0;
        },
        [&]() {
            std::error_code ec;
            std::filesystem::create_directories(torDir, ec);
            return !ec;
        },
        [&]() {
#ifndef _WIN32
            return spawnManagedTorProcess(torBin, host, socksPort, controlPort, torDir, pidFile);
#else
            std::ostringstream cmd;
            cmd << torBin
                << " --quiet"
                << " --SocksPort " << host << ":" << socksPort
                << " --ControlPort 127.0.0.1:" << controlPort
                << " --DataDirectory \"" << torDir.string() << "\""
                << " --PidFile \"" << pidFile.string() << "\""
                << " --RunAsDaemon 1"
                << " > NUL 2>&1";
            return std::system(cmd.str().c_str()) == 0;
#endif
        },
        [&]() -> std::optional<int64_t> {
            int64_t pid = 0;
            if (readManagedTorPidFile(&pid)) return pid;
            return std::nullopt;
        },
        [&](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); });

    if (startup.managedPid > 0) {
        managedTorPid_.store(startup.managedPid);
    }
    return startup.socksReachable;
}

void resetManagedTorRestartBackoffState() {
    agentTorManagedRestartConsecutiveFailures_.store(0);
    agentTorManagedRestartNextAllowedAt_.store(0);
}

bool maybeStartManagedTorRuntimeWithBackoff(bool allowStartManagedTor, const char* context) {
    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    const bool torRequired = agentTorRequired_.load();
    const bool torReachable = probeTorSocks();
    if (torReachable) {
        resetManagedTorRestartBackoffState();
        return true;
    }

    auto& cfg = utils::Config::instance();
    const uint32_t baseBackoffSec = static_cast<uint32_t>(std::clamp<int64_t>(
        cfg.getInt64("agent.tor.managed_restart_backoff_base_seconds", 2), 1, 300));
    const uint32_t maxBackoffSec = static_cast<uint32_t>(std::clamp<int64_t>(
        cfg.getInt64("agent.tor.managed_restart_backoff_max_seconds", 60), baseBackoffSec, 3600));

    core::ManagedTorRestartGateInput gateIn;
    gateIn.torRequired = torRequired;
    gateIn.allowStartManagedTor = allowStartManagedTor;
    gateIn.allowManagedAutostart = allowManagedTorAutostart();
    gateIn.torSocksReachable = false;
    gateIn.now = now;
    gateIn.nextAllowedAttemptAt = agentTorManagedRestartNextAllowedAt_.load();
    const auto gate = core::evaluateManagedTorRestartGate(gateIn);
    if (!gate.shouldAttempt) {
        if (gate.reason == "backoff") {
            agentTorManagedRestartBackoffSkips_.fetch_add(1);
        }
        return false;
    }

    bool expected = false;
    if (!agentTorManagedRestartInFlight_.compare_exchange_strong(expected, true)) {
        return false;
    }

    bool started = false;
    const uint64_t attemptAt = static_cast<uint64_t>(std::time(nullptr));
    agentTorManagedRestartLastAttemptAt_.store(attemptAt);
    if (probeTorSocks()) {
        started = true;
    } else {
        started = startManagedTorRuntime();
    }

    if (started) {
        const uint32_t failuresBefore = agentTorManagedRestartConsecutiveFailures_.exchange(0);
        agentTorManagedRestartNextAllowedAt_.store(0);
        if (failuresBefore > 0) {
            utils::Logger::info(std::string("Managed Tor bootstrap recovered after restart retries") +
                                " (failures=" + std::to_string(failuresBefore) +
                                (context ? ", context=" + std::string(context) : std::string()) + ")");
        }
    } else {
        const uint32_t failures = agentTorManagedRestartConsecutiveFailures_.fetch_add(1) + 1;
        const uint32_t backoffSec =
            core::evaluateManagedTorRestartBackoffSeconds(failures, baseBackoffSec, maxBackoffSec);
        const uint64_t nextAllowedAt = attemptAt + backoffSec;
        agentTorManagedRestartNextAllowedAt_.store(nextAllowedAt);
        utils::Logger::warn(std::string("Managed Tor bootstrap retry backoff scheduled") +
                            " (failures=" + std::to_string(failures) +
                            ", backoff=" + std::to_string(backoffSec) + "s" +
                            (context ? ", context=" + std::string(context) : std::string()) + ")");
    }

    agentTorManagedRestartInFlight_.store(false);
    return started;
}
