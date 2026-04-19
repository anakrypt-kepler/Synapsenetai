#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <ctime>
#include <vector>
#include <array>
#include <deque>
#include <memory>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include <random>
#include <filesystem>
#include <algorithm>
#include <utility>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <optional>
#include <cctype>
#include <cerrno>
#include <regex>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#include <sys/time.h>
#ifndef _WIN32
#include <sys/wait.h>
#include <signal.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>

#include "core/ledger.h"
#include "core/knowledge.h"
#include "core/transfer.h"
#include "core/consensus.h"
#include "core/poe_v1_engine.h"
#include "core/poe_bootstrap_policy.h"
#include "core/update_bundle.h"
#include "core/update_installer.h"
#include "core/implant_compatibility.h"
#include "core/implant_safety_pipeline.h"
#include "core/agent_coordination.h"
#include "core/agent_draft_queue.h"
#include "core/agent_submission_pipeline.h"
#include "core/agent_score.h"
#include "core/agent_storage.h"
#include "core/agent_runtime.h"
#include "core/agent_scheduler.h"
#include "core/tor_process_guard.h"
#include "core/tor_bridge_provider.h"
#include "core/tor_bridge_utils.h"
#include "core/tor_peer_identity.h"
#include "core/tor_route_policy.h"
#include "cli/cli_parser.h"
#include "cli/cli_rpc_client.h"
#include "config/config_loader.h"
#include "tui/tui_command_bridge.h"
#if SYNAPSE_BUILD_TUI
#include "tui/tui.h"
#include "tui/tui_runtime.h"
#include "tui/tui_snapshot_builder.h"
#endif
#include "network/network.h"
#include "network/discovery.h"
#include "node/node_config.h"
#include "node/node_init.h"
#include "node/maintenance_runtime.h"
#include "node/poe_runtime.h"
#include "node/node_runtime.h"
#include "node/node_status_runtime.h"
#include "node/tor_control_runtime.h"
#include "node/tor_status_runtime.h"
#include "rpc/rpc_ai_runtime.h"
#include "rpc/rpc_commands.h"
#include "rpc/rpc_model_views.h"
#include "rpc/rpc_naan_runtime.h"
#include "rpc/rpc_naan_views.h"
#include "rpc/rpc_node_views.h"
#include "rpc/rpc_poe_views.h"
#include "rpc/rpc_update_runtime.h"
#include "rpc/rpc_update_views.h"
#include "model/model_loader.h"
#include "model/model_access.h"
#include "crypto/address.h"
#include "crypto/crypto.h"
#include "crypto/keys.h"
#include "database/database.h"
#include "utils/logger.h"
#include "utils/config.h"
#include "utils/single_instance.h"
#include "utils/utils.h"
#include "privacy/privacy.h"
#include "python/sandbox.h"
#include "quantum/quantum_security.h"
#include "infrastructure/messages.h"
#include "web/rpc_server.h"
#include "web/curl_fetch.h"
#include "web/web.h"
#include "node/synapse_net.h"
#include "../third_party/llama.cpp/vendor/nlohmann/json.hpp"

namespace synapse {

using json = nlohmann::json;

std::atomic<bool> g_running{true};
std::atomic<bool> g_reloadConfig{false};
std::atomic<bool> g_daemonMode{false};
std::atomic<int> g_shutdownSignal{0};

static const char* shutdownSignalName(int signal) {
    switch (signal) {
        case SIGINT: return "SIGINT";
        case SIGTERM: return "SIGTERM";
#ifndef _WIN32
        case SIGHUP: return "SIGHUP";
#endif
        default: return "signal";
    }
}

static std::string shutdownReasonFromSignalState() {
    int signal = g_shutdownSignal.load();
    if (signal == 0) return "requested";
    return std::string("signal:") + shutdownSignalName(signal);
}

static std::string makeTorSessionDisplayId() {
    std::array<uint8_t, 6> bytes{};
    std::random_device rd;
    for (auto& byte : bytes) byte = static_cast<uint8_t>(rd());
    return "tor:" + crypto::toHex(bytes.data(), bytes.size());
}

std::string formatUptime(uint64_t seconds);

struct NodeStats {
    uint64_t uptime = 0;
    uint64_t peersConnected = 0;
    uint64_t peersInbound = 0;
    uint64_t peersOutbound = 0;
    uint64_t knowledgeEntries = 0;
    uint64_t transactionsProcessed = 0;
    uint64_t blocksValidated = 0;
    uint64_t modelRequests = 0;
    uint64_t bytesReceived = 0;
    uint64_t bytesSent = 0;
    double syncProgress = 0.0;
    double cpuUsage = 0.0;
    uint64_t memoryUsage = 0;
    uint64_t diskUsage = 0;
};

struct SystemInfo {
    std::string osName;
    std::string osVersion;
    std::string architecture;
    uint32_t cpuCores;
    uint64_t totalMemory;
    uint64_t availableMemory;
    uint64_t totalDisk;
    uint64_t availableDisk;
};

class SynapseNet : public tui::TuiCommandHandlerProvider, public rpc::RpcCommandHandlerProvider {
public:
    SynapseNet() : running_(false), startTime_(0), syncProgress_(0.0), torSessionDisplayId_(makeTorSessionDisplayId()) {}
    ~SynapseNet() { shutdown(); }
    
    bool initialize(const NodeConfig& config) {
        config_ = config;
        for (char& c : config_.poeValidatorMode) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (config_.poeValidatorMode != "stake") config_.poeValidatorMode = "static";
        if (config_.poeMinStake.empty()) config_.poeMinStake = "0";
        utils::Config::instance().setDataDir(config_.dataDir);
        
	        utils::Logger::init(config_.dataDir + "/synapsenet.log");
	        utils::Logger::enableConsole(!config_.tui);
	        setLogLevel(config_.logLevel);
	        utils::Logger::info("SynapseNet v0.1.0 starting...");
	        utils::Logger::info("Data directory: " + config_.dataDir);
        
        if (!config_.tui) std::cout << "Loading configuration..." << std::endl;
        if (!loadConfiguration()) {
            utils::Logger::error("Failed to load configuration");
            return false;
        }
        
        if (!config_.tui) std::cout << "Initializing database..." << std::endl;
        if (!initDatabase()) return false;
        
        if (!config_.tui) std::cout << "Initializing crypto..." << std::endl;
        if (!initCrypto()) return false;
        
        if (!config_.tui) std::cout << "Initializing quantum security..." << std::endl;
        if (!initQuantumSecurity()) return false;

        if (!config_.cli) {
            if (!config_.tui) std::cout << "Initializing network..." << std::endl;
            if (!initNetwork()) return false;
        }
        
	        if (!config_.tui) std::cout << "Initializing core..." << std::endl;
	        if (!initCore()) return false;

	        bool needsModel = !config_.cli;
	        if (config_.cli && !config_.commandArgs.empty()) {
	            const std::string cmd = config_.commandArgs[0];
	            needsModel = (cmd == "model" || cmd == "ai");
	        }

	        if (needsModel) {
	            if (!config_.tui) std::cout << "Initializing model..." << std::endl;
	            if (!initModel()) return false;
	        }

	        if (!config_.cli) {
	            if (!config_.tui) std::cout << "Initializing privacy..." << std::endl;
	            if (!initPrivacy()) return false;

	            if (!config_.tui) std::cout << "Initializing RPC..." << std::endl;
            if (!initRPC()) return false;

            if (!config_.tui) std::cout << "Initializing mempool..." << std::endl;
            if (!initMempool()) return false;
        }
        
        utils::Logger::info("All subsystems initialized successfully");
        if (!config_.tui) std::cout << "Initialization complete!" << std::endl;
        return true;
    }

    uint64_t parseTuiAmountAtoms(const std::string& value) override {
        return parseNgtAtomic(value);
    }

    std::string handleTuiSendCommand(const std::string& to, uint64_t amountAtoms) override {
        uint64_t feeAtoms = 0;
        json out;
        out["status"] = "ok";
        out["txid"] = createAndSubmitPaymentTx(to, amountAtoms, feeAtoms);
        out["feeAtoms"] = feeAtoms;
        return out.dump();
    }

    std::string handleTuiPoeSubmit(const std::string& paramsJson) override {
        return handleRpcPoeSubmit(paramsJson);
    }

    std::string handleTuiPoeSubmitCode(const std::string& paramsJson) override {
        return handleRpcPoeSubmitCode(paramsJson);
    }

    std::string handleTuiPoeEpoch(const std::string& paramsJson) override {
        return handleRpcPoeEpoch(paramsJson);
    }

	private:
        struct RemoteOfferCache {
            synapse::RemoteModelOfferMessage offer;
            std::string peerId;
            uint64_t receivedAt = 0;
        };

        enum class UpdateManifestAccept {
            ACCEPTED,
            DUPLICATE,
            REJECTED
        };

        struct NaanAbuseClassifierPolicy {
            uint32_t spamRejectThreshold = 3;
            uint32_t spamViolationSteps = 1;
            uint32_t invalidCitationViolationSteps = 1;
            uint32_t policyViolationSteps = 1;
        };

        struct NaanAbuseClassification {
            uint32_t spamViolations = 0;
            uint32_t invalidCitationViolations = 0;
            uint32_t policyViolations = 0;
        };

        struct NaanUiEvent {
            uint64_t timestamp = 0;
            std::string category;
            std::string message;
        };

	    json parseRpcParams(const std::string& paramsJson) const {
	    if (paramsJson.empty()) {
	        return json::object();
	    }
    json parsed = json::parse(paramsJson, nullptr, false);
    if (parsed.is_discarded()) {
        throw std::runtime_error("Invalid JSON params");
    }
    if (parsed.is_array()) {
        if (parsed.empty()) return json::object();
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

crypto::Hash256 parseHash256Hex(const std::string& hex) const {
    crypto::Hash256 out{};
    auto bytes = crypto::fromHex(hex);
    if (bytes.size() != out.size()) {
        throw std::runtime_error("Expected 32-byte hex string");
    }
    std::memcpy(out.data(), bytes.data(), out.size());
    return out;
}

static std::string safeDump(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

bool parsePublicKeyHex(const std::string& hex, crypto::PublicKey& out) const {
    auto bytes = crypto::fromHex(hex);
    if (bytes.size() != out.size()) return false;
    std::memcpy(out.data(), bytes.data(), out.size());
    return true;
}

bool parseSignatureHex(const std::string& hex, crypto::Signature& out) const {
    auto bytes = crypto::fromHex(hex);
    if (bytes.size() != out.size()) return false;
    std::memcpy(out.data(), bytes.data(), out.size());
    return true;
}

bool canConnectTcp(const std::string& host, uint16_t port, uint32_t timeoutMs = 1200) const {
    if (host.empty() || port == 0) return false;

    struct addrinfo hints {};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    std::string portStr = std::to_string(port);
    struct addrinfo* results = nullptr;
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &results) != 0) {
        return false;
    }

    bool ok = false;
    for (struct addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        int rc = connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
        if (rc == 0) {
            ok = true;
            close(fd);
            break;
        }
        if (errno != EINPROGRESS) {
            close(fd);
            continue;
        }

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv{};
        tv.tv_sec = static_cast<long>(timeoutMs / 1000);
        tv.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);
        int sel = select(fd + 1, nullptr, &wfds, nullptr, &tv);
        if (sel > 0 && FD_ISSET(fd, &wfds)) {
            int err = 0;
            socklen_t errLen = sizeof(err);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errLen) == 0 && err == 0) {
                ok = true;
                close(fd);
                break;
            }
        }
        close(fd);
    }

    freeaddrinfo(results);
    return ok;
}

#include "node/main_parts/tor_management.inc"

core::TorRoutePolicyDecision refreshTorRoutePolicy(bool allowStartManagedTor) {
    const bool torRequired = agentTorRequired_.load();
    bool torReachable = probeTorSocks();

    if (torRequired && !torReachable && allowStartManagedTor) {
        torReachable = maybeStartManagedTorRuntimeWithBackoff(allowStartManagedTor, "refresh_tor_route_policy");
        if (torReachable) {
            agentTorManaged_.store(true);
        }
    }
    if (torReachable) {
        resetManagedTorRestartBackoffState();
    }

    agentTorReachable_.store(torReachable);
    refreshTorWebReadiness(torReachable, false);

    core::TorRoutePolicyInput routeIn;
    routeIn.torRequired = torRequired;
    routeIn.torReachable = torReachable;
    routeIn.allowClearnetFallback = agentAllowClearnetFallback_.load();
    routeIn.allowP2PFallback = agentAllowP2PFallback_.load();
    const auto route = core::evaluateTorRoutePolicy(routeIn);
    agentTorDegraded_.store(route.torDegraded);
    updateAndLogTorReadinessState(torRequired, torReachable, agentTorWebReady_.load(), route.torDegraded);
    return route;
}

std::string redactPotentialSecrets(const std::string& input) const {
    std::string out = input;
    static const std::regex pattern(
        "(api[_-]?key|token|authorization|bearer|private[ _-]?key|mnemonic|seed[ _-]?phrase|secret|password)\\s*[:=]\\s*[^\\s\\\"'<>]+",
        std::regex::icase
    );
    out = std::regex_replace(out, pattern, "$1=[REDACTED]");
    return out;
}

bool containsPotentialSecrets(const std::string& input) const {
    static const std::regex pattern(
        "(api[_-]?key|token|authorization|bearer|private[ _-]?key|mnemonic|seed[ _-]?phrase|secret|password)\\s*[:=]\\s*[^\\s\\\"'<>]+",
        std::regex::icase
    );
    return std::regex_search(input, pattern);
}

json redactPotentialSecretsInJson(const json& value, bool* redacted = nullptr) const {
    if (value.is_string()) {
        const std::string current = value.get<std::string>();
        const std::string safe = redactPotentialSecrets(current);
        if (redacted && safe != current) *redacted = true;
        return safe;
    }
    if (value.is_array()) {
        json out = json::array();
        for (const auto& item : value) {
            out.push_back(redactPotentialSecretsInJson(item, redacted));
        }
        return out;
    }
    if (value.is_object()) {
        json out = json::object();
        for (auto it = value.begin(); it != value.end(); ++it) {
            out[it.key()] = redactPotentialSecretsInJson(it.value(), redacted);
        }
        return out;
    }
    return value;
}

void reloadImplantUpdatePoliciesFromConfig() {
    auto& cfg = utils::Config::instance();

    core::ImplantCompatibilityPolicy compatibility;
    compatibility.protocolMin = static_cast<uint32_t>(std::max(1, cfg.getInt("implant.update.protocol_min", 1)));
    compatibility.protocolMax = static_cast<uint32_t>(std::max<int>(
        static_cast<int>(compatibility.protocolMin),
        cfg.getInt("implant.update.protocol_max", static_cast<int>(compatibility.protocolMin))
    ));
    compatibility.halVersion = static_cast<uint32_t>(std::max(1, cfg.getInt("implant.hal.version", 1)));
    compatibility.intentSchemaVersion = static_cast<uint32_t>(std::max(1, cfg.getInt("implant.intent.schema_version", 1)));
    compatibility.requireSafetyGate = cfg.getBool("implant.update.require_safety_gate", true);

    core::ImplantUpdateGovernancePolicy governance;
    governance.requireTrustedSigner = cfg.getBool("implant.update.require_trusted_signer", true);
    governance.minSignerApprovals = static_cast<uint32_t>(std::max(1, cfg.getInt("implant.update.min_signer_approvals", 1)));

    for (const auto& signerHex : cfg.getList("implant.update.trusted_signers")) {
        crypto::PublicKey signer{};
        if (!parsePublicKeyHex(signerHex, signer)) {
            utils::Logger::warn("Ignoring invalid implant.update.trusted_signers entry (expected 32-byte hex): " + signerHex);
            continue;
        }
        governance.trustedSigners.push_back(signer);
    }

    std::sort(governance.trustedSigners.begin(), governance.trustedSigners.end(), [](const crypto::PublicKey& a, const crypto::PublicKey& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
    });
    governance.trustedSigners.erase(std::unique(governance.trustedSigners.begin(), governance.trustedSigners.end(), [](const crypto::PublicKey& a, const crypto::PublicKey& b) {
        return a == b;
    }), governance.trustedSigners.end());

    {
        std::lock_guard<std::mutex> lock(implantPolicyMtx_);
        implantCompatibilityPolicy_ = compatibility;
        implantUpdatePolicy_ = governance;
    }

    refreshSecurityPolicyHashes("config_reload");
}

json implantUpdatePolicyToJson() {
    core::ImplantCompatibilityPolicy compatibility;
    core::ImplantUpdateGovernancePolicy governance;
    {
        std::lock_guard<std::mutex> lock(implantPolicyMtx_);
        compatibility = implantCompatibilityPolicy_;
        governance = implantUpdatePolicy_;
    }

    json out;
    out["protocolMin"] = compatibility.protocolMin;
    out["protocolMax"] = compatibility.protocolMax;
    out["halVersion"] = compatibility.halVersion;
    out["intentSchemaVersion"] = compatibility.intentSchemaVersion;
    out["requireSafetyGate"] = compatibility.requireSafetyGate;
    out["requireTrustedSigner"] = governance.requireTrustedSigner;
    out["minSignerApprovals"] = governance.minSignerApprovals;
    json trusted = json::array();
    for (const auto& signer : governance.trustedSigners) {
        trusted.push_back(crypto::toHex(signer));
    }
    out["trustedSigners"] = trusted;
    return out;
}

std::string hashWithDomain(const std::string& domain, const std::string& payload) const {
    std::vector<uint8_t> bytes;
    bytes.reserve(domain.size() + payload.size());
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return crypto::toHex(crypto::sha256(bytes.data(), bytes.size()));
}

json sandboxPolicyToJson(const core::SandboxPolicy& policy) const {
    json out;
    out["allowSideEffects"] = policy.allowSideEffects;
    out["workspaceRoot"] = policy.workspaceRoot;
    out["readRoots"] = policy.readRoots;
    out["writeRoots"] = policy.writeRoots;
    out["deniedPathPrefixes"] = policy.deniedPathPrefixes;
    out["deniedFileNames"] = policy.deniedFileNames;
    out["deniedExtensions"] = policy.deniedExtensions;
    out["maxToolPayloadBytes"] = policy.maxToolPayloadBytes;
    json allowlist = json::array();
    for (auto capability : policy.allowlist) {
        allowlist.push_back(core::capabilityToString(capability));
    }
    out["allowlist"] = allowlist;
    return out;
}

json taskSchedulerPolicyToJson(const core::AgentTaskSchedulerPolicy& policy) const {
    json out;
    out["tickSeconds"] = policy.tickSeconds;
    out["epochTicks"] = policy.epochTicks;
    out["starvationThresholdTicks"] = policy.starvationThresholdTicks;
    out["epochBudgetCpu"] = policy.epochBudget.cpu;
    out["epochBudgetRam"] = policy.epochBudget.ram;
    out["epochBudgetNetwork"] = policy.epochBudget.network;
    json priority = json::array();
    for (auto taskClass : policy.fixedPriority) {
        priority.push_back(core::agentTaskClassToString(taskClass));
    }
    out["fixedPriority"] = priority;
    json classes = json::array();
    for (size_t idx = 0; idx < core::kAgentTaskClassCount; ++idx) {
        json item;
        item["taskClass"] = core::agentTaskClassToString(static_cast<core::AgentTaskClass>(idx));
        item["cpu"] = policy.classes[idx].cost.cpu;
        item["ram"] = policy.classes[idx].cost.ram;
        item["network"] = policy.classes[idx].cost.network;
        item["minIntervalTicks"] = policy.classes[idx].minIntervalTicks;
        classes.push_back(item);
    }
    out["classes"] = classes;
    return out;
}

json scorePolicyToJson(const core::AgentScorePolicy& policy) const {
    json out;
    out["scoreMin"] = policy.scoreMin;
    out["scoreMax"] = policy.scoreMax;
    out["initialScore"] = policy.initialScore;
    out["decayNumerator"] = policy.decayNumerator;
    out["decayDenominator"] = policy.decayDenominator;
    out["acceptWeight"] = policy.acceptWeight;
    out["rejectWeight"] = policy.rejectWeight;
    out["violationWeight"] = policy.violationWeight;
    out["throttledBelowOrEqual"] = policy.throttledBelowOrEqual;
    out["reviewOnlyBelowOrEqual"] = policy.reviewOnlyBelowOrEqual;
    out["localDraftOnlyBelowOrEqual"] = policy.localDraftOnlyBelowOrEqual;
    out["localDraftRecoveryAbove"] = policy.localDraftRecoveryAbove;
    out["localDraftRecoveryCleanSteps"] = policy.localDraftRecoveryCleanSteps;
    out["normalBatchLimit"] = policy.normalBatchLimit;
    out["throttledBatchLimit"] = policy.throttledBatchLimit;
    out["reviewOnlyBatchLimit"] = policy.reviewOnlyBatchLimit;
    out["localDraftOnlyBatchLimit"] = policy.localDraftOnlyBatchLimit;
    return out;
}

json submissionPipelineConfigToJson(const core::AgentSubmissionPipelineConfig& config) const {
    json out;
    out["maxBatchSize"] = config.maxBatchSize;
    out["includeReviewRequired"] = config.includeReviewRequired;
    out["minDistinctReviewers"] = config.minDistinctReviewers;
    out["requireNonAuthorReviewer"] = config.requireNonAuthorReviewer;
    out["intentMinTitleBytes"] = config.intentMinTitleBytes;
    out["intentMinBodyBytes"] = config.intentMinBodyBytes;
    out["intentRequireCitationForText"] = config.intentRequireCitationForText;
    out["intentMinCitationsForText"] = config.intentMinCitationsForText;
    out["duplicateGateContentId"] = config.duplicateGateContentId;
    out["duplicateGateNoveltyBuckets"] = config.duplicateGateNoveltyBuckets;
    out["duplicateGateCitationGraph"] = config.duplicateGateCitationGraph;
    out["duplicateCitationGraphMaxHamming"] = config.duplicateCitationGraphMaxHamming;
    out["citationSanityMaxCitations"] = config.citationSanityMaxCitations;
    out["citationSanityRejectDuplicateCitations"] = config.citationSanityRejectDuplicateCitations;
    out["citationSanityRequireKnownCitations"] = config.citationSanityRequireKnownCitations;
    out["citationSanityRejectSelfReference"] = config.citationSanityRejectSelfReference;
    out["citationSanityRejectIntraSetCycles"] = config.citationSanityRejectIntraSetCycles;
    out["citationSanityMaxIntraSetEdges"] = config.citationSanityMaxIntraSetEdges;
    out["citationSanityMinCorroboratingCitations"] = config.citationSanityMinCorroboratingCitations;
    out["citationSanityMinDistinctCitationAuthors"] = config.citationSanityMinDistinctCitationAuthors;
    return out;
}

json connectorAbusePolicyToJson(const web::ConnectorAbusePolicy& policy) const {
    json out;
    out["policyBlockDeltaThreshold"] = policy.policyBlockDeltaThreshold;
    out["failureDeltaThreshold"] = policy.failureDeltaThreshold;
    out["cooldownTicks"] = policy.cooldownTicks;
    out["violationPenaltySteps"] = policy.violationPenaltySteps;
    return out;
}

std::string computeNaanPolicyHash() const {
    auto& cfg = utils::Config::instance();
    json out;
    out["sandbox"] = sandboxPolicyToJson(agentRuntimeSandbox_.getPolicy());
    out["taskScheduler"] = taskSchedulerPolicyToJson(naanTaskScheduler_.policy());
    out["score"] = scorePolicyToJson(agentScore_.policy());
    out["adaptiveScheduler"] = {
        {"draftNormal", agentAdaptiveScheduler_.policy().draftIntervalNormalSeconds},
        {"draftThrottled", agentAdaptiveScheduler_.policy().draftIntervalThrottledSeconds},
        {"draftQuarantined", agentAdaptiveScheduler_.policy().draftIntervalQuarantinedSeconds},
        {"pipelineNormal", agentAdaptiveScheduler_.policy().pipelineIntervalNormalSeconds},
        {"pipelineThrottled", agentAdaptiveScheduler_.policy().pipelineIntervalThrottledSeconds},
        {"pipelineQuarantined", agentAdaptiveScheduler_.policy().pipelineIntervalQuarantinedSeconds},
        {"heartbeatNormal", agentAdaptiveScheduler_.policy().heartbeatIntervalNormalSeconds},
        {"heartbeatThrottled", agentAdaptiveScheduler_.policy().heartbeatIntervalThrottledSeconds},
        {"heartbeatQuarantined", agentAdaptiveScheduler_.policy().heartbeatIntervalQuarantinedSeconds}
    };
    out["submissionPipeline"] = submissionPipelineConfigToJson(agentSubmissionPipeline_.config());
    out["connectorAbuse"] = connectorAbusePolicyToJson(naanConnectorAbuseGuard_.getPolicy());
    out["routing"] = {
        {"torRequired", cfg.getBool("agent.tor.required", true)},
        {"allowClearnetFallback", cfg.getBool("agent.routing.allow_clearnet_fallback", false)},
        {"allowP2PFallback", cfg.getBool("agent.routing.allow_p2p_clearnet_fallback", false)}
    };
    out["retention"] = {
        {"maxSubmittedDrafts", cfg.getInt64("agent.retention.max_submitted_drafts", 0)},
        {"maxObservatoryHistory", cfg.getInt64("agent.retention.max_observatory_history", 0)},
        {"roomMaxMessages", cfg.getInt64("agent.retention.room.max_messages", 0)},
        {"roomRetentionSeconds", cfg.getInt64("agent.retention.room.seconds", 0)}
    };
    out["toolSchemas"] = json::array();
    for (const auto& schema : naanToolSchemas_) {
        json s;
        s["toolName"] = schema.toolName;
        s["capability"] = core::capabilityToString(schema.capability);
        s["requiredKeys"] = schema.requiredKeys;
        s["optionalKeys"] = schema.optionalKeys;
        s["maxPayloadBytes"] = schema.maxPayloadBytes;
        s["allowSideEffects"] = schema.allowSideEffects;
        s["requireExplicitSideEffectFlag"] = schema.requireExplicitSideEffectFlag;
        out["toolSchemas"].push_back(s);
    }
    return hashWithDomain("synapsenet:naan:policy:v1", out.dump());
}

std::string computeImplantPolicyHash() {
    return hashWithDomain("synapsenet:implant:policy:v1", implantUpdatePolicyToJson().dump());
}

std::pair<std::string, std::string> securityPolicyHashes() {
    std::lock_guard<std::mutex> lock(securityPolicyMtx_);
    return {naanPolicyHash_, implantPolicyHash_};
}

void refreshSecurityPolicyHashes(const std::string& context) {
    const std::string naanHash = computeNaanPolicyHash();
    const std::string implantHash = computeImplantPolicyHash();
    {
        std::lock_guard<std::mutex> lock(securityPolicyMtx_);
        naanPolicyHash_ = naanHash;
        implantPolicyHash_ = implantHash;
    }
    utils::Logger::info("security_policy_hash naan=" + naanHash + " implant=" + implantHash + " context=" + context);
}

void emitSecurityEvent(uint64_t atTimestamp,
                       const std::string& kind,
                       const std::string& severity,
                       const std::string& subject,
                       const json& details) {
    bool redacted = false;
    json safeDetails = redactPotentialSecretsInJson(details, &redacted);
    auto hashes = securityPolicyHashes();
    json payload;
    payload["event"] = "security_event";
    payload["kind"] = kind;
    payload["severity"] = severity;
    payload["subject"] = subject;
    payload["details"] = safeDetails;
    payload["policyHashNaan"] = hashes.first;
    payload["policyHashImplant"] = hashes.second;
    payload["redacted"] = redacted;
    appendNaanAuditEvent(atTimestamp, "security_event", subject, payload);
    naanSecurityEvents_.fetch_add(1);
    naanSecurityLastEventAt_.store(atTimestamp);
    if (severity == "high" || severity == "critical") {
        naanSecurityHighSeverityEvents_.fetch_add(1);
    }
    utils::Logger::warn("SECURITY_EVENT " + safeDump(payload));
}

std::vector<core::DetachedSignerApproval> detachedSignerApprovals(const crypto::Hash256& bundleId) {
    const std::string bundleHex = crypto::toHex(bundleId);
    std::lock_guard<std::mutex> lock(updateApprovalMtx_);
    auto it = updateDetachedApprovalsByBundle_.find(bundleHex);
    if (it == updateDetachedApprovalsByBundle_.end()) {
        return {};
    }
    return it->second;
}

void upsertDetachedSignerApproval(const crypto::Hash256& bundleId, const core::DetachedSignerApproval& approval) {
    const std::string bundleHex = crypto::toHex(bundleId);
    std::lock_guard<std::mutex> lock(updateApprovalMtx_);
    auto& approvals = updateDetachedApprovalsByBundle_[bundleHex];
    auto it = std::find_if(approvals.begin(), approvals.end(), [&](const core::DetachedSignerApproval& item) {
        return item.signer == approval.signer;
    });
    if (it == approvals.end()) {
        approvals.push_back(approval);
    } else {
        *it = approval;
    }
}

json detachedSignerApprovalsToJson(const crypto::Hash256& bundleId) {
    const std::string bundleHex = crypto::toHex(bundleId);
    std::vector<core::DetachedSignerApproval> approvals;
    {
        std::lock_guard<std::mutex> lock(updateApprovalMtx_);
        auto it = updateDetachedApprovalsByBundle_.find(bundleHex);
        if (it != updateDetachedApprovalsByBundle_.end()) {
            approvals = it->second;
        }
    }

    json out;
    out["bundleId"] = bundleHex;
    out["count"] = approvals.size();
    json items = json::array();
    for (const auto& approval : approvals) {
        json item;
        item["signer"] = crypto::toHex(approval.signer);
        item["signature"] = crypto::toHex(approval.signature);
        const auto digest = core::ImplantCompatibility::detachedApprovalHash(bundleId);
        item["signatureValid"] = crypto::verify(digest, approval.signature, approval.signer);
        items.push_back(item);
    }
    out["items"] = items;
    return out;
}

bool fetchStoredUpdateManifest(const crypto::Hash256& bundleId, core::UpdateManifest& outManifest) {
    std::string idHex = crypto::toHex(bundleId);
    std::lock_guard<std::mutex> lock(invMtx_);
    auto it = updateManifestsById_.find(idHex);
    if (it == updateManifestsById_.end()) return false;
    outManifest = it->second;
    return true;
}

bool validateImplantDistributionManifest(const core::UpdateManifest& manifest, bool safetyGatePassed, std::string* reason = nullptr) {
    core::ImplantCompatibilityPolicy compatibility;
    core::ImplantUpdateGovernancePolicy governance;
    std::vector<core::DetachedSignerApproval> approvals;
    {
        std::lock_guard<std::mutex> lock(implantPolicyMtx_);
        compatibility = implantCompatibilityPolicy_;
        governance = implantUpdatePolicy_;
    }
    approvals = detachedSignerApprovals(manifest.bundleId);
    return core::ImplantCompatibility::canDistributeManifest(manifest, safetyGatePassed, compatibility, governance, approvals, reason);
}

bool validateUpdateSignerThreshold(const core::UpdateManifest& manifest, std::string* reason = nullptr) {
    core::ImplantUpdateGovernancePolicy governance;
    {
        std::lock_guard<std::mutex> lock(implantPolicyMtx_);
        governance = implantUpdatePolicy_;
    }
    auto approvals = detachedSignerApprovals(manifest.bundleId);
    return core::ImplantCompatibility::verifyDetachedSignerApprovals(manifest, governance, approvals, reason);
}

core::UpdateInstaller::UpdatePolicy installerPolicyForManifest(const core::UpdateManifest& manifest) {
    core::ImplantUpdateGovernancePolicy governance;
    {
        std::lock_guard<std::mutex> lock(implantPolicyMtx_);
        governance = implantUpdatePolicy_;
    }

    core::UpdateInstaller::UpdatePolicy policy;
    if (governance.requireTrustedSigner && !governance.trustedSigners.empty()) {
        policy.allowedSigners = governance.trustedSigners;
        policy.minSignatures = std::max<uint32_t>(1, governance.minSignerApprovals);
        return policy;
    }

    policy.allowedSigners.push_back(manifest.signer);
    for (const auto& extra : manifest.additionalSignatures) {
        if (std::find(policy.allowedSigners.begin(), policy.allowedSigners.end(), extra.first) == policy.allowedSigners.end()) {
            policy.allowedSigners.push_back(extra.first);
        }
    }
    policy.minSignatures = 1;
    return policy;
}

std::optional<core::ImplantSafetyRecord> getImplantSafetyRecord(const crypto::Hash256& bundleId) {
    std::lock_guard<std::mutex> lock(implantSafetyMtx_);
    return implantSafetyPipeline_.getRecord(bundleId);
}

bool authorizeNaanTool(const core::ToolInvocation& invocation, const std::string& context) {
    core::RuntimeActionResult gate = agentRuntimeSandbox_.authorizeTool(invocation, naanToolSchemas_);
    if (gate != core::RuntimeActionResult::ALLOWED) {
        utils::Logger::warn("NAAN sandbox denied " + context + ": " + core::runtimeActionResultToString(gate));
        json details;
        details["toolName"] = invocation.toolName;
        details["context"] = context;
        details["gate"] = core::runtimeActionResultToString(gate);
        details["capability"] = core::capabilityToString(invocation.capability);
        details["payloadBytes"] = invocation.payloadBytes;
        emitSecurityEvent(static_cast<uint64_t>(std::time(nullptr)), "sandbox_denied", "high", invocation.toolName, details);
        return false;
    }
    return true;
}

bool sanitizeNaanPayload(json* payload, const std::string& context, uint64_t nowTimestamp) {
    if (payload == nullptr) return false;
    bool redacted = false;
    *payload = redactPotentialSecretsInJson(*payload, &redacted);
    const std::string encoded = payload->dump();
    if (containsPotentialSecrets(encoded)) {
        json details;
        details["context"] = context;
        details["reason"] = "secret_guardrail_blocked";
        emitSecurityEvent(nowTimestamp, "secret_guardrail_blocked", "critical", context, details);
        return false;
    }
    if (redacted) {
        json details;
        details["context"] = context;
        details["reason"] = "secret_guardrail_redacted";
        emitSecurityEvent(nowTimestamp, "secret_guardrail_redacted", "high", context, details);
    }
    return true;
}

static bool hasPrefix(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string classifyNaanEventCategory(const std::string& kind) const {
    if (kind.find("tor") != std::string::npos) return "tor";
    if (kind.find("connector") != std::string::npos) return "connector";
    if (kind.find("pipeline") != std::string::npos || kind.find("draft") != std::string::npos) return "pipeline";
    if (kind.find("reward") != std::string::npos || kind.find("epoch") != std::string::npos) return "reward";
    if (kind.find("quarantine") != std::string::npos || kind.find("score") != std::string::npos) return "quarantine";
    if (kind.find("security") != std::string::npos) return "security";
    if (kind.find("observatory") != std::string::npos || kind.find("room") != std::string::npos) return "timeline";
    return "scheduler";
}

void recordNaanUiEvent(uint64_t timestamp, const std::string& category, const std::string& message) {
    NaanUiEvent ev;
    ev.timestamp = timestamp;
    ev.category = category;
    ev.message = message;

    std::lock_guard<std::mutex> lock(naanUiEventsMtx_);
    naanUiEvents_.push_back(std::move(ev));
    while (naanUiEvents_.size() > 512) {
        naanUiEvents_.pop_front();
    }
}

std::vector<NaanUiEvent> snapshotNaanUiEvents() const {
    std::lock_guard<std::mutex> lock(naanUiEventsMtx_);
    return std::vector<NaanUiEvent>(naanUiEvents_.begin(), naanUiEvents_.end());
}

bool configureNaanStorage() {
    auto& cfg = utils::Config::instance();
    auto readBoundedU64 = [&](const char* key, int64_t fallback, int64_t minValue, int64_t maxValue) {
        int64_t v = cfg.getInt64(key, fallback);
        if (v < minValue) v = minValue;
        if (v > maxValue) v = maxValue;
        return static_cast<uint64_t>(v);
    };

    core::AgentStorageAuditPolicy policy = naanAuditLog_.policy();
    policy.maxSegments = static_cast<uint32_t>(readBoundedU64(
        "naan.storage.audit.max_segments", 8, 1, 256));
    policy.maxSegmentBytes = readBoundedU64(
        "naan.storage.audit.max_segment_bytes", 1048576, 1024, 1073741824);
    naanAuditLog_.setPolicy(policy);

    naanStorageRootPath_ = config_.dataDir + "/naan/storage";
    std::string reason;
    if (!naanAuditLog_.open(naanStorageRootPath_, &reason)) {
        utils::Logger::error("Failed to initialize NAAN storage audit log: " + reason);
        return false;
    }

    auto st = naanAuditLog_.stats();
    naanStorageRecoveredLines_.store(st.recoveredTruncatedLines);
    naanStorageDroppedSegments_.store(st.droppedSegments);
    if (reason != "ok") {
        utils::Logger::warn("NAAN storage audit log recovered with reason: " + reason);
    }
    return true;
}

void appendNaanAuditEvent(uint64_t atTimestamp,
                          const std::string& kind,
                          const std::string& objectId,
                          const json& payload) {
    bool redacted = false;
    json safePayload = redactPotentialSecretsInJson(payload, &redacted);
    if (redacted) {
        naanRedactionCount_.fetch_add(1);
    }

    std::string reason;
    if (!naanAuditLog_.append(atTimestamp, kind, objectId, safeDump(safePayload), &reason)) {
        utils::Logger::warn("Failed to append NAAN audit event (" + kind + "): " + reason);
        return;
    }

    auto st = naanAuditLog_.stats();
    naanStorageRecoveredLines_.store(st.recoveredTruncatedLines);
    naanStorageDroppedSegments_.store(st.droppedSegments);

    std::string safeObject = redactPotentialSecrets(objectId);
    if (safeObject != objectId) {
        naanRedactionCount_.fetch_add(1);
    }

    std::string payloadSummary = safeDump(safePayload);
    if (payloadSummary.size() > 220) {
        payloadSummary = payloadSummary.substr(0, 220) + "...";
    }
    std::string message = kind;
    if (!safeObject.empty()) {
        message += " object=" + safeObject;
    }
    if (!payloadSummary.empty()) {
        message += " payload=" + payloadSummary;
    }
    recordNaanUiEvent(atTimestamp, classifyNaanEventCategory(kind), message);
}

bool persistNaanCrashState(const std::string& context) {
    if (naanRuntimeCrashStatePath_.empty()) return true;
    std::string reason;
    if (!naanRuntimeSupervisor_.saveCrashState(naanRuntimeCrashStatePath_, &reason)) {
        utils::Logger::warn("Failed to persist NAAN runtime crash state (" + context + "): " + reason);
        return false;
    }
    return true;
}

bool loadNaanScoreState(std::string* reason = nullptr) {
    if (naanScoreStatePath_.empty()) {
        if (reason) *reason = "path_empty";
        return true;
    }

    std::string scoreReason;
    if (!agentScore_.loadState(naanScoreStatePath_, &scoreReason)) {
        if (reason) *reason = "score_" + scoreReason;
        return false;
    }

    uint64_t loadedDecay = 0;
    uint64_t loadedViolationTick = 0;
    std::ifstream in(naanScoreDecayStatePath_);
    if (in.good()) {
        std::string line;
        if (!std::getline(in, line)) {
            if (reason) *reason = "decay_empty";
            return false;
        }
        std::stringstream ss(line);
        std::string tag;
        std::string decayToken;
        std::string violationToken;
        if (!std::getline(ss, tag, ',') || !std::getline(ss, decayToken, ',') || !std::getline(ss, violationToken, ',')) {
            if (reason) *reason = "decay_invalid_format";
            return false;
        }
        if (tag != "v1") {
            if (reason) *reason = "decay_invalid_version";
            return false;
        }
        try {
            loadedDecay = std::stoull(decayToken);
            loadedViolationTick = std::stoull(violationToken);
        } catch (...) {
            if (reason) *reason = "decay_invalid_numbers";
            return false;
        }
    }

    const auto snap = agentScore_.snapshot();
    naanScoreLastDecayTs_.store(loadedDecay);
    naanScoreLastViolationTick_.store(loadedViolationTick);
    naanLastScoreBand_.store(static_cast<uint8_t>(snap.band));
    if (reason) *reason = scoreReason;
    return true;
}

bool persistNaanScoreState(const std::string& context) {
    if (naanScoreStatePath_.empty()) return true;

    std::string scoreReason;
    if (!agentScore_.saveState(naanScoreStatePath_, &scoreReason)) {
        utils::Logger::warn("Failed to persist NAAN score state (" + context + "): " + scoreReason);
        return false;
    }

    if (!naanScoreDecayStatePath_.empty()) {
        std::error_code ec;
        std::filesystem::path p(naanScoreDecayStatePath_);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path(), ec);
            if (ec) {
                utils::Logger::warn("Failed to persist NAAN score decay state (" + context + "): mkdir_failed");
                return false;
            }
        }

        std::ofstream out(naanScoreDecayStatePath_, std::ios::trunc);
        if (!out.good()) {
            utils::Logger::warn("Failed to persist NAAN score decay state (" + context + "): open_failed");
            return false;
        }
        out << "v1,"
            << naanScoreLastDecayTs_.load() << ","
            << naanScoreLastViolationTick_.load();
        if (!out.good()) {
            utils::Logger::warn("Failed to persist NAAN score decay state (" + context + "): write_failed");
            return false;
        }
    }
    return true;
}

void noteNaanScoreBandTransition(core::AgentScoreBand before,
                                 core::AgentScoreBand after,
                                 uint64_t nowTimestamp) {
    if (before == after) return;
    naanScoreBandTransitions_.fetch_add(1);
    if (before == core::AgentScoreBand::LOCAL_DRAFT_ONLY && after != core::AgentScoreBand::LOCAL_DRAFT_ONLY) {
        naanQuarantineRecoveryTransitions_.fetch_add(1);
        naanQuarantineRecoveryLastAt_.store(nowTimestamp);
    }
    naanLastScoreBand_.store(static_cast<uint8_t>(after));

    json payload;
    payload["before"] = core::agentScoreBandToString(before);
    payload["after"] = core::agentScoreBandToString(after);
    payload["transitions"] = naanScoreBandTransitions_.load();
    payload["quarantineRecoveries"] = naanQuarantineRecoveryTransitions_.load();
    appendNaanAuditEvent(nowTimestamp, "score_band_transition", "agent_score", payload);
}

void applyNaanScoreStep(uint32_t accepted,
                        uint32_t rejected,
                        uint32_t violations,
                        uint64_t nowTimestamp,
                        const std::string& context) {
    const auto before = agentScore_.snapshot();
    agentScore_.applyStep(accepted, rejected, violations);
    const auto after = agentScore_.snapshot();

    if (rejected > 0 || violations > 0) {
        naanScoreLastViolationTick_.store(naanTickCount_.load());
    }
    noteNaanScoreBandTransition(before.band, after.band, nowTimestamp);

    json payload;
    payload["context"] = context;
    payload["accepted"] = accepted;
    payload["rejected"] = rejected;
    payload["violations"] = violations;
    payload["beforeScore"] = before.score;
    payload["afterScore"] = after.score;
    payload["beforeBand"] = core::agentScoreBandToString(before.band);
    payload["afterBand"] = core::agentScoreBandToString(after.band);
    appendNaanAuditEvent(nowTimestamp, "score_step", "agent_score", payload);

    (void)persistNaanScoreState(context);
}

void applyNaanScoreDecayTick(uint64_t nowTimestamp) {
    const uint32_t interval = naanScoreDecayIntervalSeconds_.load();
    if (interval == 0) return;

    uint64_t lastDecay = naanScoreLastDecayTs_.load();
    if (lastDecay == 0) {
        naanScoreLastDecayTs_.store(nowTimestamp);
        (void)persistNaanScoreState("decay_seed");
        return;
    }
    if (nowTimestamp <= lastDecay) return;

    uint64_t steps = (nowTimestamp - lastDecay) / interval;
    if (steps == 0) return;
    if (steps > 100000) steps = 100000;

    const auto before = agentScore_.snapshot();
    for (uint64_t i = 0; i < steps; ++i) {
        agentScore_.applyStep(0, 0, 0);
    }
    const auto after = agentScore_.snapshot();

    naanScoreLastDecayTs_.store(lastDecay + steps * static_cast<uint64_t>(interval));
    noteNaanScoreBandTransition(before.band, after.band, nowTimestamp);
    (void)persistNaanScoreState("decay_tick");
}

core::AgentRuntimeFailoverState currentNaanFailoverState(uint64_t now) const {
    const auto score = agentScore_.snapshot();
    return naanRuntimeSupervisor_.failoverState(score.throttled, score.quarantined, now);
}

core::AgentAdaptiveSchedule currentNaanSchedule() const {
    const auto score = agentScore_.snapshot();
    return agentAdaptiveScheduler_.schedule(score.throttled, score.quarantined);
}

bool persistNaanSchedulerState(const std::string& context) {
    if (naanSchedulerStatePath_.empty()) return true;
    std::string reason;
    if (!naanTaskScheduler_.saveCheckpoint(naanSchedulerStatePath_, &reason)) {
        utils::Logger::warn("Failed to persist NAAN scheduler state (" + context + "): " + reason);
        return false;
    }
    return true;
}

#include "node/main_parts/naan_coordination.inc"

UpdateManifestAccept acceptUpdateManifest(const core::UpdateManifest& manifest, bool relay, std::string* reason = nullptr) {
    std::string verifyReason;
    if (!manifest.validateStrict(&verifyReason)) {
        if (reason) *reason = verifyReason;
        return UpdateManifestAccept::REJECTED;
    }

    std::string idHex = crypto::toHex(manifest.bundleId);
    bool inserted = false;
    {
        std::lock_guard<std::mutex> lock(invMtx_);
        auto it = updateManifestsById_.find(idHex);
        if (it != updateManifestsById_.end()) {
            knownUpdateBundles_.insert(idHex);
            if (reason) *reason = "duplicate";
            return UpdateManifestAccept::DUPLICATE;
        }
        updateManifestsById_[idHex] = manifest;
        knownUpdateBundles_.insert(idHex);
        inserted = true;
    }

    if (inserted && relay) {
        broadcastInv(synapse::InvType::UPDATE_BUNDLE, manifest.bundleId);
    }
    if (reason) *reason = "accepted";
    return UpdateManifestAccept::ACCEPTED;
}

uint64_t parseNgtAtomic(const std::string& value) const {
    if (value.empty()) {
        throw std::runtime_error("Empty amount");
    }
    std::string t = value;
    for (auto& c : t) {
        if (c == ',') c = '.';
    }
    size_t dot = t.find('.');
    std::string intPart = dot == std::string::npos ? t : t.substr(0, dot);
    std::string fracPart = dot == std::string::npos ? "" : t.substr(dot + 1);
    if (intPart.empty()) intPart = "0";
    if (fracPart.size() > 8) {
        throw std::runtime_error("Too many decimals");
    }
    unsigned __int128 iv = 0;
    for (char c : intPart) {
        if (c < '0' || c > '9') throw std::runtime_error("Invalid number");
        iv = iv * 10 + static_cast<unsigned>(c - '0');
    }
    unsigned __int128 fv = 0;
    for (char c : fracPart) {
        if (c < '0' || c > '9') throw std::runtime_error("Invalid number");
        fv = fv * 10 + static_cast<unsigned>(c - '0');
    }
    for (size_t i = fracPart.size(); i < 8; ++i) fv *= 10;
    unsigned __int128 total = iv * 100000000ULL + fv;
    if (total > std::numeric_limits<uint64_t>::max()) {
        throw std::runtime_error("Amount too large");
    }
    return static_cast<uint64_t>(total);
}

double atomsToNgt(uint64_t atoms) const {
    return static_cast<double>(atoms) / 100000000.0;
}

std::string addressFromPubKey(const crypto::PublicKey& pubKey) const {
    auto aliases = addressAliasesFromPubKey(pubKey);
    if (aliases.empty()) return {};
    return aliases.front();
}

std::vector<std::string> addressAliasesFromPubKey(const crypto::PublicKey& pubKey) const {
    std::vector<std::string> aliases;
    std::unordered_set<std::string> seen;
    auto pushAlias = [&](const std::string& alias) {
        if (!alias.empty() && seen.insert(alias).second) aliases.push_back(alias);
    };
    if (attachedAgentIdentity_.valid() && pubKey == attachedAgentIdentity_.id) {
        pushAlias(address_);
    }
    auto derivedAliases = crypto::walletAddressAliasesFromPublicKey(pubKey);
    pushAlias(derivedAliases[0]);
    pushAlias(derivedAliases[1]);
    return aliases;
}

uint64_t walletBalanceAtomsForPubKey(const crypto::PublicKey& pubKey) const {
    if (!transfer_) return 0;
    uint64_t total = 0;
    for (const auto& alias : addressAliasesFromPubKey(pubKey)) {
        uint64_t value = transfer_->getBalance(alias);
        if (UINT64_MAX - total < value) return UINT64_MAX;
        total += value;
    }
    return total;
}

uint64_t poeMinStakeAtoms() const {
    try {
        return parseNgtAtomic(config_.poeMinStake);
    } catch (...) {
        return 0;
    }
}

bool tryGetSelfValidatorPublicKey(crypto::PublicKey& out) const {
    if (!keys_ || !keys_->isValid()) return false;
    auto pubV = keys_->getPublicKey();
    if (pubV.size() < crypto::PUBLIC_KEY_SIZE) return false;
    std::memcpy(out.data(), pubV.data(), out.size());
    return true;
}

std::vector<std::string> currentWalletAddressAliases() const {
    std::vector<std::string> aliases;
    std::unordered_set<std::string> seen;
    auto pushAlias = [&](const std::string& alias) {
        if (!alias.empty() && seen.insert(alias).second) aliases.push_back(alias);
    };
    pushAlias(address_);
    crypto::PublicKey pubKey{};
    if (tryGetSelfValidatorPublicKey(pubKey)) {
        for (const auto& alias : addressAliasesFromPubKey(pubKey)) {
            pushAlias(alias);
        }
    }
    return aliases;
}

uint64_t currentWalletBalanceAtoms() const {
    if (!transfer_) return 0;
    uint64_t total = 0;
    for (const auto& alias : currentWalletAddressAliases()) {
        uint64_t value = transfer_->getBalance(alias);
        if (UINT64_MAX - total < value) return UINT64_MAX;
        total += value;
    }
    return total;
}

uint64_t currentWalletPendingAtoms() const {
    if (!transfer_) return 0;
    uint64_t total = 0;
    for (const auto& alias : currentWalletAddressAliases()) {
        uint64_t value = transfer_->getPendingBalance(alias);
        if (UINT64_MAX - total < value) return UINT64_MAX;
        total += value;
    }
    return total;
}

core::Transaction buildWalletPaymentTransaction(const std::string& to, uint64_t amountAtoms, uint64_t feeAtoms) const {
    if (!transfer_ || !keys_ || !keys_->isValid() || address_.empty()) {
        throw std::runtime_error("wallet/transfer not ready");
    }
    if (!crypto::isSupportedWalletAddress(to)) {
        throw std::runtime_error("invalid_address");
    }
    if (amountAtoms == 0) {
        throw std::runtime_error("amount_too_small");
    }
    if (UINT64_MAX - amountAtoms < feeAtoms) {
        throw std::runtime_error("amount_too_large");
    }

    uint64_t required = amountAtoms + feeAtoms;
    std::vector<core::UTXO> spendable;
    spendable.reserve(64);
    std::unordered_set<std::string> seenOutpoints;
    for (const auto& alias : currentWalletAddressAliases()) {
        for (const auto& utxo : transfer_->getUTXOs(alias)) {
            std::string outpoint = crypto::toHex(utxo.txHash) + ":" + std::to_string(utxo.outputIndex);
            if (seenOutpoints.insert(outpoint).second) {
                spendable.push_back(utxo);
            }
        }
    }
    std::sort(spendable.begin(), spendable.end(), [](const core::UTXO& lhs, const core::UTXO& rhs) {
        if (lhs.amount != rhs.amount) return lhs.amount > rhs.amount;
        if (lhs.txHash != rhs.txHash) return lhs.txHash < rhs.txHash;
        return lhs.outputIndex < rhs.outputIndex;
    });

    core::Transaction tx;
    tx.timestamp = std::time(nullptr);
    tx.fee = feeAtoms;
    tx.status = core::TxStatus::PENDING;

    uint64_t collected = 0;
    for (const auto& utxo : spendable) {
        if (collected >= required) break;
        core::TxInput inp;
        inp.prevTxHash = utxo.txHash;
        inp.outputIndex = utxo.outputIndex;
        tx.inputs.push_back(inp);
        if (UINT64_MAX - collected < utxo.amount) {
            throw std::runtime_error("amount_too_large");
        }
        collected += utxo.amount;
    }

    if (collected < required) {
        throw std::runtime_error("insufficient_balance");
    }

    tx.outputs.push_back(core::TxOutput{amountAtoms, to});
    if (collected > required) {
        tx.outputs.push_back(core::TxOutput{collected - required, address_});
    }
    tx.txid = tx.computeHash();
    return tx;
}

core::PoeSelfValidatorBootstrapPolicyConfig currentPoeSelfValidatorBootstrapPolicyConfig() const {
    auto& runtimeCfg = utils::Config::instance();
    core::PoeSelfValidatorBootstrapPolicyConfig policyCfg;
    policyCfg.autoDisableEnabled = runtimeCfg.getBool("poe.self_validator_bootstrap_auto_disable", true);

    int64_t strictConnectedPeers = runtimeCfg.getInt64(
        "poe.self_validator_bootstrap_strict_connected_peers",
        (config_.dev || config_.regtest) ? 2 : 4);
    if (strictConnectedPeers < 1) strictConnectedPeers = 1;
    if (strictConnectedPeers > 1024) strictConnectedPeers = 1024;
    policyCfg.strictConnectedPeers = static_cast<uint32_t>(strictConnectedPeers);

    int64_t strictKnownPeers = runtimeCfg.getInt64(
        "poe.self_validator_bootstrap_strict_known_peers",
        (config_.dev || config_.regtest) ? 4 : 8);
    if (strictKnownPeers < 1) strictKnownPeers = 1;
    if (strictKnownPeers > 1000000) strictKnownPeers = 1000000;
    policyCfg.strictKnownPeers = static_cast<uint32_t>(strictKnownPeers);

    int64_t strictValidatorCount = runtimeCfg.getInt64(
        "poe.self_validator_bootstrap_strict_validator_count",
        (config_.dev || config_.regtest) ? 2 : 3);
    if (strictValidatorCount < 1) strictValidatorCount = 1;
    if (strictValidatorCount > 64) strictValidatorCount = 64;
    policyCfg.strictValidatorCount = static_cast<uint32_t>(strictValidatorCount);

    int64_t activationChecks = runtimeCfg.getInt64(
        "poe.self_validator_bootstrap_activation_checks",
        5);
    if (activationChecks < 1) activationChecks = 1;
    if (activationChecks > 3600) activationChecks = 3600;
    policyCfg.activationChecks = static_cast<uint32_t>(activationChecks);

    int64_t temporaryOverrideUntil = runtimeCfg.getInt64(
        "poe.self_validator_bootstrap_force_allow_until",
        0);
    if (temporaryOverrideUntil < 0) temporaryOverrideUntil = 0;
    policyCfg.temporaryOverrideUntil = static_cast<uint64_t>(temporaryOverrideUntil);
    return policyCfg;
}

void persistPoeSelfValidatorBootstrapState(const core::PoeSelfValidatorBootstrapPolicyState& state) {
    auto& runtimeCfg = utils::Config::instance();
    runtimeCfg.set("poe.self_validator_bootstrap_locked_off", state.persistentStrictMode);
    runtimeCfg.set(
        "poe.self_validator_bootstrap_locked_at",
        static_cast<int64_t>(state.strictModeActivatedAt));
    runtimeCfg.set("poe.self_validator_bootstrap_lock_reason", state.strictModeReason);
    runtimeCfg.save(config_.dataDir + "/synapsenet.conf");
}

#include "node/main_parts/poe_handlers.inc"

std::string handleRpcPoeExport(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    std::string pathStr = params.value("path", "");
    if (pathStr.empty()) {
        throw std::runtime_error("path required");
    }
    return rpc::buildRpcPoeExportResponse(
        node::exportPoeDatabase(
            node::poeDbPath(config_.dataDir),
            std::filesystem::path(pathStr)));
}

std::string handleRpcPoeImport(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    std::string pathStr = params.value("path", "");
    if (pathStr.empty()) {
        throw std::runtime_error("path required");
    }
    if (!poeV1_) {
        throw std::runtime_error("PoE not ready");
    }

    auto destDb = node::poeDbPath(config_.dataDir);

    auto cfg = poeV1_->getConfig();
    auto validators = poeV1_->getStaticValidators();
    poeV1_->close();

    node::importPoeDatabase(std::filesystem::path(pathStr), destDb);

    if (!poeV1_->open(destDb.string())) {
        throw std::runtime_error("Failed to reopen PoE DB");
    }
    poeV1_->setConfig(cfg);
    if (!validators.empty()) {
        poeV1_->setStaticValidators(validators);
    }

    return rpc::buildRpcPoeImportResponse(destDb.string());
}

std::string handleRpcWalletAddress(const std::string& paramsJson) {
    (void)paramsJson;
    if (!keys_ || !keys_->isValid()) {
        throw std::runtime_error("Wallet not loaded");
    }
    auto pubV = keys_->getPublicKey();
    if (pubV.size() < crypto::PUBLIC_KEY_SIZE) {
        throw std::runtime_error("Invalid public key");
    }
    crypto::PublicKey pk{};
    std::memcpy(pk.data(), pubV.data(), pk.size());
    auto aliases = currentWalletAddressAliases();
    json result;
    result["address"] = address_;
    result["legacyAddress"] = crypto::legacyWalletAddressFromPublicKey(pk);
    result["aliases"] = aliases;
    result["pubkey"] = crypto::toHex(pk);
    return result.dump();
}

std::string handleRpcWalletBalance(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    if (!transfer_) {
        throw std::runtime_error("Transfer not ready");
    }
    std::string addr = params.value("address", "");
    const bool useCurrentWallet = addr.empty();
    if (useCurrentWallet) addr = address_;
    if (addr.empty()) {
        throw std::runtime_error("address required");
    }
    uint64_t bal = useCurrentWallet ? currentWalletBalanceAtoms() : transfer_->getBalance(addr);
    json result;
    result["address"] = addr;
    if (useCurrentWallet) {
        result["aliases"] = currentWalletAddressAliases();
    }
    result["balanceAtoms"] = bal;
    result["balance"] = atomsToNgt(bal);
    result["totalSupplyAtoms"] = transfer_->totalSupply();
    result["totalSupply"] = atomsToNgt(transfer_->totalSupply());
    return result.dump();
}

static std::string modelStateToString(model::ModelState s) {
    switch (s) {
        case model::ModelState::UNLOADED: return "UNLOADED";
        case model::ModelState::LOADING: return "LOADING";
        case model::ModelState::READY: return "READY";
        case model::ModelState::GENERATING: return "GENERATING";
        case model::ModelState::ERROR: return "ERROR";
        case model::ModelState::DOWNLOADING: return "DOWNLOADING";
    }
    return "UNKNOWN";
}

std::string handleRpcModelStatus(const std::string& paramsJson) {
    (void)paramsJson;
    if (!modelLoader_) {
        throw std::runtime_error("Model not ready");
    }
    std::lock_guard<std::mutex> lock(modelMtx_);
    auto info = modelLoader_->getInfo();
    return rpc::buildRpcModelStatusResponse(
        modelLoader_->isLoaded(),
        modelStateToString(modelLoader_->getState()),
        modelLoader_->isGenerating(),
        info,
        modelLoader_->getError(),
        modelRequests_.load());
}

std::string handleRpcModelList(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    if (!modelLoader_) {
        throw std::runtime_error("Model not ready");
    }
    std::string dir = params.value("dir", "");
    if (dir.empty()) dir = config_.dataDir + "/models";
    std::lock_guard<std::mutex> lock(modelMtx_);
    return rpc::buildRpcModelListResponse(modelLoader_->listModels(dir));
}

std::string handleRpcModelLoad(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    if (!modelLoader_) {
        throw std::runtime_error("Model not ready");
    }
    std::string path = params.value("path", "");
    std::string name = params.value("name", "");
    if (path.empty() && !name.empty()) {
        path = (std::filesystem::path(config_.dataDir) / "models" / name).string();
    }
    if (path.empty()) {
        throw std::runtime_error("path required");
    }
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("model_not_found");
    }

    model::LoaderConfig cfg = modelLoader_->getConfig();
    if (params.contains("contextSize")) cfg.contextSize = static_cast<uint32_t>(std::max(256, params.value("contextSize", 2048)));
    if (params.contains("threads")) cfg.threads = static_cast<uint32_t>(std::max(1, params.value("threads", 4)));
    if (params.contains("gpuLayers")) cfg.gpuLayers = static_cast<uint32_t>(std::max(0, params.value("gpuLayers", 0)));
    if (params.contains("useGpu")) cfg.useGpu = params.value("useGpu", false);
    if (params.contains("useMmap")) cfg.useMmap = params.value("useMmap", true);
    if (params.contains("useMlock")) cfg.useMlock = params.value("useMlock", false);

    std::lock_guard<std::mutex> lock(modelMtx_);
    bool ok = modelLoader_->load(path, cfg);

    auto info = modelLoader_->getInfo();

    if (ok) {
        utils::Config::instance().set("model.last_path", path);
        utils::Config::instance().save(config_.dataDir + "/synapsenet.conf");
    }

    return rpc::buildRpcModelLoadResponse(
        ok,
        modelStateToString(modelLoader_->getState()),
        modelLoader_->getError(),
        info);
}

std::string handleRpcModelUnload(const std::string& paramsJson) {
    (void)paramsJson;
    if (!modelLoader_) {
        throw std::runtime_error("Model not ready");
    }
    std::lock_guard<std::mutex> lock(modelMtx_);
    bool ok = modelLoader_->unload();
    return rpc::buildRpcModelUnloadResponse(
        ok,
        modelStateToString(modelLoader_->getState()));
}

std::string handleRpcModelRemoteList(const std::string& paramsJson) {
    (void)paramsJson;
    const uint64_t now = std::time(nullptr);
    std::vector<rpc::RpcModelRemoteOfferView> offers;
    std::lock_guard<std::mutex> lock(remoteMtx_);
    for (const auto& [offerId, cache] : remoteOffers_) {
        const auto& o = cache.offer;
        if (o.expiresAt != 0 && o.expiresAt < now) continue;
        offers.push_back({
            offerId,
            cache.peerId,
            cache.receivedAt,
            o.modelId,
            o.providerAddress,
            o.pricePerRequestAtoms,
            o.maxSlots,
            o.usedSlots,
            o.expiresAt
        });
    }
    return rpc::buildRpcModelRemoteListResponse(offers);
}

std::string handleRpcModelRemoteRent(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    if (!network_) {
        throw std::runtime_error("network_not_ready");
    }
    if (!keys_ || !keys_->isValid()) {
        throw std::runtime_error("wallet_not_ready");
    }
    const auto route = refreshTorRoutePolicy(true);
    if (!route.allowP2PDiscovery) {
        throw std::runtime_error("tor_fail_closed_remote_model");
    }

    const std::string offerId = params.value("offerId", "");
    if (offerId.empty()) {
        throw std::runtime_error("offerId required");
    }

    RemoteOfferCache offer;
    {
        std::lock_guard<std::mutex> lock(remoteMtx_);
        auto it = remoteOffers_.find(offerId);
        if (it == remoteOffers_.end()) {
            throw std::runtime_error("offer_not_found");
        }
        offer = it->second;
    }

    synapse::RemoteModelRentMessage rent;
    rent.offerId = offerId;
    rent.timestamp = std::time(nullptr);
    auto pubV = keys_->getPublicKey();
    if (pubV.size() < rent.renterPubKey.size()) {
        throw std::runtime_error("invalid_pubkey");
    }
    std::memcpy(rent.renterPubKey.data(), pubV.data(), rent.renterPubKey.size());

    {
        std::lock_guard<std::mutex> lock(remoteMtx_);
        remoteRentOkByOffer_.erase(offerId);
    }

    network_->send(offer.peerId, makeMessage("m_rent", rent.serialize()));

    const uint64_t deadline = std::time(nullptr) + (config_.dev ? 10 : 30);
    synapse::RemoteModelRentOkMessage ok;
    for (;;) {
        std::unique_lock<std::mutex> lk(remoteMtx_);
        auto it = remoteRentOkByOffer_.find(offerId);
        if (it != remoteRentOkByOffer_.end()) {
            ok = it->second;
            break;
        }
        lk.unlock();
        if (std::time(nullptr) >= deadline) {
            throw std::runtime_error("rent_timeout");
        }
        std::unique_lock<std::mutex> waitLk(remoteMtx_);
        remoteCv_.wait_for(waitLk, std::chrono::milliseconds(250));
    }

    return rpc::buildRpcModelRemoteRentResponse(
        offerId,
        offer.peerId,
        ok.sessionId,
        ok.providerAddress,
        ok.pricePerRequestAtoms,
        ok.expiresAt);
}

std::string handleRpcModelRemoteEnd(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    const std::string sessionId = params.value("sessionId", "");
    if (sessionId.empty()) throw std::runtime_error("sessionId required");
    std::lock_guard<std::mutex> lock(remoteMtx_);
    remoteSessions_.erase(sessionId);
    return rpc::buildRpcModelOkResponse();
}

std::string handleRpcModelAccessGet(const std::string& paramsJson) {
    (void)paramsJson;
    if (!modelAccess_) throw std::runtime_error("model access not ready");
    return rpc::buildRpcModelAccessGetResponse(
        modelAccess_->getMode(),
        modelAccess_->getMaxSlots(),
        modelAccess_->getActiveSlots(),
        modelAccess_->getAvailableSlots(),
        modelAccess_->getPrice(),
        remotePricePerRequestAtoms_);
}

std::string handleRpcModelAccessSet(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    if (!modelAccess_) throw std::runtime_error("model access not ready");

    bool changed = false;
    if (params.contains("mode") && params["mode"].is_string()) {
        modelAccess_->setMode(rpc::parseAccessMode(params["mode"].get<std::string>()));
        utils::Config::instance().set("model.access.mode", rpc::accessModeToString(modelAccess_->getMode()));
        changed = true;
    }
    if (params.contains("maxSlots")) {
        uint32_t slots = static_cast<uint32_t>(std::max(1, params.value("maxSlots", 3)));
        modelAccess_->setMaxSlots(slots);
        utils::Config::instance().set("model.access.max_slots", static_cast<int>(slots));
        changed = true;
    }
    if (params.contains("pricePerHourAtoms")) {
        uint64_t p = static_cast<uint64_t>(std::max<int64_t>(0, params.value("pricePerHourAtoms", 0)));
        modelAccess_->setPrice(p);
        utils::Config::instance().set("model.access.price_per_hour_atoms", static_cast<int64_t>(p));
        changed = true;
    }
    if (params.contains("remotePricePerRequestAtoms")) {
        uint64_t p = static_cast<uint64_t>(std::max<int64_t>(0, params.value("remotePricePerRequestAtoms", 0)));
        remotePricePerRequestAtoms_ = p;
        utils::Config::instance().set("model.remote.price_per_request_atoms", static_cast<int64_t>(p));
        changed = true;
    }

    if (changed) {
        utils::Config::instance().save(config_.dataDir + "/synapsenet.conf");
    }
    return handleRpcModelAccessGet("{}");
}

std::string handleRpcMarketListings(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    if (!modelMarketplace_) throw std::runtime_error("marketplace_not_ready");
    bool includeInactive = params.value("includeInactive", false);
    return rpc::buildRpcMarketListingsResponse(
        modelMarketplace_->getAllListings(includeInactive));
}

std::string handleRpcMarketStats(const std::string& paramsJson) {
    (void)paramsJson;
    if (!modelMarketplace_) throw std::runtime_error("marketplace_not_ready");
    return rpc::buildRpcMarketStatsResponse(modelMarketplace_->getStats());
}

std::string createAndSubmitPaymentTx(const std::string& to, uint64_t amountAtoms, uint64_t& feeAtomsOut) {
    if (!transfer_ || !keys_ || !keys_->isValid() || address_.empty()) {
        throw std::runtime_error("Wallet/transfer not ready");
    }
    if (amountAtoms == 0) {
        feeAtomsOut = 0;
        throw std::runtime_error("Amount too small");
    }
    if (currentWalletBalanceAtoms() < amountAtoms) {
        throw std::runtime_error("Insufficient balance");
    }

    uint64_t fee = transfer_->estimateFee(0);
    core::Transaction tx;
    for (int i = 0; i < 5; ++i) {
        tx = buildWalletPaymentTransaction(to, amountAtoms, fee);
        uint64_t requiredFee = transfer_->estimateFee(tx.serialize().size());
        if (requiredFee == fee) break;
        fee = requiredFee;
    }

    const uint64_t balanceAtoms = currentWalletBalanceAtoms();
    if (UINT64_MAX - amountAtoms < fee) {
        throw std::runtime_error("Amount too large");
    }
    const uint64_t requiredAtoms = amountAtoms + fee;
    if (balanceAtoms < requiredAtoms) {
        throw std::runtime_error("Insufficient balance (including fee)");
    }

    crypto::PrivateKey pk{};
    auto pkv = keys_->getPrivateKey();
    if (pkv.size() < pk.size()) {
        throw std::runtime_error("Invalid private key");
    }
    std::memcpy(pk.data(), pkv.data(), pk.size());
    if (!transfer_->signTransaction(tx, pk)) {
        throw std::runtime_error("Failed to sign transaction");
    }
    if (!transfer_->submitTransaction(tx)) {
        throw std::runtime_error("Failed to submit transaction");
    }
    feeAtomsOut = fee;
    return crypto::toHex(tx.txid);
}

std::string handleRpcAiCompleteRemote(const json& params, const std::string& prompt, const model::GenerationParams& gp) {
    if (!network_) throw std::runtime_error("network_not_ready");
    if (!keys_) throw std::runtime_error("wallet_not_ready");
    rpc::RpcAiRemoteCompletionInputs inputs;
    inputs.params = &params;
    inputs.prompt = &prompt;
    inputs.generationParams = &gp;
    inputs.devMode = config_.dev;
    inputs.network = network_.get();
    inputs.keys = keys_.get();
    inputs.remoteMtx = &remoteMtx_;
    inputs.remoteCv = &remoteCv_;
    inputs.remoteSessions = &remoteSessions_;
    inputs.remotePending = &remotePending_;
    inputs.refreshTorRoutePolicy =
        [this](bool allowStartManagedTor) { return refreshTorRoutePolicy(allowStartManagedTor); };
    inputs.createAndSubmitPaymentTx =
        [this](const std::string& to, uint64_t amountAtoms, uint64_t& feeAtomsOut) {
            return createAndSubmitPaymentTx(to, amountAtoms, feeAtomsOut);
        };
    inputs.nextRequestId = []() { return randomHex16(); };
    return rpc::runRpcAiCompleteRemote(inputs);
}

std::string handleRpcAiComplete(const std::string& paramsJson) {
    return rpc::runRpcAiComplete(paramsJson, makeRpcAiCompletionInputs());
}

std::string handleRpcAiStop(const std::string& paramsJson) {
    (void)paramsJson;
    if (!modelLoader_) {
        throw std::runtime_error("Model not ready");
    }
    modelLoader_->stopGeneration();
    json out;
    out["ok"] = true;
    return out.dump();
}

std::string handleRpcPoeValidators(const std::string& paramsJson) {
    (void)paramsJson;
    if (!poeV1_) {
        throw std::runtime_error("PoE not ready");
    }
    return rpc::buildRpcPoeValidatorsResponse(
        poeV1_->getDeterministicValidators());
}

std::string handleRpcUpdateManifestSubmit(const std::string& paramsJson) {
    return rpc::runRpcUpdateManifestSubmit(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcUpdateManifestFetch(const std::string& paramsJson) {
    return rpc::runRpcUpdateManifestFetch(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcUpdateManifestList(const std::string& paramsJson) {
    return rpc::runRpcUpdateManifestList(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcUpdateManifestApprove(const std::string& paramsJson) {
    return rpc::runRpcUpdateManifestApprove(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcUpdateManifestApprovals(const std::string& paramsJson) {
    return rpc::runRpcUpdateManifestApprovals(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcUpdateInstallState(const std::string& paramsJson) {
    (void)paramsJson;
    return rpc::runRpcUpdateInstallState(makeRpcUpdateRuntimeInputs());
}

std::string handleRpcUpdateInstallPrepare(const std::string& paramsJson) {
    return rpc::runRpcUpdateInstallPrepare(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcUpdateInstallAdvance(const std::string& paramsJson) {
    return rpc::runRpcUpdateInstallAdvance(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcUpdateInstallCommit(const std::string& paramsJson) {
    return rpc::runRpcUpdateInstallCommit(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcUpdateInstallRollback(const std::string& paramsJson) {
    (void)paramsJson;
    return rpc::runRpcUpdateInstallRollback(makeRpcUpdateRuntimeInputs());
}

std::string handleRpcImplantUpdateState(const std::string& paramsJson) {
    (void)paramsJson;
    return rpc::runRpcImplantUpdateState(makeRpcUpdateRuntimeInputs());
}

std::string handleRpcImplantUpdatePrepare(const std::string& paramsJson) {
    return rpc::runRpcImplantUpdatePrepare(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcImplantUpdateAdvance(const std::string& paramsJson) {
    return rpc::runRpcImplantUpdateAdvance(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcImplantUpdateCommit(const std::string& paramsJson) {
    return rpc::runRpcImplantUpdateCommit(paramsJson, makeRpcUpdateRuntimeInputs());
}

std::string handleRpcImplantUpdateRollback(const std::string& paramsJson) {
    (void)paramsJson;
    return rpc::runRpcImplantUpdateRollback(makeRpcUpdateRuntimeInputs());
}

std::string handleRpcNaanStatus(const std::string& paramsJson) {
    (void)paramsJson;
    return rpc::runRpcNaanStatus(makeRpcNaanStatusRuntimeInputs());
}

std::string handleRpcNaanObservatoryArtifacts(const std::string& paramsJson) {
    return rpc::runRpcNaanObservatoryArtifacts(paramsJson, makeRpcNaanRuntimeInputs());
}

std::string handleRpcNaanObservatoryArtifactGet(const std::string& paramsJson) {
    return rpc::runRpcNaanObservatoryArtifactGet(paramsJson, makeRpcNaanRuntimeInputs());
}

std::string handleRpcNaanObservatoryDrafts(const std::string& paramsJson) {
    return rpc::runRpcNaanObservatoryDrafts(paramsJson, makeRpcNaanRuntimeInputs());
}

std::string handleRpcNaanObservatoryDraftGet(const std::string& paramsJson) {
    return rpc::runRpcNaanObservatoryDraftGet(paramsJson, makeRpcNaanRuntimeInputs());
}

std::string handleRpcNaanPipelineDryRun(const std::string& paramsJson) {
    return rpc::runRpcNaanPipelineDryRun(paramsJson, makeRpcNaanRuntimeInputs());
}

std::string handleRpcNaanPipelineDrain(const std::string& paramsJson) {
    return rpc::runRpcNaanPipelineDrain(paramsJson, makeRpcNaanRuntimeInputs());
}

std::string handleRpcNodeStatus(const std::string& paramsJson) {
    (void)paramsJson;
    const NodeStats st = getStats();
    const auto route = refreshTorRoutePolicy(true);

    rpc::RpcNodeStatusInputs inputs;
    inputs.running = running_.load();
    inputs.networkType = config_.networkType;
    inputs.p2pPort = network_ ? network_->getPort() : config_.port;
    inputs.rpcPort = config_.rpcPort;
    inputs.peersConnected = st.peersConnected;
    inputs.uptimeSeconds = st.uptime;
    inputs.uptime = formatUptime(st.uptime);
    inputs.syncProgress = st.syncProgress;
    if (ledger_) {
        inputs.ledgerAvailable = true;
        inputs.ledgerHeight = ledger_->height();
        inputs.ledgerEvents = ledger_->eventCount();
        inputs.tipHash = crypto::toHex(ledger_->tipHash());
    }
    inputs.knowledgeEntries = st.knowledgeEntries;
    inputs.walletAddress = address_;
    inputs.privacyMode = config_.privacyMode;
    inputs.quantumSecurity = config_.quantumSecurity;
    inputs.quantum = node::collectNodeQuantumInputs(makeNodeQuantumStatusRuntimeInputs());
    inputs.poeBootstrapJson = currentPoeSelfValidatorBootstrapStatus().dump();
    inputs.tor = node::collectTorStatusInputs(makeTorStatusRuntimeInputs(), route);
    inputs.networkHealth =
        node::collectNodeNetworkHealthInputs(makeNodeNetworkHealthRuntimeInputs());
    return rpc::buildRpcNodeStatusResponse(inputs);
}

std::string handleRpcNodePeers(const std::string& paramsJson) {
    (void)paramsJson;
    if (!network_) {
        return rpc::buildRpcNodePeersResponse({}, {});
    }
    return rpc::buildRpcNodePeersResponse(
        network_->getPeers(),
        [this](const network::Peer& peer) { return getPeerDisplayInfo(peer); });
}

std::string handleRpcNodeLogs(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    size_t limit = 100;
    if (params.contains("limit")) {
        limit = static_cast<size_t>(std::max(1, params.value("limit", 100)));
    }
    return rpc::buildRpcNodeLogsResponse(utils::Logger::getRecentLogs(limit));
}

std::string handleRpcNodeSeeds(const std::string& paramsJson) {
    (void)paramsJson;
    rpc::RpcNodeSeedsInputs inputs;
    inputs.onionAddress = activeOnionAddress();
    inputs.onionPort = network_ ? network_->getPort() : config_.port;
    inputs.onionActive = isOnionServiceActive();
    inputs.torSeedAddress = activeTorSeedAddress();
    if (discovery_) {
        inputs.bootstrapNodes = discovery_->getBootstrapNodes();
        inputs.dnsSeeds = discovery_->getDnsSeeds();
    }
    return rpc::buildRpcNodeSeedsResponse(inputs);
}

std::string handleRpcNodeDiscoveryStats(const std::string& paramsJson) {
    (void)paramsJson;
    if (!discovery_) {
        return rpc::buildRpcNodeDiscoveryStatsResponse(std::nullopt, false, 0, 0);
    }
    return rpc::buildRpcNodeDiscoveryStatsResponse(
        discovery_->getStats(),
        discovery_->isRunning(),
        discovery_->getDnsSeeds().size(),
        discovery_->getBootstrapNodes().size());
}

std::string handleRpcNodeTorControl(const std::string& paramsJson) {
    auto params = parseRpcParams(paramsJson);
    const std::string actionRaw = params.value("action", "");
    const auto torStatusRuntimeInputs = makeTorStatusRuntimeInputs();

    auto reloadNaanWebConfig = [&](bool persistSanitized) -> std::string {
        if (!ensureWebSubsystem()) {
            throw std::runtime_error("web_subsystem_init_failed");
        }

        std::lock_guard<std::mutex> lock(webMtx_);
        if (!webSearch_ || !webExtractor_) {
            throw std::runtime_error("web_subsystem_not_ready");
        }

        node::ReloadNaanWebConfigInputs reloadInputs;
        reloadInputs.dataDir = config_.dataDir;
        reloadInputs.persistSanitized = persistSanitized;
        reloadInputs.webSearch = webSearch_.get();
        reloadInputs.webExtractor = webExtractor_.get();
        reloadInputs.webConfigTotalLines = &naanWebConfigTotalLines_;
        reloadInputs.webConfigAppliedLines = &naanWebConfigAppliedLines_;
        reloadInputs.webConfigInvalidLines = &naanWebConfigInvalidLines_;
        reloadInputs.webConfigUnknownKeys = &naanWebConfigUnknownKeys_;
        reloadInputs.webConfigUnknownKeySamples = &naanWebConfigUnknownKeySamples_;
        reloadInputs.webConfigSanitizedWrites = &naanWebConfigSanitizedWrites_;
        reloadInputs.agentTorBridgeRemoteLastFetchAt = &agentTorBridgeRemoteLastFetchAt_;
        reloadInputs.agentTorBridgeRemoteFetchAttempts = &agentTorBridgeRemoteFetchAttempts_;
        reloadInputs.agentTorBridgeRemoteFetchSuccesses = &agentTorBridgeRemoteFetchSuccesses_;
        reloadInputs.agentTorBridgeRemoteRateLimitedSkips = &agentTorBridgeRemoteRateLimitedSkips_;
        reloadInputs.configuredTorRuntimeMode = [this]() { return configuredTorRuntimeMode(); };
        reloadInputs.configuredTorSocksHost = [this]() { return configuredTorSocksHost(); };
        reloadInputs.configuredTorSocksPort = [this]() { return configuredTorSocksPort(); };
        reloadInputs.configuredTorControlPort = [this]() { return configuredTorControlPort(); };
        reloadInputs.agentTorRequired = [this]() { return agentTorRequired_.load(); };
        reloadInputs.agentTorReachable = [this]() { return agentTorReachable_.load(); };
        reloadInputs.refreshTorRoutePolicy =
            [this](bool allowStartManagedTor) { return refreshTorRoutePolicy(allowStartManagedTor); };
        reloadInputs.setTorBridgeProviderMetaSnapshotJson = [this](const std::string& raw) {
            setTorBridgeProviderMetaSnapshot(json::parse(raw));
        };
        return node::reloadNaanWebConfigJson(reloadInputs);
    };

    node::TorControlActionCallbacks callbacks;
    callbacks.collectTorControlStatus =
        [torStatusRuntimeInputs](const core::TorRoutePolicyDecision& route) {
            return node::collectTorStatusInputs(torStatusRuntimeInputs, route);
        };
    callbacks.reloadNaanWebConfig =
        [&](bool persistSanitized) { return reloadNaanWebConfig(persistSanitized); };
    callbacks.refreshTorRoutePolicy =
        [this](bool allowStartManagedTor) { return refreshTorRoutePolicy(allowStartManagedTor); };
    callbacks.stopManagedTorRuntimeIfOwned =
        [this](bool preserveRestartIntent) { return stopManagedTorRuntimeIfOwned(preserveRestartIntent); };
    callbacks.resetManagedTorRestartBackoffState =
        [this]() { resetManagedTorRestartBackoffState(); };
    callbacks.startManagedTorRuntime = [this]() { return startManagedTorRuntime(); };
    callbacks.configuredTorRuntimeMode = [this]() { return configuredTorRuntimeMode(); };
    callbacks.persistLastKnownGoodBridgeSubset =
        [this](const std::string& reason) { return persistLastKnownGoodBridgeSubset(reason); };
    callbacks.saveConfig = [](const std::string& path) {
        return utils::Config::instance().save(path);
    };
    callbacks.configPath = config_.dataDir + "/synapsenet.conf";
    return node::handleTorControlAction(
        params,
        actionRaw,
        callbacks,
        agentTorManagedRestartInFlight_);
}

		public:
		    int runCommand(const std::vector<std::string>& args) {
	        (void)args;
	        return synapse::runCliLocally(config_, *this);
    }
    
    int run() {
        running_ = true;
        startTime_ = std::time(nullptr);
        
        utils::Logger::info("Node starting...");
        
        const char* kiro_env = std::getenv("KIRO_SESSION");
        bool in_kiro = (kiro_env != nullptr);
        bool interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
        
        const bool daemonRuntime = config_.daemon || in_kiro || !interactive;
        g_daemonMode = daemonRuntime;
        if (daemonRuntime) {
            if (in_kiro) {
                std::cout << "Detected Kiro environment - starting in daemon mode..." << std::endl;
            } else if (!interactive) {
                std::cout << "Non-interactive terminal - starting in daemon mode..." << std::endl;
            } else {
                std::cout << "Starting in daemon mode..." << std::endl;
            }
            return runDaemon();
        }
        
        if (!config_.tui) std::cout << "Starting with TUI..." << std::endl;
        return runWithTUI();
    }
    
    void shutdown() {
        const bool wasRunning = running_.exchange(false);
        const bool hasWork =
            wasRunning || network_ || discovery_ || rpc_ || webAi_ || webSearch_ ||
            webDetector_ || webExtractor_ || privacy_ || quantumManager_ || db_ ||
            !rpcCookieFilePath_.empty();
        if (!hasWork) return;

        const std::string shutdownReason = shutdownReasonFromSignalState();
        utils::Logger::info("Initiating shutdown sequence (reason=" + shutdownReason + ")");
        (void)persistNaanSchedulerState(shutdownReason);
        (void)persistNaanCrashState(shutdownReason);
        naanRuntimeInitialized_.store(false);
        
	        if (network_) network_->stop();
	        if (discovery_) discovery_->stop();
	        if (rpc_) rpc_->stop();
            cleanupRpcAuth();
	        {
	            std::lock_guard<std::mutex> lock(webMtx_);
	            if (webAi_) webAi_->shutdown();
	            if (webSearch_) webSearch_->shutdown();
	            webAi_.reset();
	            webSearch_.reset();
	            webDetector_.reset();
	            webExtractor_.reset();
	        }
	        if (privacy_) privacy_->shutdown();
            if (configuredTorRuntimeMode() != "external") {
                (void)stopManagedTorRuntimeIfOwned(false);
            }
	        if (quantumManager_) quantumManager_->shutdown();
	        if (db_) db_->close();
        
        node::stopNodeThreads(networkThread_, consensusThread_, maintenanceThread_, syncThread_);
        
        saveState();
        
        utils::Config::instance().save(config_.dataDir + "/synapsenet.conf");
        utils::Logger::info("Shutdown complete (reason=" + shutdownReason + ")");
        utils::Logger::shutdown();
    }
    
    void reload() {
        utils::Logger::info("Reloading configuration...");
        loadConfiguration();
    }
    
    NodeStats getStats() const {
        NodeStats stats;
        stats.uptime = std::time(nullptr) - startTime_;
        stats.peersConnected = network_ ? network_->peerCount() : 0;
	        stats.knowledgeEntries = poeV1_ ? poeV1_->totalEntries() : (knowledge_ ? knowledge_->totalEntries() : 0);
	        stats.syncProgress = syncProgress_;
	        stats.memoryUsage = getMemoryUsage();
	        stats.diskUsage = getDiskUsage();
	        stats.modelRequests = modelRequests_.load();
	        return stats;
	    }
    
    SystemInfo getSystemInfo() const {
        SystemInfo info;
        info.osName = "Unknown";
        info.cpuCores = std::thread::hardware_concurrency();
        return info;
    }
    
    bool isRunning() const { return running_; }
    const NodeConfig& getConfig() const { return config_; }
    
	private:
	    void saveState() {}

        bool initializeRpcAuth() {
            cleanupRpcAuth();
            if (!config_.rpcAuthRequired) return true;

            rpcCookieFilePath_ = resolveRpcCookiePath(config_.dataDir, config_.rpcCookieFile);
            std::filesystem::create_directories(std::filesystem::path(rpcCookieFilePath_).parent_path());
            rpcCookieToken_ = crypto::toHex(crypto::randomBytes(32));
            std::ofstream cookieFile(rpcCookieFilePath_, std::ios::trunc);
            if (!cookieFile.is_open()) {
                utils::Logger::error("Failed to create RPC cookie file at " + rpcCookieFilePath_);
                return false;
            }
            cookieFile << rpcCookieToken_ << "\n";
            cookieFile.close();
            if (!cookieFile.good()) {
                utils::Logger::error("Failed to write RPC cookie file at " + rpcCookieFilePath_);
                return false;
            }
#ifndef _WIN32
            ::chmod(rpcCookieFilePath_.c_str(), S_IRUSR | S_IWUSR);
#endif

            if (!config_.rpcUser.empty() || !config_.rpcPassword.empty()) {
                if (config_.rpcUser.empty() || config_.rpcPassword.empty()) {
                    utils::Logger::error("RPC basic authentication requires both network.rpc_user and network.rpc_password");
                    return false;
                }
                rpcBasicAuthToken_ = makeBasicAuthorizationValue(config_.rpcUser, config_.rpcPassword);
            } else {
                rpcBasicAuthToken_.clear();
            }

            return true;
        }

        void cleanupRpcAuth() {
            rpcCookieToken_.clear();
            rpcBasicAuthToken_.clear();
            if (!rpcCookieFilePath_.empty()) {
                std::error_code ec;
                std::filesystem::remove(rpcCookieFilePath_, ec);
                rpcCookieFilePath_.clear();
            }
        }

        bool rpcAuthMatches(const std::string& token) const {
            if (!rpcCookieToken_.empty() && token == rpcCookieToken_) return true;
            if (!rpcBasicAuthToken_.empty() && token == rpcBasicAuthToken_) return true;
            return false;
        }

	    void setLogLevel(const std::string& level) {
	        if (level == "debug") utils::Logger::setLevel(utils::LogLevel::DEBUG);
	        else if (level == "info") utils::Logger::setLevel(utils::LogLevel::INFO);
	        else if (level == "warn") utils::Logger::setLevel(utils::LogLevel::WARN);
        else if (level == "error") utils::Logger::setLevel(utils::LogLevel::ERROR);
        else utils::Logger::setLevel(utils::LogLevel::INFO);
    }
    
    bool loadConfiguration() {
        std::string configPath = config_.configPath.empty() ? 
            config_.dataDir + "/synapsenet.conf" : config_.configPath;
        
        if (!utils::Config::instance().load(configPath)) {
            utils::Logger::info("No config file found, using defaults");
            utils::Config::instance().loadDefaults();
        }
        
        auto& cfg = utils::Config::instance();
        auto readBoundedU32 = [&](const std::string& key, int fallback, uint32_t minValue, uint32_t maxValue) {
            int64_t v = cfg.getInt64(key, fallback);
            if (v < static_cast<int64_t>(minValue)) v = static_cast<int64_t>(minValue);
            if (v > static_cast<int64_t>(maxValue)) v = static_cast<int64_t>(maxValue);
            return static_cast<uint32_t>(v);
        };
        auto readBoundedU64 = [&](const std::string& key, int64_t fallback, uint64_t minValue, uint64_t maxValue) {
            int64_t v = cfg.getInt64(key, fallback);
            if (v < 0) v = 0;
            uint64_t out = static_cast<uint64_t>(v);
            if (out < minValue) out = minValue;
            if (out > maxValue) out = maxValue;
            return out;
        };

        if (!config_.securityLevelSetByCli) {
            const std::string configuredSecurityLevel = cfg.getString("security.level", config_.securityLevel);
            if (!configuredSecurityLevel.empty()) {
                config_.securityLevel = configuredSecurityLevel;
            }
        }
        if (!config_.quantumSecuritySetByCli) {
            config_.quantumSecurity = cfg.getBool("security.quantum_enabled", config_.quantumSecurity);
        }
        if (config_.poeValidators.empty()) {
            config_.poeValidators = cfg.getString("poe.validators", "");
        }
        if (config_.poeValidatorMode == "static") {
            const std::string configuredMode = cfg.getString("poe.validator_mode", config_.poeValidatorMode);
            if (!configuredMode.empty()) {
                config_.poeValidatorMode = configuredMode;
            }
        }
        if (config_.poeMinStake == "0") {
            const std::string configuredMinStake = cfg.getString("poe.validator_min_stake", config_.poeMinStake);
            if (!configuredMinStake.empty()) {
                config_.poeMinStake = configuredMinStake;
            }
        }
        if (config_.securityLevelSetByCli && config_.securityLevel == "standard" && !config_.quantumSecuritySetByCli) {
            config_.quantumSecurity = false;
        }

        if (config_.port == 8333) {
            config_.port = cfg.getInt("port", 8333);
        }
        if (config_.rpcPort == 8332) {
            config_.rpcPort = cfg.getInt("rpcport", 8332);
        }
        config_.rpcBindAddress = cfg.getString("network.rpc_bind_address", config_.rpcBindAddress);
        if (config_.rpcBindAddress.empty()) {
            config_.rpcBindAddress = "127.0.0.1";
        }
        config_.rpcAuthRequired = cfg.getBool("network.rpc_auth_required", true);
        config_.rpcUser = cfg.getString("network.rpc_user", cfg.getString("rpcuser", ""));
        config_.rpcPassword = cfg.getString("network.rpc_password", cfg.getString("rpcpassword", ""));
        config_.rpcCookieFile = cfg.getString("network.rpc_cookie_file", config_.rpcCookieFile);
        config_.maxPeers = readBoundedU32("maxpeers", cfg.getInt("network.max_peers", 125), 1, 100000);
        config_.maxInbound = readBoundedU32("maxinbound", cfg.getInt("network.max_inbound", 100), 0, 100000);
        config_.maxOutbound = readBoundedU32("maxoutbound", cfg.getInt("network.max_outbound", 25), 0, 100000);
        if (config_.maxInbound > config_.maxPeers) config_.maxInbound = config_.maxPeers;
        if (config_.maxOutbound > config_.maxPeers) config_.maxOutbound = config_.maxPeers;
        config_.networkAdaptiveAdmission = cfg.getBool("network.scale.adaptive_admission", true);
        config_.networkDeterministicEviction = cfg.getBool("network.scale.deterministic_eviction", true);
        config_.networkMaxPeersPerIp = readBoundedU32("network.scale.max_peers_per_ip", 8, 1, 100000);
        config_.networkMaxPeersPerSubnet = readBoundedU32("network.scale.max_peers_per_subnet", 32, 1, 100000);
        config_.networkSubnetPrefixBits = readBoundedU32("network.scale.subnet_prefix_bits", 24, 8, 32);
        config_.networkTokenBucketEnabled = cfg.getBool("network.scale.token_bucket_enabled", true);
        config_.networkTokenBucketBytesPerSecond = readBoundedU32(
            "network.scale.token_bucket_bytes_per_sec",
            static_cast<int>(network::MAX_MESSAGE_SIZE * 2),
            1024,
            1024 * 1024 * 128);
        config_.networkTokenBucketBytesBurst = readBoundedU32(
            "network.scale.token_bucket_bytes_burst",
            static_cast<int>(network::MAX_MESSAGE_SIZE * 4),
            1024,
            1024 * 1024 * 256);
        config_.networkTokenBucketMessagesPerSecond = readBoundedU32(
            "network.scale.token_bucket_msgs_per_sec",
            500,
            1,
            200000);
        config_.networkTokenBucketMessagesBurst = readBoundedU32(
            "network.scale.token_bucket_msgs_burst",
            1000,
            1,
            400000);
        config_.networkMalformedPenalty = readBoundedU32("network.scale.penalty_malformed", 20, 1, 10000);
        config_.networkRatePenalty = readBoundedU32("network.scale.penalty_rate", 10, 1, 10000);
        config_.networkPenaltyHalfLifeSeconds = readBoundedU32("network.scale.penalty_half_life_seconds", 900, 1, 86400 * 7);
        config_.networkBaseBanSeconds = readBoundedU32("network.scale.base_ban_seconds", 120, 1, 86400 * 30);
        config_.networkMaxBanSeconds = readBoundedU32("network.scale.max_ban_seconds", 3600, 1, 86400 * 30);
        if (config_.networkMaxBanSeconds < config_.networkBaseBanSeconds) {
            config_.networkMaxBanSeconds = config_.networkBaseBanSeconds;
        }
        config_.networkOverloadMode = cfg.getBool("network.scale.overload_mode", true);
        config_.networkOverloadEnterPeerPercent = readBoundedU32("network.scale.overload_enter_peer_percent", 90, 1, 100);
        config_.networkOverloadExitPeerPercent = readBoundedU32("network.scale.overload_exit_peer_percent", 70, 0, 100);
        if (config_.networkOverloadExitPeerPercent > config_.networkOverloadEnterPeerPercent) {
            config_.networkOverloadExitPeerPercent = config_.networkOverloadEnterPeerPercent;
        }
        config_.networkOverloadEnterBufferedRxBytes = readBoundedU64(
            "network.scale.overload_enter_buffer_bytes",
            static_cast<int64_t>(network::MAX_MESSAGE_SIZE * 32),
            network::MAX_MESSAGE_SIZE,
            network::MAX_MESSAGE_SIZE * 1024);
        config_.networkOverloadExitBufferedRxBytes = readBoundedU64(
            "network.scale.overload_exit_buffer_bytes",
            static_cast<int64_t>(network::MAX_MESSAGE_SIZE * 16),
            network::MAX_MESSAGE_SIZE,
            network::MAX_MESSAGE_SIZE * 1024);
        if (config_.networkOverloadExitBufferedRxBytes > config_.networkOverloadEnterBufferedRxBytes) {
            config_.networkOverloadExitBufferedRxBytes = config_.networkOverloadEnterBufferedRxBytes;
        }
        config_.networkInvMaxItems = readBoundedU32("network.scale.inv_max_items", 256, 1, 5000);
        config_.networkInvOverloadItems = readBoundedU32("network.scale.inv_overload_items", 32, 1, 1000);
        if (config_.networkInvOverloadItems > config_.networkInvMaxItems) {
            config_.networkInvOverloadItems = config_.networkInvMaxItems;
        }
        config_.networkGetDataMaxItems = readBoundedU32("network.scale.getdata_max_items", 128, 1, 5000);
        config_.networkGetDataOverloadItems = readBoundedU32("network.scale.getdata_overload_items", 32, 1, 1000);
        if (config_.networkGetDataOverloadItems > config_.networkGetDataMaxItems) {
            config_.networkGetDataOverloadItems = config_.networkGetDataMaxItems;
        }
        config_.networkGossipFanoutLimit = readBoundedU32("network.scale.gossip_fanout_limit", 64, 1, 100000);
        config_.networkGossipDedupWindowSeconds = readBoundedU32("network.scale.gossip_dedup_window_seconds", 5, 1, 3600);
        config_.networkVoteDedupWindowSeconds = readBoundedU32("network.scale.vote_dedup_window_seconds", 600, 1, 86400);
        config_.networkVoteDedupMaxEntries = readBoundedU32("network.scale.vote_dedup_max_entries", 20000, 64, 2000000);
        config_.dbCacheSize = cfg.getInt("dbcache", 450);
        config_.networkUseHardcodedBootstrap =
            cfg.getBool("network.discovery.use_hardcoded_bootstrap", true);

        remotePricePerRequestAtoms_ = static_cast<uint64_t>(
            std::max<int64_t>(0, cfg.getInt64("model.remote.price_per_request_atoms", 0))
        );
        {
            std::lock_guard<std::mutex> lock(remoteProvMtx_);
            if (localOfferId_.empty()) {
                localOfferId_ = cfg.getString("model.remote.offer_id", "");
                if (localOfferId_.empty()) localOfferId_ = randomHex16();
            }
        }

        reloadImplantUpdatePoliciesFromConfig();
        refreshSecurityPolicyHashes("config_load");

        const bool torRequired = cfg.getBool("agent.tor.required", true);
        const bool allowClearnetFallback = cfg.getBool("agent.routing.allow_clearnet_fallback", false);
        const bool allowP2PFallback = cfg.getBool("agent.routing.allow_p2p_clearnet_fallback", false);

        agentTorRequired_.store(torRequired);
        agentAllowClearnetFallback_.store(allowClearnetFallback);
        agentAllowP2PFallback_.store(allowP2PFallback);
        int64_t ownedManagedPid = 0;
        bool ownedManagedTor = false;
        if (readManagedTorPidFile(&ownedManagedPid)) {
#ifndef _WIN32
            ownedManagedTor = managedTorProcessMatchesOwnership(ownedManagedPid);
#else
            ownedManagedTor = false;
#endif
        }
        managedTorPid_.store(ownedManagedTor ? ownedManagedPid : 0);
        agentTorManaged_.store(ownedManagedTor);

        if (torRequired) {
            config_.privacyMode = true;
        }

        refreshTorRoutePolicy(true);
        
        return true;
    }
    
    bool initDatabase() {
        if (config_.amnesia) {
            utils::Logger::info("Amnesia mode: using in-memory database");
            return true;
        }
        
        std::string dbPath = config_.dataDir + "/chaindata";
        std::filesystem::create_directories(dbPath);
        
        db_ = std::make_unique<database::Database>();
        if (!db_->open(dbPath + "/chain.db")) {
            utils::Logger::error("Failed to open database at " + dbPath);
            return false;
        }
        
        utils::Logger::info("Database initialized: " + dbPath);
        return true;
    }
    
    bool initCrypto() {
        keys_ = std::make_unique<crypto::Keys>();
        
        std::string walletPath = config_.dataDir + "/wallet.dat";
        
        if (std::filesystem::exists(walletPath)) {
            if (!keys_->load(walletPath, "")) {
                utils::Logger::error("Failed to load wallet");
                return false;
            }
            utils::Logger::info("Wallet loaded successfully");
        } else if (config_.tui) {
            utils::Logger::info("Wallet not found, waiting for TUI creation");
            return true;
        } else {
            utils::Logger::info("Generating new wallet...");
            if (!keys_->generate()) {
                utils::Logger::error("Failed to generate keys");
                return false;
            }
            if (!keys_->save(walletPath, "")) {
                utils::Logger::error("Failed to save wallet");
                return false;
            }
            utils::Logger::info("New wallet created");
        }
        
        if (keys_->isValid()) {
            address_ = keys_->getAddress();
            utils::Logger::info("Wallet address: " + address_.substr(0, 16) + "...");
            updateSignerFromKeys();
        }
        return true;
    }
    
    bool initQuantumSecurity() {
        std::string requestedLevel = config_.securityLevel;
        std::transform(requestedLevel.begin(), requestedLevel.end(), requestedLevel.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::replace(requestedLevel.begin(), requestedLevel.end(), '_', '-');

        if (requestedLevel == "quantumready") {
            requestedLevel = "quantum-ready";
        }
        if (requestedLevel != "standard" && requestedLevel != "high" &&
            requestedLevel != "paranoid" && requestedLevel != "quantum-ready") {
            utils::Logger::warn("Unknown security level '" + config_.securityLevel + "', falling back to standard");
            requestedLevel = "standard";
        }
        config_.securityLevel = requestedLevel;
        if (config_.securityLevel != "standard") {
            config_.quantumSecurity = true;
        }
        if (config_.quantumSecurity && config_.securityLevel == "standard") {
            config_.securityLevel = "high";
        }

        if (!config_.quantumSecurity) {
            utils::Logger::info("Quantum security: disabled");
            return true;
        }
        
        quantumManager_ = std::make_unique<quantum::QuantumManager>();
        
        quantum::SecurityLevel level = quantum::SecurityLevel::STANDARD;
        if (config_.securityLevel == "high") {
            level = quantum::SecurityLevel::HIGH;
        } else if (config_.securityLevel == "paranoid") {
            level = quantum::SecurityLevel::PARANOID;
        } else if (config_.securityLevel == "quantum-ready") {
            level = quantum::SecurityLevel::QUANTUM_READY;
        }
        
        if (!quantumManager_->init(level)) {
            utils::Logger::error("Failed to initialize quantum security");
            return false;
        }
        
        utils::Logger::info(
            "Quantum security initialized: level=" + config_.securityLevel +
            " capability=" + node::quantumCapabilityMode() +
            " kyber=" + node::quantumKyberImplementationMode() +
            " dilithium=" + node::quantumDilithiumImplementationMode() +
            " sphincs=" + node::quantumSphincsImplementationMode());
        return true;
    }
    
    bool initNetwork() {
        network_ = std::make_unique<network::Network>();
        discovery_ = std::make_unique<network::Discovery>();
        
        network::NetworkConfig netCfg;
        netCfg.maxPeers = config_.maxPeers;
        netCfg.maxInbound = config_.maxInbound;
        netCfg.maxOutbound = config_.maxOutbound;
        netCfg.adaptiveAdmission = config_.networkAdaptiveAdmission;
        netCfg.deterministicEviction = config_.networkDeterministicEviction;
        netCfg.maxPeersPerIp = config_.networkMaxPeersPerIp;
        netCfg.maxPeersPerSubnet = config_.networkMaxPeersPerSubnet;
        netCfg.subnetPrefixBits = static_cast<uint8_t>(config_.networkSubnetPrefixBits);
        netCfg.enableTokenBucketDosGuard = config_.networkTokenBucketEnabled;
        netCfg.tokenBucketBytesPerSecond = config_.networkTokenBucketBytesPerSecond;
        netCfg.tokenBucketBytesBurst = config_.networkTokenBucketBytesBurst;
        netCfg.tokenBucketMessagesPerSecond = config_.networkTokenBucketMessagesPerSecond;
        netCfg.tokenBucketMessagesBurst = config_.networkTokenBucketMessagesBurst;
        netCfg.malformedPenaltyScore = config_.networkMalformedPenalty;
        netCfg.ratePenaltyScore = config_.networkRatePenalty;
        netCfg.penaltyHalfLifeSeconds = config_.networkPenaltyHalfLifeSeconds;
        netCfg.baseBanSeconds = config_.networkBaseBanSeconds;
        netCfg.maxBanSeconds = config_.networkMaxBanSeconds;
        netCfg.enableOverloadMode = config_.networkOverloadMode;
        netCfg.overloadEnterPeerPercent = config_.networkOverloadEnterPeerPercent;
        netCfg.overloadExitPeerPercent = config_.networkOverloadExitPeerPercent;
        netCfg.overloadEnterBufferedRxBytes = config_.networkOverloadEnterBufferedRxBytes;
        netCfg.overloadExitBufferedRxBytes = config_.networkOverloadExitBufferedRxBytes;

        const bool torRequired = agentTorRequired_.load();
        bool torReachable = probeTorSocks();
        if (torRequired && !torReachable) {
            torReachable = maybeStartManagedTorRuntimeWithBackoff(true, "init_network");
            if (torReachable) {
                agentTorManaged_.store(true);
            }
        }
        if (torReachable) {
            resetManagedTorRestartBackoffState();
        }
        agentTorReachable_.store(torReachable);
        refreshTorWebReadiness(torReachable, false);
        core::TorRoutePolicyInput routeIn;
        routeIn.torRequired = torRequired;
        routeIn.torReachable = torReachable;
        routeIn.allowClearnetFallback = agentAllowClearnetFallback_.load();
        routeIn.allowP2PFallback = agentAllowP2PFallback_.load();
        const auto route = core::evaluateTorRoutePolicy(routeIn);
        agentTorDegraded_.store(route.torDegraded);
        updateAndLogTorReadinessState(torRequired, torReachable, agentTorWebReady_.load(), route.torDegraded);

        if (torRequired) {
            netCfg.useSocksProxy = torReachable;
            netCfg.socksProxyHost = configuredTorSocksHost();
            netCfg.socksProxyPort = configuredTorSocksPort();
            if (!route.allowP2PDiscovery) {
                netCfg.maxOutbound = 0;
            }
        }
        network_->setConfig(netCfg);
        
        network::DiscoveryConfig discCfg;
        discCfg.maxPeers = config_.maxPeers;
        discCfg.minPeers = std::min<uint32_t>(8, config_.maxOutbound);
        discCfg.bootstrapQuarantineSeconds = static_cast<uint32_t>(std::max<int64_t>(
            30, utils::Config::instance().getInt64("network.discovery.bootstrap_quarantine_seconds", 600)));
        discovery_->setConfig(discCfg);
        
        if (config_.networkUseHardcodedBootstrap) {
            if (config_.testnet) {
                discovery_->addBootstrap("testnet-seed1.synapsenet.io", 18333);
                discovery_->addBootstrap("testnet-seed2.synapsenet.io", 18333);
                discovery_->addDnsSeed("testnet-seed1.synapsenet.io");
                discovery_->addDnsSeed("testnet-seed2.synapsenet.io");
            } else if (config_.regtest) {
                utils::Logger::info("Regtest mode: no bootstrap nodes");
            } else {
                discovery_->addBootstrap("seed1.synapsenet.io", 8333);
                discovery_->addBootstrap("seed2.synapsenet.io", 8333);
                discovery_->addBootstrap("seed3.synapsenet.io", 8333);
                discovery_->addBootstrap("seed4.synapsenet.io", 8333);
                discovery_->addBootstrap("seed5.synapsenet.io", 8333);
                discovery_->addDnsSeed("seed1.synapsenet.io");
                discovery_->addDnsSeed("seed2.synapsenet.io");
                discovery_->addDnsSeed("seed3.synapsenet.io");
                discovery_->addDnsSeed("seed4.synapsenet.io");
                discovery_->addDnsSeed("seed5.synapsenet.io");
            }
        } else if (config_.regtest) {
            utils::Logger::info("Regtest mode: no bootstrap nodes");
        }
        
        for (const auto& node : config_.seedNodes) {
            size_t colonPos = node.find(':');
            if (colonPos != std::string::npos) {
                std::string host = node.substr(0, colonPos);
                uint16_t port = std::stoi(node.substr(colonPos + 1));
                discovery_->addBootstrap(host, port);
                discovery_->addDnsSeed(host);
            }
        }

        {
            auto bootstrapNodes = discovery_->getBootstrapNodes();
            auto dnsSeeds = discovery_->getDnsSeeds();
            if (!config_.regtest && bootstrapNodes.size() < discCfg.minimumBootstrapNodes) {
                utils::Logger::warn("Bootstrap node count (" + std::to_string(bootstrapNodes.size()) +
                    ") is below the minimum (" + std::to_string(discCfg.minimumBootstrapNodes) +
                    "); network resilience is degraded");
            }
            if (!config_.regtest && dnsSeeds.size() < 2) {
                utils::Logger::warn("Fewer than 2 DNS seeds configured; bootstrap may not survive a single seed failure");
            }
        }
        
        network_->onMessage([this](const std::string& peerId, const network::Message& msg) {
            handleMessage(peerId, msg);
        });
        
        network_->onPeerConnected([this](const network::Peer& peer) {
            handlePeerConnected(peer);
        });
        
        network_->onPeerDisconnected([this](const network::Peer& peer) {
            handlePeerDisconnected(peer);
        });
        
        discovery_->setSendMessageCallback([this](const std::string& peerId, const std::string& command, const std::vector<uint8_t>& payload) -> bool {
            if (!network_) return false;
            auto msg = makeMessage(command, payload);
            return network_->send(peerId, msg);
        });
        
        discovery_->setGetConnectedPeersCallback([this]() -> std::vector<std::string> {
            if (!network_) return {};
            std::vector<std::string> peerIds;
            for (const auto& peer : network_->getPeers()) {
                if (peer.state == network::PeerState::CONNECTED) {
                    peerIds.push_back(peer.id);
                }
            }
            return peerIds;
        });
        
        std::string externalIP = config_.bindAddress;
        if (externalIP == "0.0.0.0" || externalIP.empty()) {
            externalIP = "";
        }
        if (!externalIP.empty()) {
            discovery_->setExternalAddress(externalIP);
        }
        
        uint16_t port = config_.testnet ? 18333 : config_.port;
        if (!network_->start(port)) {
            utils::Logger::info("Network offline mode - port " + std::to_string(port) + " unavailable");
            offlineMode_ = true;
        } else {
            offlineMode_ = false;
            uint16_t bound = network_->getPort();
            if (bound != 0) config_.port = bound;
            utils::Logger::info("Network started on port " + std::to_string(bound));
            const auto route = refreshTorRoutePolicy(true);
            if (route.allowP2PDiscovery) {
                discovery_->start(bound);
                discovery_->refreshFromDNS();
            } else {
                utils::Logger::warn("NAAN Tor-required mode active: outbound P2P discovery is suspended (degraded)");
            }
        }
        
        return true;
    }
    
    bool initCore() {
        if (!config_.tui) std::cout << "Creating core components..." << std::endl;
        ledger_ = std::make_unique<core::Ledger>();
        knowledge_ = std::make_unique<core::KnowledgeNetwork>();
        transfer_ = std::make_unique<core::TransferManager>();
        consensus_ = std::make_unique<core::Consensus>();
        poeV1_ = std::make_unique<core::PoeV1Engine>();
        
        if (!config_.tui) std::cout << "Creating directories..." << std::endl;
        std::string ledgerPath = config_.dataDir + "/ledger";
        std::string knowledgePath = config_.dataDir + "/knowledge";
        std::string transferPath = config_.dataDir + "/transfer";
        std::string consensusPath = config_.dataDir + "/consensus";
        std::string poePath = config_.dataDir + "/poe";
        std::string updatesPath = config_.dataDir + "/updates";
        std::filesystem::create_directories(ledgerPath);
        std::filesystem::create_directories(knowledgePath);
        std::filesystem::create_directories(transferPath);
        std::filesystem::create_directories(consensusPath);
        std::filesystem::create_directories(poePath);
        std::filesystem::create_directories(updatesPath);

        if (config_.resetNgt) {
            std::error_code ec;
            std::filesystem::remove(transferPath + "/transfer.db", ec);
            std::filesystem::remove(transferPath + "/transfer.db-wal", ec);
            std::filesystem::remove(transferPath + "/transfer.db-shm", ec);
            utils::Logger::info("NGT balances reset (transfer DB cleared)");
        }
        
        if (!config_.tui) std::cout << "Opening ledger..." << std::endl;
        if (!ledger_->open(ledgerPath + "/ledger.db")) {
            utils::Logger::error("Failed to open ledger");
            return false;
        }
        if (!config_.tui) std::cout << "Ledger opened successfully" << std::endl;
        
        if (!config_.tui) std::cout << "Opening knowledge DB..." << std::endl;
        if (!knowledge_->open(knowledgePath + "/knowledge.db")) {
            utils::Logger::error("Failed to open knowledge DB");
            return false;
        }
        
        if (!config_.tui) std::cout << "Opening transfer DB..." << std::endl;
        if (!transfer_->open(transferPath + "/transfer.db")) {
            utils::Logger::error("Failed to open transfer DB");
            return false;
        }
        
        if (!config_.tui) std::cout << "Opening consensus DB..." << std::endl;
        if (!consensus_->open(consensusPath + "/consensus.db")) {
            utils::Logger::error("Failed to open consensus DB");
            return false;
        }

        if (!config_.tui) std::cout << "Opening PoE v1 DB..." << std::endl;
        if (!poeV1_->open(poePath + "/poe.db")) {
            utils::Logger::error("Failed to open PoE v1 DB");
            return false;
        }

        {
            std::string reason;
            if (!updateInstaller_.open(updatesPath + "/installer.state", &reason)) {
                utils::Logger::error("Failed to open update installer state: " + reason);
                return false;
            }
        }

        {
            std::string reason;
            if (!implantSafetyPipeline_.open(updatesPath + "/implant_safety.state", &reason)) {
                utils::Logger::error("Failed to open implant safety pipeline state: " + reason);
                return false;
            }
        }

        if (!config_.cli) {
            if (!initNaanCoordination()) {
                utils::Logger::error("Failed to initialize NAAN coordination runtime");
                return false;
            }
        }

	        core::PoeV1Config poeCfg;
            auto& runtimeCfg = utils::Config::instance();
	        poeCfg.validatorMode = config_.poeValidatorMode;
	        poeCfg.validatorMinStakeAtoms = poeMinStakeAtoms();
	        poeCfg.powBits = (config_.dev || config_.regtest) ? 12 : 16;
            const bool strictMainnetPoe = !(config_.dev || config_.regtest);
            const bool adaptiveMajorityVoting = runtimeCfg.getBool("poe.validators_majority", false);
            const bool adaptiveValidatorQuorum = adaptiveMajorityVoting || runtimeCfg.getBool("poe.validators_adaptive", false);
            int64_t validatorsN = runtimeCfg.getInt64("poe.validators_n", adaptiveMajorityVoting ? 0 : (strictMainnetPoe ? 3 : 1));
            if (adaptiveMajorityVoting) {
                validatorsN = 0;
            } else if (adaptiveValidatorQuorum) {
                if (validatorsN < 0) validatorsN = 0;
            } else if (validatorsN < 1) {
                validatorsN = 1;
            }
            if (validatorsN > 64) validatorsN = 64;
            int64_t validatorsM = runtimeCfg.getInt64("poe.validators_m", strictMainnetPoe ? 2 : 1);
            if (adaptiveValidatorQuorum) {
                if (validatorsM < 0) validatorsM = 0;
            } else if (validatorsM < 1) {
                validatorsM = 1;
            }
            if (validatorsN > 0 && validatorsM > validatorsN) validatorsM = validatorsN;
	        poeCfg.validatorsN = static_cast<uint32_t>(validatorsN);
	        poeCfg.validatorsM = static_cast<uint32_t>(validatorsM);
            poeCfg.adaptiveQuorum = adaptiveValidatorQuorum;
            poeCfg.adaptiveMajority = adaptiveMajorityVoting;
            int64_t adaptiveMinVotes = runtimeCfg.getInt64("poe.validators_min_votes", 1);
            if (adaptiveMinVotes < 1) adaptiveMinVotes = 1;
            if (adaptiveMinVotes > 64) adaptiveMinVotes = 64;
            poeCfg.adaptiveMinVotes = static_cast<uint32_t>(adaptiveMinVotes);
            const bool configuredAllowSelfBootstrap = runtimeCfg.getBool(
                "poe.allow_self_validator_bootstrap",
                adaptiveMajorityVoting ? true : !strictMainnetPoe);
            poeSelfValidatorBootstrapConfiguredAllow_ = configuredAllowSelfBootstrap;
            {
                std::lock_guard<std::mutex> lock(poeSelfValidatorBootstrapMtx_);
                poeSelfValidatorBootstrapState_.configuredAllowSelfBootstrap = configuredAllowSelfBootstrap;
                poeSelfValidatorBootstrapState_.persistentStrictMode =
                    runtimeCfg.getBool("poe.self_validator_bootstrap_locked_off", false);
                int64_t lockedAt = runtimeCfg.getInt64("poe.self_validator_bootstrap_locked_at", 0);
                if (lockedAt < 0) lockedAt = 0;
                poeSelfValidatorBootstrapState_.strictModeActivatedAt = static_cast<uint64_t>(lockedAt);
                poeSelfValidatorBootstrapState_.strictModeReason =
                    runtimeCfg.getString("poe.self_validator_bootstrap_lock_reason", "");
            }
            poeCfg.allowSelfBootstrapValidator = configuredAllowSelfBootstrap;
            {
                int64_t noveltyBands = runtimeCfg.getInt64("poe.novelty_bands", static_cast<int64_t>(poeCfg.noveltyBands));
                if (noveltyBands < 0) noveltyBands = 0;
                if (noveltyBands > 16) noveltyBands = 16;
                poeCfg.noveltyBands = static_cast<uint32_t>(noveltyBands);

                int64_t noveltyHamming = runtimeCfg.getInt64("poe.novelty_max_hamming", static_cast<int64_t>(poeCfg.noveltyMaxHamming));
                if (noveltyHamming < 0) noveltyHamming = 0;
                if (noveltyHamming > 64) noveltyHamming = 64;
                poeCfg.noveltyMaxHamming = static_cast<uint32_t>(noveltyHamming);

                int64_t minSubmitInterval = runtimeCfg.getInt64(
                    "poe.min_submit_interval_seconds",
                    static_cast<int64_t>(poeCfg.minSubmitIntervalSeconds));
                if (minSubmitInterval < 1) minSubmitInterval = 1;
                if (minSubmitInterval > 86400) minSubmitInterval = 86400;
                poeCfg.minSubmitIntervalSeconds = static_cast<uint32_t>(minSubmitInterval);
            }
	        poeCfg.limits.minPowBits = poeCfg.powBits;
	        poeCfg.limits.maxPowBits = 28;
	        poeCfg.limits.maxTitleBytes = 512;
	        poeCfg.limits.maxBodyBytes = 65536;
	        poeV1_->setConfig(poeCfg);
        {
            std::vector<crypto::PublicKey> validators;

            crypto::PublicKey selfPub{};
            bool hasSelfPub = false;
            if (keys_ && keys_->isValid()) {
                auto pubV = keys_->getPublicKey();
                if (pubV.size() >= crypto::PUBLIC_KEY_SIZE) {
                    std::memcpy(selfPub.data(), pubV.data(), selfPub.size());
                    hasSelfPub = true;
                }
            }

            auto addValidatorHex = [&](const std::string& token) {
                std::string t = token;
                auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
                while (!t.empty() && isSpace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
                while (!t.empty() && isSpace(static_cast<unsigned char>(t.back()))) t.pop_back();
                if (t.empty()) return;

                if (t == "self") {
                    if (hasSelfPub) validators.push_back(selfPub);
                    return;
                }

                if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) t = t.substr(2);
                auto bytes = crypto::fromHex(t);
                if (bytes.size() != crypto::PUBLIC_KEY_SIZE) {
                    utils::Logger::warn("Invalid poe validator pubkey (expected 32 bytes hex): " + t);
                    return;
                }
                crypto::PublicKey pk{};
                std::memcpy(pk.data(), bytes.data(), pk.size());
                validators.push_back(pk);
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

            if (validators.empty() && hasSelfPub && poeCfg.allowSelfBootstrapValidator) {
                validators.push_back(selfPub);
            }

            if (!validators.empty()) {
                std::sort(validators.begin(), validators.end(), [](const crypto::PublicKey& a, const crypto::PublicKey& b) {
                    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
                });
                validators.erase(std::unique(validators.begin(), validators.end(), [](const crypto::PublicKey& a, const crypto::PublicKey& b) {
                    return a == b;
                }), validators.end());
                poeV1_->setStaticValidators(validators);
            }
        }

        updatePoeValidatorsFromStake();
        applyPoeSelfValidatorBootstrapPolicy(static_cast<uint64_t>(std::time(nullptr)), true);
        autoPoeEpochLastFinalizedCount_.store(poeV1_ ? poeV1_->totalFinalized() : 0);
        if (poeV1_ && !poeV1_->listEpochIds(1).empty()) {
            autoPoeEpochLastRunAt_.store(static_cast<uint64_t>(std::time(nullptr)));
        } else {
            autoPoeEpochLastRunAt_.store(0);
        }
        
        if (!config_.tui) std::cout << "Setting up callbacks..." << std::endl;
        networkHeight_ = ledger_->height();

        if (keys_ && keys_->isValid()) {
            ledger_->setSigner([this](const crypto::Hash256& hash) {
                return signHash(hash);
            });
        }
        
        if (!config_.tui) std::cout << "Setting up knowledge callbacks..." << std::endl;
        knowledge_->onNewEntry([this](const core::KnowledgeEntry& entry) {
            std::string h = crypto::toHex(entry.hash);
            {
                std::lock_guard<std::mutex> lock(invMtx_);
                knowledgeByHash_[h] = entry.id;
                knownKnowledge_.insert(h);
            }
            if (suppressCallbacks_) return;
            broadcastInv(synapse::InvType::KNOWLEDGE, entry.hash);
            
            if (keys_ && keys_->isValid() && ledger_) {
                core::Event ev{};
                ev.timestamp = entry.timestamp;
                ev.type = core::EventType::KNOWLEDGE;
                ev.data = entry.serialize();
                auto pub = keys_->getPublicKey();
                if (pub.size() >= ev.author.size()) {
                    std::memcpy(ev.author.data(), pub.data(), ev.author.size());
                }
                ledger_->append(ev);
            }
        });
        
        if (!config_.tui) std::cout << "Setting up transfer callbacks..." << std::endl;
        transfer_->onNewTransaction([this](const core::Transaction& tx) {
            std::string h = crypto::toHex(tx.txid);
            {
                std::lock_guard<std::mutex> lock(invMtx_);
                knownTxs_.insert(h);
            }
            if (suppressCallbacks_) return;
            broadcastInv(synapse::InvType::TX, tx.txid);
            
            if (keys_ && keys_->isValid() && ledger_) {
                core::Event ev{};
                ev.timestamp = tx.timestamp;
                ev.type = core::EventType::TRANSFER;
                ev.data = tx.serialize();
                auto pub = keys_->getPublicKey();
                if (pub.size() >= ev.author.size()) {
                    std::memcpy(ev.author.data(), pub.data(), ev.author.size());
                }
                ledger_->append(ev);
            }
        });
        
        if (!config_.tui) std::cout << "Setting up ledger callbacks..." << std::endl;
        ledger_->onNewBlock([this](const core::Block& block) {
            {
                std::lock_guard<std::mutex> lock(invMtx_);
                knownBlocks_.insert(crypto::toHex(block.hash));
            }
            broadcastInv(synapse::InvType::BLOCK, block.hash);
        });
        
        utils::Logger::info("Core subsystems initialized");
        if (!config_.tui) std::cout << "Core initialization complete!" << std::endl;
        return true;
    }
    
	    bool initModel() {
	        modelLoader_ = std::make_unique<model::ModelLoader>();
	        modelAccess_ = std::make_unique<model::ModelAccess>();
            modelMarketplace_ = std::make_unique<model::ModelMarketplace>();

            try {
                std::string modeStr = utils::Config::instance().getString("model.access.mode", "PRIVATE");
                modelAccess_->setMode(rpc::parseAccessMode(modeStr));
            } catch (...) {
                modelAccess_->setMode(model::AccessMode::PRIVATE);
            }
            {
                int slots = utils::Config::instance().getInt("model.access.max_slots", 3);
                if (slots < 1) slots = 1;
                modelAccess_->setMaxSlots(static_cast<uint32_t>(slots));
            }
            {
                int64_t p = utils::Config::instance().getInt64("model.access.price_per_hour_atoms", 0);
                if (p < 0) p = 0;
                modelAccess_->setPrice(static_cast<uint64_t>(p));
            }

            {
                std::string listingId;
                {
                    std::lock_guard<std::mutex> lock(remoteProvMtx_);
                    listingId = localOfferId_;
                }
                if (!listingId.empty() && modelMarketplace_) {
                    modelMarketplace_->upsertModel(
                        listingId,
                        address_,
                        "active",
                        "",
                        0,
                        "GGUF",
                        modelAccess_->getPrice(),
                        remotePricePerRequestAtoms_,
                        modelAccess_->getMaxSlots(),
                        false
                    );
                }
            }
        
        std::string modelDir = config_.dataDir + "/models";
        std::filesystem::create_directories(modelDir);
        
        auto models = modelLoader_->listModels(modelDir);
        if (!models.empty()) {
            utils::Logger::info("Found " + std::to_string(models.size()) + " local models");
        }
        
	        return true;
	    }

	    bool ensureWebSubsystem() {
	        std::lock_guard<std::mutex> lock(webMtx_);
	        if (webSearch_ && webAi_ && webDetector_ && webExtractor_) return true;

	        webSearch_ = std::make_unique<web::WebSearch>();
	        webDetector_ = std::make_unique<web::QueryDetector>();
	        webExtractor_ = std::make_unique<web::HtmlExtractor>();
	        webAi_ = std::make_unique<web::AIWrapper>();

	        if (!webAi_->init()) {
	            webAi_.reset();
	            webSearch_.reset();
	            webDetector_.reset();
	            webExtractor_.reset();
	            return false;
	        }

	        webAi_->setWebSearch(webSearch_.get());
	        webAi_->setDetector(webDetector_.get());
	        webAi_->setExtractor(webExtractor_.get());
	        webAi_->enableAutoSearch(true);
	        webAi_->enableContextInjection(true);

	        webSearch_->onSearchError([this](const std::string& err) {
                const bool torRequired = agentTorRequired_.load();
                const bool torReadyForWeb = agentTorWebReady_.load();
                const std::string kind =
                    core::classifyWebSearchFailureKind(err, torRequired, torReadyForWeb);
                if (kind == "TOR_BOOTSTRAP_INCOMPLETE") {
                    utils::Logger::warn("Web search blocked: Tor bootstrap incomplete (reason=" +
                                       getTorBootstrapReasonCodeCached() + "): " + err);
                } else if (kind == "TIMEOUT") {
                    utils::Logger::warn("Web search timeout: " + err);
                } else {
                    utils::Logger::warn("Web search: " + err);
                }
	        });

	        web::SearchConfig cfg;
	        std::string webCfgPath = config_.dataDir + "/web_search.conf";
	        web::loadSearchConfig(webCfgPath, cfg);
            const std::string ahmiaOnion = "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/";
            std::string naanWebCfgPath = config_.dataDir + "/naan_agent_web.conf";
            if (!std::filesystem::exists(naanWebCfgPath)) {
                std::ofstream naanCfg(naanWebCfgPath);
                if (naanCfg.is_open()) {
                    naanCfg << "clearnet_engines=duckduckgo\n";
                    naanCfg << "darknet_engines=ahmia,torch,darksearch,deepsearch\n";
                    naanCfg << "custom_darknet_urls=" << ahmiaOnion << "\n";
                    naanCfg << "route_clearnet_through_tor=1\n";
                    naanCfg << "naan_force_tor_mode=1\n";
                    naanCfg << "naan_auto_search_enabled=1\n";
                    naanCfg << "naan_auto_search_mode=both\n";
                    naanCfg << "naan_auto_search_queries=latest space engineering research,latest ai research papers,open source systems engineering best practices\n";
                    naanCfg << "naan_auto_search_max_results=4\n";
                    naanCfg << "clearnet_site_allowlist=\n";
                    naanCfg << "clearnet_site_denylist=\n";
                    naanCfg << "onion_site_allowlist=\n";
                    naanCfg << "onion_site_denylist=\n";
                    naanCfg << "clearnet_route_bypass_hosts=\n";
                    naanCfg << "onion_route_bypass_hosts=\n";
                    naanCfg << "bypass_onion_https_fallback=1\n";
                }
            }
            web::SearchConfigValidationStats validation{};
            web::loadSearchConfigOverlay(naanWebCfgPath, cfg, &validation);
            web::sanitizeSearchConfig(cfg);
            cfg.tor.runtimeMode = configuredTorRuntimeMode();
            cfg.tor.socksHost = configuredTorSocksHost();
            cfg.tor.socksPort = configuredTorSocksPort();
            cfg.tor.controlHost = "127.0.0.1";
            cfg.tor.controlPort = configuredTorControlPort();
            naanWebConfigTotalLines_.store(validation.totalLines);
            naanWebConfigAppliedLines_.store(validation.appliedLines);
            naanWebConfigInvalidLines_.store(validation.invalidLines);
            naanWebConfigUnknownKeys_.store(validation.unknownKeys);
            naanWebConfigUnknownKeySamples_ = validation.unknownKeySamples;
            naanWebConfigSanitizedWrites_.fetch_add(1);

            bool hasAhmiaOnion = false;
            for (const auto& url : cfg.customDarknetUrls) {
                if (web::normalizeUrl(url) == web::normalizeUrl(ahmiaOnion)) {
                    hasAhmiaOnion = true;
                    break;
                }
            }
            if (!hasAhmiaOnion) {
                cfg.customDarknetUrls.push_back(ahmiaOnion);
            }
	        if (cfg.connectorAuditDir.empty()) {
	            cfg.connectorAuditDir = config_.dataDir + "/audit/connectors";
	        }

	        webExtractor_->setRemoveAds(cfg.removeAds);
	        webExtractor_->setRemoveScripts(cfg.removeScripts);
	        webExtractor_->setRemoveStyles(cfg.removeStyles);
	        webExtractor_->setRiskPolicy(cfg.extractionRisk);

            const bool torRequired = agentTorRequired_.load();
            const auto route = refreshTorRoutePolicy(true);
            const bool torReachable = agentTorReachable_.load();
            const bool hardTorOnly = torRequired || cfg.naanForceTorMode;

            const bool onionOptIn = utils::Config::instance().getBool("web.inject.onion", false);
            const bool torClearnetOptIn = utils::Config::instance().getBool("web.inject.tor_clearnet", false);
            if (hardTorOnly) {
                cfg.routeClearnetThroughTor = true;
                cfg.enableClearnet = torReachable && route.allowWebClearnet;
                cfg.enableDarknet = torReachable && route.allowWebOnion;
            } else {
                cfg.enableClearnet = true;
                cfg.enableDarknet = onionOptIn;
                cfg.routeClearnetThroughTor = torClearnetOptIn;
            }
            webSearch_->init(cfg);
            return true;
        }
	    
	    bool initPrivacy() {
	        if (!config_.privacyMode) {
	            utils::Logger::info("Privacy mode: disabled");
	            return true;
        }
        
        privacy_ = std::make_unique<privacy::Privacy>();
        privacy::PrivacyConfig privConfig;
        privConfig.useTor = true;
        privConfig.circuitCount = 3;
        privConfig.rotateIdentity = true;
        privConfig.rotationInterval = 3600;
        privConfig.torSocksHost = configuredTorSocksHost();
        privConfig.torSocksPort = configuredTorSocksPort();
        privConfig.torControlHost = "127.0.0.1";
        privConfig.torControlPort = configuredTorControlPort();
        core::OnionServicePolicyInput onionInput;
        onionInput.networkPort = network_ ? network_->getPort() : config_.port;
        onionInput.overrideVirtualPort = utils::Config::instance().getInt64("agent.tor.onion.virtual_port", 0);
        onionInput.overrideTargetPort = utils::Config::instance().getInt64("agent.tor.onion.target_port", 0);
        onionInput.dataDir = config_.dataDir;
        const auto onionPolicy = core::evaluateOnionServicePolicy(onionInput);
        privConfig.onionServiceDir = onionPolicy.serviceDir;
        privConfig.onionVirtualPort = onionPolicy.virtualPort;
        privConfig.onionTargetPort = onionPolicy.targetPort;
        
        if (!privacy_->init(privConfig)) {
            if (agentTorRequired_.load()) {
                utils::Logger::warn("Failed to initialize privacy layer; entering deterministic degraded mode");
                agentTorDegraded_.store(true);
                return true;
            }
            utils::Logger::error("Failed to initialize privacy layer");
            return false;
        }
        
        const bool torRequired = agentTorRequired_.load();
        bool privacyEnabled = privacy_->enable(privacy::PrivacyMode::FULL);
        const auto recovery = core::runTorPrivacyEnableRecovery(
            privacyEnabled,
            torRequired,
            8,
            [this]() {
                const auto route = refreshTorRoutePolicy(true);
                core::TorPrivacyBootstrapProbe probe;
                probe.torReachable = agentTorReachable_.load();
                probe.torDegraded = route.torDegraded;
                return probe;
            },
            [this, &privConfig]() {
                privacy_->shutdown();
                return privacy_->init(privConfig);
            },
            [this]() {
                return privacy_->enable(privacy::PrivacyMode::FULL);
            },
            [](int ms) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            });
        privacyEnabled = recovery.enabled;
        const int torBootstrapRecoveryRetries = recovery.retryAttemptsUsed;

        if (!privacyEnabled) {
            if (torRequired) {
                const auto route = refreshTorRoutePolicy(true);
                const bool torReachable = agentTorReachable_.load();
                agentTorDegraded_.store(route.torDegraded);
                updateAndLogTorReadinessState(torRequired, torReachable, agentTorWebReady_.load(), route.torDegraded);
                if (torReachable && !route.torDegraded) {
                    utils::Logger::warn(
                        "Tor route is reachable, but privacy FULL mode (onion service) failed; continuing without onion service");
                } else {
                    utils::Logger::warn("Failed to enable Tor during startup bootstrap; entering deterministic degraded mode");
                }
                return true;
            }
            utils::Logger::error("Failed to enable Tor");
            return false;
        }
        
        std::string onion = privacy_->getOnionAddress();
        agentTorDegraded_.store(false);
        if (torBootstrapRecoveryRetries > 0) {
            utils::Logger::info(
                "Tor bootstrap recovered after " + std::to_string(torBootstrapRecoveryRetries) +
                " privacy retry attempts");
        }
        utils::Logger::info("Privacy mode enabled: " + onion);
        utils::Logger::info("Tor control reachable: " + std::string(probeTorControl() ? "yes" : "no"));
        utils::Logger::info("Onion service active: " + std::string(isOnionServiceActive() ? "yes" : "no"));
        if (!activeTorSeedAddress().empty()) {
            utils::Logger::info("Tor seed address: " + activeTorSeedAddress());
        }
        return true;
    }
    
    bool initRPC() {
        if (config_.rpcPort == 0) {
            utils::Logger::info("RPC server: disabled");
            return true;
        }

        if (!initializeRpcAuth()) {
            return false;
        }
        
        rpc_ = std::make_unique<web::RpcServer>();
        if (config_.rpcAuthRequired) {
            rpc_->setAuthCallback([this](const std::string& token) {
                return rpcAuthMatches(token);
            });
        }
        rpc_->setHealthEndpointHandler([this]() {
            web::HttpEndpointResponse response;
            response.statusCode = running_.load() ? 200 : 503;
            json out;
            out["status"] = running_.load() ? "ok" : "stopped";
            out["live"] = running_.load();
            out["software"] = "synapsed";
            out["version"] = "0.1.0-beta";
            out["uptimeSeconds"] = getStats().uptime;
            out["rpcAuthenticated"] = config_.rpcAuthRequired;
            out["torRequired"] = agentTorRequired_.load();
            out["torDegraded"] = agentTorDegraded_.load();
            response.body = out.dump();
            return response;
        });
        rpc_->setReadinessEndpointHandler([this]() {
            web::HttpEndpointResponse response;
            const bool live = running_.load();
            const bool coreReady = ledger_ && transfer_ && consensus_ && poeV1_;
            const auto route = refreshTorRoutePolicy(true);
            const bool torReady = !agentTorRequired_.load() || (route.allowP2PDiscovery && agentTorWebReady_.load());
            const bool ready = live && coreReady && torReady;
            response.statusCode = ready ? 200 : 503;
            json out;
            out["status"] = ready ? "ready" : "degraded";
            out["ready"] = ready;
            out["live"] = live;
            out["software"] = "synapsed";
            out["version"] = "0.1.0-beta";
            out["peersConnected"] = network_ ? network_->peerCount() : 0;
            out["torRequired"] = agentTorRequired_.load();
            out["torReadyForWeb"] = agentTorWebReady_.load();
            out["torDegraded"] = route.torDegraded;
            out["poeBootstrap"] = currentPoeSelfValidatorBootstrapStatus();
            if (!live) {
                out["reason"] = "stopping";
            } else if (!coreReady) {
                out["reason"] = "core_not_ready";
            } else if (!torReady) {
                out["reason"] = "tor_not_ready";
            } else {
                out["reason"] = "ready";
            }
            response.body = out.dump();
            return response;
        });
        if (!rpc_->start(config_.rpcPort, config_.rpcBindAddress)) {
            utils::Logger::error("Failed to start RPC server");
            return false;
        }
        if (config_.rpcAuthRequired) {
            utils::Logger::info("RPC server listening on " + config_.rpcBindAddress + ":" + std::to_string(config_.rpcPort) +
                                " with cookie authentication at " + rpcCookieFilePath_);
        } else {
            utils::Logger::warn("RPC server listening on " + config_.rpcBindAddress + ":" + std::to_string(config_.rpcPort) +
                                " without authentication");
        }

        rpc::registerCoreRpcMethods(*rpc_, *this);
	        
	        utils::Logger::info("RPC server started on port " + std::to_string(config_.rpcPort));
	        return true;
	    }
    
    bool initMempool() {
        if (transfer_) {
            transfer_->setMaxMempoolSize(static_cast<size_t>(config_.maxMempool) * 1024);
        }
        utils::Logger::info("Mempool initialized: " + std::to_string(config_.maxMempool) + " MB");
        return true;
    }

    int runWithTUI() {
#if !SYNAPSE_BUILD_TUI
        std::cerr << "TUI support was disabled at build time; reconfigure with -DBUILD_TUI=ON.\n";
        return runDaemon();
#else
        const char* term = std::getenv("TERM");
        bool stdin_tty = isatty(STDIN_FILENO);
        bool stdout_tty = isatty(STDOUT_FILENO);
        
        if (!term) {
            std::cerr << "TERM environment variable not set. Try: export TERM=xterm-256color\n";
            return 1;
        }
        
        if (!stdin_tty || !stdout_tty) {
            std::cerr << "Not running in a proper terminal. TUI requires an interactive terminal.\n";
            std::cerr << "Running in daemon mode instead...\n";
            return runDaemon();
        }
        
        tui::TUI ui;
        if (!ui.init()) {
            utils::Logger::error("Failed to initialize TUI");
            std::cerr << "Failed to initialize TUI (ncurses). Possible issues:\n";
            std::cerr << "1. Terminal too small (minimum 80x24)\n";
            std::cerr << "2. TERM variable incorrect: " << term << "\n";
            std::cerr << "3. Not running in interactive terminal\n";
            std::cerr << "Falling back to daemon mode...\n";
            return runDaemon();
        }

        utils::Logger::enableConsole(false);
        
        if (network_ && network_->getPort() != 0) {
            ui.setNetworkPort(network_->getPort());
            ui.setNetworkOnline(true);
        } else {
            ui.setNetworkOnline(false);
        }
        
        node::startNodeThreads(
            networkThread_,
            consensusThread_,
            maintenanceThread_,
            syncThread_,
            [this]() { networkLoop(); },
            [this]() { consensusLoop(); },
            [this]() { maintenanceLoop(); },
            [this]() { syncLoop(); });

        tui::registerCoreTuiCommandHandler(ui, *this);
        
        std::unordered_set<std::string> notifiedKnowledgePaid;
        tui::TuiUpdateHooks updateHooks;
        updateHooks.shouldKeepRunning = [this]() { return running_.load() && g_running.load(); };
        updateHooks.refreshWalletState = [this]() {
            if (!keys_ || keys_->isValid()) {
                return;
            }
            std::string walletPath = config_.dataDir + "/wallet.dat";
            if (!std::filesystem::exists(walletPath) || !keys_->load(walletPath, "")) {
                return;
            }
            address_ = keys_->getAddress();
            updateSignerFromKeys();
            if (!poeV1_) {
                return;
            }
            auto current = poeV1_->getStaticValidators();
            if (!current.empty() || !poeV1_->getConfig().allowSelfBootstrapValidator) {
                return;
            }
            auto pubV = keys_->getPublicKey();
            if (pubV.size() < crypto::PUBLIC_KEY_SIZE) {
                return;
            }
            crypto::PublicKey pk{};
            std::memcpy(pk.data(), pubV.data(), pk.size());
            poeV1_->setStaticValidators({pk});
        };
        updateHooks.getCoreSnapshot = [this]() {
            const json status = currentPoeSelfValidatorBootstrapStatus();
            tui::TuiCoreSnapshotCollectorInputs inputs;
            inputs.network = network_.get();
            inputs.knowledge = knowledge_.get();
            inputs.poe = poeV1_.get();
            inputs.discovery = discovery_.get();
            inputs.poeBootstrapInfo.validatorCount = status.value("validatorCount", static_cast<uint64_t>(0));
            inputs.poeBootstrapInfo.selfBootstrapActive =
                status.value("effectiveAllowSelfValidatorBootstrap", false);
            inputs.poeBootstrapInfo.mode = status.value("mode", std::string("disabled"));
            inputs.syncProgress = syncProgress_;
            inputs.lastReward = autoPoeLastRewardMineAtoms_.load();
            inputs.lastRewardEpochId = autoPoeLastRewardEpochId_.load();
            inputs.lastRewardEntries = autoPoeLastRewardEntries_.load();
            inputs.lastRewardAt = autoPoeLastRewardAt_.load();
            inputs.peerDisplayInfo = [this](const network::Peer& peer) { return getPeerDisplayInfo(peer); };
            inputs.modelLoader = modelLoader_.get();
            inputs.modelsDirectory = config_.dataDir + "/models";
            inputs.walletReady = keys_ && keys_->isValid();
            inputs.walletAddress = address_;
            inputs.walletBalance = currentWalletBalanceAtoms();
            inputs.walletPending = currentWalletPendingAtoms();
            inputs.blockHeight = ledger_ ? ledger_->height() : 0;
            inputs.quantumEnabled = config_.quantumSecurity;
            inputs.liboqsEnabled = node::quantumLiboqsEnabled();
            inputs.quantumCapabilityMode = node::quantumCapabilityMode();
            const auto pqcStatus = quantum::getPQCBackendStatus();
            inputs.kyberReal = pqcStatus.kyberReal;
            inputs.dilithiumReal = pqcStatus.dilithiumReal;
            inputs.sphincsReal = pqcStatus.sphincsReal;
            inputs.kyberImplementation = node::quantumKyberImplementationMode();
            inputs.dilithiumImplementation = node::quantumDilithiumImplementationMode();
            inputs.sphincsImplementation = node::quantumSphincsImplementationMode();
            return tui::collectTuiCoreSnapshot(inputs);
        };
        updateHooks.getKnowledgeRefresh = [this, &notifiedKnowledgePaid]() {
            tui::TuiKnowledgeSnapshotCollectorInputs inputs;
            inputs.poe = poeV1_.get();
            inputs.acceptanceRewardCredited = [this](const crypto::Hash256& submitId) {
                return transfer_
                    ? transfer_->hasTransaction(node::rewardIdForAcceptance(submitId))
                    : false;
            };
            inputs.addressFromPubKey = [this](const crypto::PublicKey& pubKey) { return addressFromPubKey(pubKey); };
            inputs.currentAddress = address_;
            inputs.atomsToNgt = [this](uint64_t atoms) { return atomsToNgt(atoms); };
            return tui::collectTuiKnowledgeRefresh(inputs, notifiedKnowledgePaid);
        };
        updateHooks.getAttachedAgentStatus = [this]() {
            const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
            const auto score = agentScore_.snapshot();
            const bool runtimeInitialized = naanRuntimeInitialized_.load();
            const auto runtimeState = runtimeInitialized
                ? currentNaanFailoverState(now)
                : core::AgentRuntimeFailoverState::RECOVERY;
            const auto crashState = naanRuntimeSupervisor_.crashState();
            const auto schedulerState = naanTaskScheduler_.snapshot();
            const auto adaptiveSchedule = agentAdaptiveScheduler_.schedule(score.throttled, score.quarantined);
            const bool torRequired = agentTorRequired_.load();
            bool torReachable = probeTorSocks();
            if (!torReachable && torRequired && !agentTorManaged_.load()) {
                bool started = maybeStartManagedTorRuntimeWithBackoff(true, "attached_status");
                if (started) {
                    agentTorManaged_.store(true);
                    torReachable = probeTorSocks();
                }
            }
            if (torReachable) {
                resetManagedTorRestartBackoffState();
            }
            agentTorReachable_.store(torReachable);
            refreshTorWebReadiness(torReachable, false);
            core::TorRoutePolicyInput routeIn;
            routeIn.torRequired = torRequired;
            routeIn.torReachable = torReachable;
            routeIn.allowClearnetFallback = agentAllowClearnetFallback_.load();
            routeIn.allowP2PFallback = agentAllowP2PFallback_.load();
            const auto route = core::evaluateTorRoutePolicy(routeIn);
            agentTorDegraded_.store(route.torDegraded);
            updateAndLogTorReadinessState(torRequired, torReachable, agentTorWebReady_.load(), route.torDegraded);

            tui::TuiAttachedAgentStatusCollectorInputs inputs;
            inputs.runtimeInitialized = runtimeInitialized;
            inputs.runtimeState = runtimeState;
            inputs.crashState = crashState;
            inputs.schedulerState = schedulerState;
            inputs.adaptiveSchedule = adaptiveSchedule;
            inputs.score = score;
            inputs.draftQueue = &agentDraftQueue_;
            inputs.pipeline = {
                naanPipelineRuns_.load(),
                naanPipelineApproved_.load(),
                naanPipelineRejected_.load(),
                naanPipelineSubmitted_.load()};
            inputs.timing = {
                naanLastActionTs_.load(),
                naanLastReviewTs_.load(),
                naanLastDraftTs_.load(),
                naanLastHeartbeatTs_.load(),
                naanTickCount_.load()};
            inputs.taskRuns = {
                naanTaskResearchRuns_.load(),
                naanTaskVerifyRuns_.load(),
                naanTaskReviewRuns_.load(),
                naanTaskDraftRuns_.load(),
                naanTaskSubmitRuns_.load()};
            inputs.tor = {
                torRequired,
                torReachable,
                agentTorWebReady_.load(),
                agentTorOnionReady_.load(),
                probeTorControl(),
                configuredTorControlPort(),
                agentTorManaged_.load(),
                route,
                agentTorBootstrapPercent_.load(),
                configuredTorSocksHost(),
                configuredTorSocksPort(),
                likelyTor9050vs9150ConflictHint(torReachable),
                agentAllowClearnetFallback_.load(),
                agentAllowP2PFallback_.load(),
                configuredTorRuntimeMode(),
                config_.privacyMode,
                isOnionServiceActive(),
                activeOnionAddress(),
                activeTorSeedAddress()};
            inputs.config = {
                config_.dataDir,
                config_.configPath.empty() ? (config_.dataDir + "/synapsenet.conf") : config_.configPath,
                config_.dataDir + "/web_search.conf",
                naanRedactionCount_.load(),
                securityPolicyHashes().first,
                config_.port,
                syncProgress_,
                config_.maxPeers,
                config_.maxInbound,
                config_.maxOutbound,
                networkHeight_.load()};
            inputs.connectorAbuseLastAt = naanConnectorAbuseLastAt_.load();
            inputs.network = network_.get();
            inputs.invBackpressureDrops = invBackpressureDrops_.load();
            inputs.getDataBackpressureDrops = getDataBackpressureDrops_.load();
            inputs.gossipSuppressed = gossipSuppressed_.load();
            inputs.gossipSubsetRouted = gossipSubsetRouted_.load();
            inputs.ledger = ledger_.get();
            inputs.miningActive = miningActive_.load();
            inputs.miningHashAttemptsTotal = miningHashAttemptsTotal_.load();
            inputs.miningHashAttemptsLast = miningHashAttemptsLast_.load();
            inputs.miningLastSolvedAt = miningLastSolvedAt_.load();
            inputs.miningWorkTarget =
                "leading_zero_bits>=" + std::to_string(miningWorkTargetBits_.load());
            {
                std::lock_guard<std::mutex> lock(miningStateMtx_);
                inputs.miningCandidateHash = crypto::toHex(miningCandidateHash_);
            }
            inputs.storageAudit = naanAuditLog_.stats();
            inputs.storageRecoveredLines = naanStorageRecoveredLines_.load();
            inputs.storageDroppedSegments = naanStorageDroppedSegments_.load();
            inputs.storageIndexRecoveryRuns = naanIndexRecoveryRuns_.load();
            inputs.storageIndexRecoveryLastAt = naanIndexRecoveryLastAt_.load();
            inputs.storageConsistencyChecks = naanConsistencyChecks_.load();
            inputs.storageConsistencyRepairs = naanConsistencyRepairs_.load();
            inputs.storageConsistencyLastAt = naanConsistencyLastAt_.load();

            std::lock_guard<std::mutex> lock(webMtx_);
            if (webSearch_) {
                inputs.webConnectorAvailable = true;
                inputs.webConnectorHealth = webSearch_->getConnectorHealth();
                inputs.webStats = webSearch_->getStats();
            }
            {
                std::lock_guard<std::mutex> webResearchLock(naanWebResearchMtx_);
                inputs.webLastSearchAt = naanWebResearchSnapshot_.lastSearchAt;
                inputs.webQuery = naanWebResearchSnapshot_.query;
                inputs.webQueryType = naanWebResearchSnapshot_.queryType;
                inputs.webResultCount = naanWebResearchSnapshot_.resultCount;
                inputs.webClearnetResults = naanWebResearchSnapshot_.clearnetResults;
                inputs.webOnionResults = naanWebResearchSnapshot_.onionResults;
                inputs.webTopSites = naanWebResearchSnapshot_.topSites;
                inputs.webSaved = naanWebResearchSnapshot_.saved;
                inputs.webSkipReason = naanWebResearchSnapshot_.skipReason;
                inputs.webError = naanWebResearchSnapshot_.error;
            }
            return tui::collectAttachedAgentStatus(inputs);
        };
        updateHooks.getObservatoryFeed = [this]() {
            tui::TuiObservatoryFeedBuilderHooks hooks;
            hooks.listObservatoryEntries = [this]() { return agentCoordination_.getObservatoryFeed(0, 120); };
            hooks.redactPayload = [this](const std::string& payload) { return redactPotentialSecrets(payload); };
            hooks.onPayloadRedacted = [this]() { naanRedactionCount_.fetch_add(1); };
            return tui::buildObservatoryFeed(hooks);
        };
        updateHooks.getAgentEvents = [this]() {
            tui::TuiAgentEventsBuilderHooks hooks;
            hooks.listAgentEvents = [this]() {
                std::vector<tui::TuiRawAgentEvent> raw;
                auto uiEvents = snapshotNaanUiEvents();
                raw.reserve(uiEvents.size());
                for (const auto& event : uiEvents) {
                    tui::TuiRawAgentEvent item;
                    item.timestamp = event.timestamp;
                    item.category = event.category;
                    item.message = event.message;
                    raw.push_back(std::move(item));
                }
                return raw;
            };
            return tui::buildAgentEvents(hooks);
        };
        std::thread updateThread = tui::startTuiUpdateThread(ui, updateHooks);
        
        ui.run();
        
        running_ = false;
        updateThread.join();
        node::stopNodeThreads(networkThread_, consensusThread_, maintenanceThread_, syncThread_);
        ui.shutdown();
        utils::Logger::enableConsole(true);
        
        return 0;
#endif
    }
    
    int runDaemon() {
        node::DaemonRuntimeAdapterInputs inputs;
        inputs.config = &config_;
        inputs.networkThread = &networkThread_;
        inputs.consensusThread = &consensusThread_;
        inputs.maintenanceThread = &maintenanceThread_;
        inputs.syncThread = &syncThread_;
        inputs.networkLoop = [this]() { networkLoop(); };
        inputs.consensusLoop = [this]() { consensusLoop(); };
        inputs.maintenanceLoop = [this]() { maintenanceLoop(); };
        inputs.syncLoop = [this]() { syncLoop(); };
        inputs.reload = [this]() { reload(); };
        inputs.getStats = [this]() {
            const auto stats = getStats();
            node::DaemonRuntimeStats out;
            out.uptime = stats.uptime;
            out.peersConnected = stats.peersConnected;
            out.knowledgeEntries = stats.knowledgeEntries;
            out.syncProgress = stats.syncProgress;
            return out;
        };
        inputs.formatUptime = [](uint64_t uptime) { return formatUptime(uptime); };
        inputs.shouldKeepRunning = [this]() { return running_.load() && g_running.load(); };
        inputs.consumeReloadRequested = []() {
            if (!g_reloadConfig.load()) return false;
            g_reloadConfig = false;
            return true;
        };
        inputs.walletReady = [this]() { return keys_ && keys_->isValid(); };
        inputs.walletAddress = [this]() { return address_; };
        inputs.networkOnline = [this]() { return network_ && network_->getPort() != 0; };
        return node::runDaemonRuntimeAdapter(inputs);
    }
    
    void networkLoop() {
        node::NetworkRuntimeAdapterInputs inputs;
        inputs.config = &config_;
        inputs.shouldKeepRunning = [this]() { return running_.load() && g_running.load(); };
        inputs.torRequired = [this]() { return agentTorRequired_.load(); };
        inputs.probeTorSocks = [this]() { return probeTorSocks(); };
        inputs.maybeStartManagedTorRuntimeWithBackoff = [this](bool requireWebReady, const char* reason) {
            return maybeStartManagedTorRuntimeWithBackoff(requireWebReady, reason);
        };
        inputs.setTorManaged = [this](bool value) { agentTorManaged_.store(value); };
        inputs.resetManagedTorRestartBackoffState = [this]() { resetManagedTorRestartBackoffState(); };
        inputs.setTorReachable = [this](bool value) { agentTorReachable_.store(value); };
        inputs.refreshTorWebReadiness = [this](bool torReachable, bool strictOnionOnly) {
            refreshTorWebReadiness(torReachable, strictOnionOnly);
        };
        inputs.allowClearnetFallback = [this]() { return agentAllowClearnetFallback_.load(); };
        inputs.allowP2PFallback = [this]() { return agentAllowP2PFallback_.load(); };
        inputs.setTorDegraded = [this](bool value) { agentTorDegraded_.store(value); };
        inputs.torWebReady = [this]() { return agentTorWebReady_.load(); };
        inputs.updateAndLogTorReadinessState = [this](bool torRequired, bool torReachable, bool torWebReady, bool torDegraded) {
            updateAndLogTorReadinessState(torRequired, torReachable, torWebReady, torDegraded);
        };
        inputs.network = network_.get();
        inputs.discovery = discovery_.get();
        inputs.configuredTorSocksHost = [this]() { return configuredTorSocksHost(); };
        inputs.configuredTorSocksPort = [this]() { return configuredTorSocksPort(); };
        node::runNetworkRuntimeAdapter(inputs);
    }
    
    void consensusLoop() {
        node::runConsensusRuntimeAdapter(
            [this]() { return running_.load() && g_running.load(); },
            [this]() {
                if (consensus_) {
                    consensus_->processTimeouts();
                }
            });
    }
    
		    void maintenanceLoop() {
                node::MaintenanceLoopHooks hooks;
                hooks.config = &config_;
                hooks.shouldKeepRunning = [this]() { return running_.load() && g_running.load(); };
                hooks.tickNaanCoordinationSupervised = [this](uint64_t now) { tickNaanCoordinationSupervised(now); };
                hooks.applyPoeSelfValidatorBootstrapPolicy = [this](uint64_t now) {
                    applyPoeSelfValidatorBootstrapPolicy(now);
                };
                hooks.maybeRunAutoPoeEpoch = [this](uint64_t now) { maybeRunAutoPoeEpoch(now); };
                hooks.autoVoteSweepAllPending = [this]() { autoVoteSweepAllPending(); };
                hooks.processPoeSyncRetries = [this](uint64_t now, uint32_t limitEpochs) {
                    const auto retries =
                        node::collectPoeSyncRetryRequests(poeSync_, poeSyncMtx_, now, limitEpochs);
                    for (const auto& r : retries) {
                        sendPoeGetInv(r.peerId, r.kind, r.after, r.limit);
                    }
                };
                hooks.compactDatabase = [this]() {
                    if (db_) {
                        db_->compact();
                    }
                };
                hooks.performQuantumMaintenance = [this]() {
                    if (quantumManager_) {
                        quantumManager_->performMaintenance();
                    }
                };
                hooks.handleBlockBuildTick = [this]() {
                    node::BlockBuildTickInputs inputs;
                    inputs.ledger = ledger_.get();
                    inputs.torRequired = agentTorRequired_.load();
                    inputs.torWebReady = agentTorWebReady_.load();
                    if (inputs.torRequired) {
                        inputs.route = refreshTorRoutePolicy(true);
                    }
                    if (node::shouldBuildPendingBlock(inputs)) {
                        buildBlockFromPending();
                    } else {
                        if (ledger_ && ledger_->getPendingEventCount() != 0) {
                            miningFailClosedSkips_.fetch_add(1);
                        }
                    }
                };
                hooks.processRemoteModelMaintenance = [this](uint64_t now) {
                    if (modelAccess_) {
                        modelAccess_->processExpiredSessions();
                    }
                    if (!modelMarketplace_) {
                        return;
                    }
                    const auto expired =
                        node::collectExpiredProviderSessions(providerSessions_, remoteProvMtx_, now);
                    for (const auto& e : expired) {
                        if (modelAccess_) {
                            (void)modelAccess_->endSession(e.second.renterId);
                        }
                        (void)modelMarketplace_->endRental(e.first);
                    }
                };
                hooks.maybeBroadcastLocalOffer = [this](uint64_t now) {
                    if (!node::shouldBroadcastLocalOffer(
                            network_.get(),
                            modelAccess_.get(),
                            modelLoader_.get(),
                            remotePricePerRequestAtoms_)) {
                        return false;
                    }
                    if (modelMarketplace_) {
                        auto info = modelLoader_->getInfo();
                        modelMarketplace_->upsertModel(
                            localOfferId_,
                            address_,
                            info.name.empty() ? "active" : info.name,
                            "",
                            info.sizeBytes,
                            "GGUF",
                            modelAccess_->getPrice(),
                            remotePricePerRequestAtoms_,
                            modelAccess_->getMaxSlots(),
                            true
                        );
                    }
                    auto offer = buildLocalOffer(now);
                    network_->broadcast(makeMessage("m_offer", offer.serialize()));
                    return true;
                };
                node::runMaintenanceLoop(hooks);
	    }
    
    void syncLoop() {
        node::SyncRuntimeAdapterInputs inputs;
        inputs.shouldKeepRunning = [this]() { return running_.load() && g_running.load(); };
        inputs.ledger = ledger_.get();
        inputs.networkHeight = &networkHeight_;
        inputs.syncProgress = &syncProgress_;
        inputs.syncing = &syncing_;
        inputs.network = network_.get();
        inputs.requestedBlocks = &requestedBlocks_;
        inputs.syncMtx = &syncMtx_;
        inputs.requestBlock = [this](const std::string& peerId, uint64_t height) {
            sendGetBlock(peerId, height);
        };
        node::runSyncRuntimeAdapter(inputs);
    }

    static std::vector<uint8_t> serializeU64(uint64_t val) {
        std::vector<uint8_t> out(8);
        for (int i = 0; i < 8; i++) out[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xff);
        return out;
    }
    
    static uint64_t deserializeU64(const std::vector<uint8_t>& data) {
        if (data.size() < 8) return 0;
        uint64_t val = 0;
        for (int i = 0; i < 8; i++) val |= static_cast<uint64_t>(data[i]) << (i * 8);
        return val;
    }
    
    network::Message makeMessage(const std::string& command, const std::vector<uint8_t>& payload) {
        network::Message msg;
        msg.command = command;
        msg.payload = payload;
        msg.timestamp = std::time(nullptr);
        return msg;
    }
    
    crypto::Signature signHash(const crypto::Hash256& hash) {
        crypto::Signature sig{};
        if (!keys_ || !keys_->isValid()) return sig;
        auto privVec = keys_->getPrivateKey();
        if (privVec.size() < crypto::PRIVATE_KEY_SIZE) return sig;
        crypto::PrivateKey priv{};
        std::memcpy(priv.data(), privVec.data(), priv.size());
        sig = crypto::sign(hash, priv);
        return sig;
    }
    
    void updateSignerFromKeys() {
        if (keys_ && keys_->isValid() && ledger_) {
            ledger_->setSigner([this](const crypto::Hash256& hash) {
                return signHash(hash);
            });
        }
    }
    
    void buildBlockFromPending() {
        if (!ledger_) return;
        auto events = ledger_->getPendingEvents();
        if (events.empty()) return;
        core::Block block;
        block.height = ledger_->height();
        block.timestamp = std::time(nullptr);
        block.prevHash = ledger_->tipHash();
        block.events = std::move(events);
        block.difficulty = ledger_->currentDifficulty();
        block.merkleRoot = block.computeMerkleRoot();
        block.nonce = 0;
        block.hash = block.computeHash();

        miningActive_.store(true);
        miningWorkTargetBits_.store(block.difficulty);
        uint64_t attempts = 1;
        {
            std::lock_guard<std::mutex> lock(miningStateMtx_);
            miningCandidateHash_ = block.hash;
        }

        while (!block.meetsTarget()) {
            block.nonce++;
            block.hash = block.computeHash();
            attempts++;
            if ((attempts & 1023ULL) == 0ULL) {
                std::lock_guard<std::mutex> lock(miningStateMtx_);
                miningCandidateHash_ = block.hash;
            }
        }
        {
            std::lock_guard<std::mutex> lock(miningStateMtx_);
            miningCandidateHash_ = block.hash;
        }
        miningHashAttemptsLast_.store(attempts);
        miningHashAttemptsTotal_.fetch_add(attempts);
        miningLastSolvedAt_.store(static_cast<uint64_t>(std::time(nullptr)));
        miningActive_.store(false);

        if (!ledger_->appendBlockWithValidation(block)) {
            return;
        }

        if (transfer_) {
            std::vector<core::Transaction> blockTxs;
            for (const auto& ev : block.events) {
                if (ev.type != core::EventType::TRANSFER) continue;
                core::Transaction tx = core::Transaction::deserialize(ev.data);
                if (tx.txid == crypto::Hash256{}) continue;
                blockTxs.push_back(tx);
            }
            if (!blockTxs.empty()) {
                if (!transfer_->applyBlockTransactionsFromBlock(blockTxs, block.height, block.hash)) {
                    utils::Logger::error("Failed to apply block transfer events (local mined block)");
                }
            }
        }
    }
    
    void sendVersion(const std::string& peerId) {
        if (!network_) return;
        synapse::VersionMessage v{};
        v.version = 1;
        v.services = 0;
        v.timestamp = std::time(nullptr);
        v.nonce = static_cast<uint64_t>(std::random_device{}()) << 32 | std::random_device{}();
        v.userAgent = "SynapseNet:0.1";
        v.startHeight = ledger_ ? ledger_->height() : 0;
        v.relay = true;
        uint16_t port = network_->getPort();
        v.portRecv = port;
        v.portFrom = port;
        auto msg = makeMessage("version", v.serialize());
        network_->send(peerId, msg);
    }
    
    void sendVerack(const std::string& peerId) {
        if (!network_) return;
        auto msg = makeMessage("verack", {});
        network_->send(peerId, msg);
    }
    
    void sendGetAddr(const std::string& peerId) {
        if (!network_) return;
        auto msg = makeMessage("getaddr", {});
        network_->send(peerId, msg);
    }

    void sendMempoolRequest(const std::string& peerId) {
        if (!network_) return;
        auto msg = makeMessage("mempool", {});
        network_->send(peerId, msg);
    }
    
	    void sendGetBlock(const std::string& peerId, uint64_t height) {
	        if (!network_) return;
	        auto msg = makeMessage("getblock", serializeU64(height));
	        network_->send(peerId, msg);
	    }

		    static void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
		        out.push_back(static_cast<uint8_t>(v & 0xFF));
		        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
		        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
		        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
		    }

		    static void writeU64LE(std::vector<uint8_t>& out, uint64_t v) {
		        for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
		    }

		    static uint32_t readU32LE(const std::vector<uint8_t>& data, size_t off) {
		        if (off + 4 > data.size()) return 0;
		        return static_cast<uint32_t>(data[off]) |
		               (static_cast<uint32_t>(data[off + 1]) << 8) |
	               (static_cast<uint32_t>(data[off + 2]) << 16) |
		               (static_cast<uint32_t>(data[off + 3]) << 24);
		    }

		    static uint64_t readU64LE(const std::vector<uint8_t>& data, size_t off) {
		        if (off + 8 > data.size()) return 0;
		        uint64_t v = 0;
		        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(data[off + static_cast<size_t>(i)]) << (8 * i);
		        return v;
		    }

		    static bool hashLess(const crypto::Hash256& a, const crypto::Hash256& b) {
		        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
		    }

		    static crypto::Hash256 poeEpochInvHash(uint64_t epochId) {
		        crypto::Hash256 out{};
		        for (int i = 0; i < 8; ++i) {
		            out[24 + static_cast<size_t>(i)] = static_cast<uint8_t>((epochId >> (8 * (7 - i))) & 0xFF);
		        }
		        return out;
		    }

		    static std::optional<uint64_t> epochIdFromPoeInvHash(const crypto::Hash256& h) {
		        for (size_t i = 0; i < 24; ++i) {
		            if (h[i] != 0) return std::nullopt;
		        }
		        uint64_t v = 0;
		        for (size_t i = 0; i < 8; ++i) {
		            v = (v << 8) | static_cast<uint64_t>(h[24 + i]);
		        }
		        if (v == 0) return std::nullopt;
		        return v;
		    }

		    static std::vector<uint8_t> serializePoeEpoch(const core::PoeEpochResult& epoch) {
		        std::vector<core::PoeEpochAllocation> allocs = epoch.allocations;
		        std::sort(allocs.begin(), allocs.end(), [](const core::PoeEpochAllocation& a, const core::PoeEpochAllocation& b) {
		            return std::lexicographical_compare(a.contentId.begin(), a.contentId.end(), b.contentId.begin(), b.contentId.end());
		        });

		        std::vector<uint8_t> out;
		        out.reserve(8 + 4 + crypto::SHA256_SIZE + 8 + crypto::SHA256_SIZE + 4 +
		                    allocs.size() * (crypto::SHA256_SIZE + crypto::SHA256_SIZE + crypto::PUBLIC_KEY_SIZE + 8 + 8));
		        writeU64LE(out, epoch.epochId);
		        writeU32LE(out, epoch.iterations);
		        out.insert(out.end(), epoch.epochSeed.begin(), epoch.epochSeed.end());
		        writeU64LE(out, epoch.totalBudget);
		        out.insert(out.end(), epoch.allocationHash.begin(), epoch.allocationHash.end());
		        writeU32LE(out, static_cast<uint32_t>(allocs.size()));
		        for (const auto& a : allocs) {
		            out.insert(out.end(), a.submitId.begin(), a.submitId.end());
		            out.insert(out.end(), a.contentId.begin(), a.contentId.end());
		            out.insert(out.end(), a.authorPubKey.begin(), a.authorPubKey.end());
		            writeU64LE(out, a.score);
		            writeU64LE(out, a.amount);
		        }
		        return out;
		    }

		    static std::optional<core::PoeEpochResult> deserializePoeEpoch(const std::vector<uint8_t>& payload) {
		        const size_t headerSize = 8 + 4 + crypto::SHA256_SIZE + 8 + crypto::SHA256_SIZE + 4;
		        if (payload.size() < headerSize) return std::nullopt;
		        size_t off = 0;
		        uint64_t epochId = readU64LE(payload, off);
		        off += 8;
		        uint32_t iterations = readU32LE(payload, off);
		        off += 4;
		        crypto::Hash256 epochSeed{};
		        std::memcpy(epochSeed.data(), payload.data() + off, epochSeed.size());
		        off += epochSeed.size();
		        uint64_t totalBudget = readU64LE(payload, off);
		        off += 8;
		        crypto::Hash256 allocHash{};
		        std::memcpy(allocHash.data(), payload.data() + off, allocHash.size());
		        off += allocHash.size();
		        uint32_t count = readU32LE(payload, off);
		        off += 4;

		        const size_t itemSize = crypto::SHA256_SIZE + crypto::SHA256_SIZE + crypto::PUBLIC_KEY_SIZE + 8 + 8;
		        size_t need = off + static_cast<size_t>(count) * itemSize;
		        if (need > payload.size()) return std::nullopt;

		        std::vector<core::PoeEpochAllocation> allocations;
		        allocations.reserve(count);
		        for (uint32_t i = 0; i < count; ++i) {
		            core::PoeEpochAllocation a;
		            std::memcpy(a.submitId.data(), payload.data() + off, a.submitId.size());
		            off += a.submitId.size();
		            std::memcpy(a.contentId.data(), payload.data() + off, a.contentId.size());
		            off += a.contentId.size();
		            std::memcpy(a.authorPubKey.data(), payload.data() + off, a.authorPubKey.size());
		            off += a.authorPubKey.size();
		            a.score = readU64LE(payload, off);
		            off += 8;
		            a.amount = readU64LE(payload, off);
		            off += 8;
		            allocations.push_back(a);
		        }

		        core::PoeEpochResult out;
		        out.ok = true;
		        out.epochId = epochId;
		        out.iterations = iterations;
		        out.epochSeed = epochSeed;
		        out.totalBudget = totalBudget;
		        out.allocationHash = allocHash;
		        out.allocations = std::move(allocations);
		        return out;
		    }

	    void sendPoeInventory(const std::string& peerId) {
	        if (!network_ || !poeV1_) return;

	        const size_t maxEntries = config_.dev ? 250 : 100;
	        const size_t maxVotes = config_.dev ? 500 : 200;
	        const size_t maxEpochs = config_.dev ? 128 : 64;

	        auto entries = poeV1_->listEntryIds(maxEntries);
	        auto votes = poeV1_->listVoteIds(maxVotes);
	        auto epochs = poeV1_->listEpochIds(maxEpochs);
	        if (entries.empty() && votes.empty() && epochs.empty()) return;

	        synapse::InvMessage inv;
	        inv.items.reserve(entries.size() + votes.size() + epochs.size());

        for (const auto& sid : entries) {
            synapse::InvItem item;
            item.type = synapse::InvType::POE_ENTRY;
            std::memcpy(item.hash.data(), sid.data(), sid.size());
            inv.items.push_back(item);
        }

	        for (const auto& vid : votes) {
	            synapse::InvItem item;
	            item.type = synapse::InvType::POE_VOTE;
	            std::memcpy(item.hash.data(), vid.data(), vid.size());
	            inv.items.push_back(item);
	        }

	        for (uint64_t epochId : epochs) {
	            synapse::InvItem item;
	            item.type = synapse::InvType::POE_EPOCH;
	            crypto::Hash256 hid = poeEpochInvHash(epochId);
	            std::memcpy(item.hash.data(), hid.data(), hid.size());
	            inv.items.push_back(item);
	        }

	        auto msg = makeMessage("inv", inv.serialize());
	        network_->send(peerId, msg);
	    }

	    void sendUpdateBundleInventory(const std::string& peerId) {
	        if (!network_) return;

	        const size_t limit = config_.dev ? 256 : 128;
	        std::vector<crypto::Hash256> bundleIds;
	        {
	            std::lock_guard<std::mutex> lock(invMtx_);
	            bundleIds.reserve(updateManifestsById_.size());
	            for (const auto& [_, manifest] : updateManifestsById_) {
	                bundleIds.push_back(manifest.bundleId);
	            }
	        }
	        if (bundleIds.empty()) return;

	        std::sort(bundleIds.begin(), bundleIds.end(), [](const crypto::Hash256& a, const crypto::Hash256& b) {
	            return hashLess(a, b);
	        });
	        if (bundleIds.size() > limit) {
	            bundleIds.erase(bundleIds.begin(), bundleIds.end() - static_cast<std::ptrdiff_t>(limit));
	        }

	        synapse::InvMessage inv;
	        inv.items.reserve(bundleIds.size());
	        for (const auto& bundleId : bundleIds) {
	            synapse::InvItem item;
	            item.type = synapse::InvType::UPDATE_BUNDLE;
	            std::memcpy(item.hash.data(), bundleId.data(), bundleId.size());
	            inv.items.push_back(item);
	        }

	        if (inv.items.empty()) return;
	        auto msg = makeMessage("inv", inv.serialize());
	        network_->send(peerId, msg);
	    }

	    void sendPoeGetInv(const std::string& peerId, PoeInvKind kind, const crypto::Hash256& after, uint32_t limit) {
	        if (!network_ || !poeV1_) return;
	        if (limit == 0) limit = 1;
	        if (limit > 2048) limit = 2048;
	        std::vector<uint8_t> payload;
	        payload.reserve(1 + crypto::SHA256_SIZE + 4);
	        payload.push_back(static_cast<uint8_t>(kind));
	        payload.insert(payload.end(), after.begin(), after.end());
	        writeU32LE(payload, limit);
	        auto msg = makeMessage("poe_getinv", payload);
	        network_->send(peerId, msg);
	    }

	    void startPoeSync(const std::string& peerId) {
	        if (!network_ || !poeV1_) return;

	        uint32_t limitEntries = config_.dev ? 512 : 256;
	        uint32_t limitVotes = config_.dev ? 1024 : 512;
	        uint64_t now = std::time(nullptr);

	        bool doEntries = false;
	        bool doVotes = false;
	        {
	            std::lock_guard<std::mutex> lock(poeSyncMtx_);
	            auto& st = poeSync_[peerId];
	            if (!st.entries.active) {
	                st.entries.active = true;
	                st.entries.inFlight = true;
	                st.entries.done = false;
	                st.entries.after = crypto::Hash256{};
	                st.entries.limit = limitEntries;
	                st.entries.lastRequestAt = now;
	                doEntries = true;
	            }
	            if (!st.votes.active) {
	                st.votes.active = true;
	                st.votes.inFlight = true;
	                st.votes.done = false;
	                st.votes.after = crypto::Hash256{};
	                st.votes.limit = limitVotes;
	                st.votes.lastRequestAt = now;
	                doVotes = true;
	            }
	        }

	        if (doEntries) sendPoeGetInv(peerId, PoeInvKind::ENTRY, crypto::Hash256{}, limitEntries);
	        if (doVotes) sendPoeGetInv(peerId, PoeInvKind::VOTE, crypto::Hash256{}, limitVotes);
	    }

	    std::vector<crypto::Hash256> selectPoeIdsPage(PoeInvKind kind, const crypto::Hash256& after, uint32_t limit) {
	        if (!poeV1_) return {};

	        std::vector<crypto::Hash256> all;
	        if (kind == PoeInvKind::ENTRY) {
	            all = poeV1_->listEntryIds(0);
	        } else if (kind == PoeInvKind::VOTE) {
	            all = poeV1_->listVoteIds(0);
	        } else if (kind == PoeInvKind::EPOCH) {
	            auto epochs = poeV1_->listEpochIds(0);
	            all.reserve(epochs.size());
	            for (uint64_t eid : epochs) {
	                all.push_back(poeEpochInvHash(eid));
	            }
	        } else {
	            return {};
	        }

	        auto it = std::upper_bound(all.begin(), all.end(), after, [](const crypto::Hash256& v, const crypto::Hash256& e) {
	            return hashLess(v, e);
	        });

	        std::vector<crypto::Hash256> page;
	        page.reserve(std::min<size_t>(static_cast<size_t>(limit), all.size()));
	        for (; it != all.end() && page.size() < limit; ++it) {
	            page.push_back(*it);
	        }
	        return page;
	    }

	    void handlePoeGetInvMessage(const std::string& peerId, const network::Message& msg) {
	        if (!network_ || !poeV1_) return;
	        if (msg.payload.size() < 1 + crypto::SHA256_SIZE + 4) return;

	        PoeInvKind kind = static_cast<PoeInvKind>(msg.payload[0]);
	        crypto::Hash256 after{};
	        std::memcpy(after.data(), msg.payload.data() + 1, after.size());
	        uint32_t limit = readU32LE(msg.payload, 1 + crypto::SHA256_SIZE);
	        if (limit == 0) limit = 1;
	        if (limit > 2048) limit = 2048;

	        auto page = selectPoeIdsPage(kind, after, limit);

	        std::vector<uint8_t> payload;
	        payload.reserve(1 + crypto::SHA256_SIZE + 4 + page.size() * crypto::SHA256_SIZE);
	        payload.push_back(static_cast<uint8_t>(kind));
	        payload.insert(payload.end(), after.begin(), after.end());
	        writeU32LE(payload, static_cast<uint32_t>(page.size()));
	        for (const auto& h : page) {
	            payload.insert(payload.end(), h.begin(), h.end());
	        }

	        auto reply = makeMessage("poe_inv", payload);
	        network_->send(peerId, reply);
	    }

#include "node/main_parts/net_message_handlers.inc"
    
    void handlePeerConnected(const network::Peer& peer) {
        const bool torRequired = agentTorRequired_.load();
        if (torRequired) {
            const bool torReachable = probeTorSocks();
            agentTorReachable_.store(torReachable);
            refreshTorWebReadiness(torReachable, false);

            core::TorRoutePolicyInput routeIn;
            routeIn.torRequired = torRequired;
            routeIn.torReachable = torReachable;
            routeIn.allowClearnetFallback = agentAllowClearnetFallback_.load();
            routeIn.allowP2PFallback = agentAllowP2PFallback_.load();
            const auto route = core::evaluateTorRoutePolicy(routeIn);
            agentTorDegraded_.store(route.torDegraded);
            updateAndLogTorReadinessState(torRequired, torReachable, agentTorWebReady_.load(), route.torDegraded);

            if (!route.allowP2PDiscovery) {
                if (network_) network_->disconnect(peer.id);
                utils::Logger::warn("Rejected peer in Tor-required fail-closed mode: " + peer.id);
                return;
            }

            if (!peer.isOutbound) {
                const bool loopback = (peer.address == "127.0.0.1") || (peer.address.rfind("127.", 0) == 0);
                if (!loopback) {
                    if (network_) network_->disconnect(peer.id);
                    utils::Logger::warn("Rejected non-loopback inbound peer in Tor-required mode: " + peer.id);
                    return;
                }
            }
        }

        utils::Logger::info("Peer connected: " + peer.id);
        sendVersion(peer.id);
        
        if (discovery_ && isDiscoveryEligiblePeerAddress(peer.address)) {
            discovery_->markPeerSuccess(peer.address);
        }
    }
    
	    void handlePeerDisconnected(const network::Peer& peer) {
	        utils::Logger::info("Peer disconnected: " + peer.id);
	        peerHeights_.erase(peer.id);
            {
                std::lock_guard<std::mutex> lock(peerHelloMtx_);
                peerHelloById_.erase(peer.id);
            }
	        {
	            std::lock_guard<std::mutex> lock(poeSyncMtx_);
	            poeSync_.erase(peer.id);
	        }
	        
	        if (discovery_ && isDiscoveryEligiblePeerAddress(peer.address)) {
	            discovery_->markPeerFailed(peer.address);
	        }
	        
	        uint64_t maxHeight = 0;
	        for (const auto& [id, height] : peerHeights_) {
	            if (height > maxHeight) maxHeight = height;
	        }
	        networkHeight_ = maxHeight;
	    }

    uint64_t getMemoryUsage() const {
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            return usage.ru_maxrss * 1024;
        }
        return 0;
    }
    
    uint64_t getDiskUsage() const {
        uint64_t total = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(config_.dataDir)) {
            if (entry.is_regular_file()) {
                total += entry.file_size();
            }
        }
        return total;
    }
    
    std::atomic<bool> running_;
    bool offlineMode_ = false;
    uint64_t startTime_;
    double syncProgress_;
    NodeConfig config_;
    std::string address_;
    std::string rpcCookieFilePath_;
    std::string rpcCookieToken_;
    std::string rpcBasicAuthToken_;
    mutable std::mutex poeSelfValidatorBootstrapMtx_;
    core::PoeSelfValidatorBootstrapPolicyState poeSelfValidatorBootstrapState_{};
    bool poeSelfValidatorBootstrapConfiguredAllow_ = false;
    bool poeSelfValidatorBootstrapEffectiveAllow_ = false;
    uint64_t poeSelfValidatorBootstrapLastEvaluatedAt_ = 0;
    std::string poeSelfValidatorBootstrapMode_ = "disabled";
    std::string poeSelfValidatorBootstrapStatusReason_ = "bootstrap_not_initialized";
    
    std::unique_ptr<database::Database> db_;
    std::unique_ptr<crypto::Keys> keys_;
    std::unique_ptr<network::Network> network_;
    std::unique_ptr<network::Discovery> discovery_;
    std::unique_ptr<core::Ledger> ledger_;
    std::unique_ptr<core::KnowledgeNetwork> knowledge_;
    std::unique_ptr<core::TransferManager> transfer_;
	    std::unique_ptr<core::Consensus> consensus_;
	    std::unique_ptr<core::PoeV1Engine> poeV1_;
	    std::unique_ptr<model::ModelLoader> modelLoader_;
	    std::unique_ptr<model::ModelAccess> modelAccess_;
        std::unique_ptr<model::ModelMarketplace> modelMarketplace_;
	    std::mutex modelMtx_;
	    std::atomic<uint64_t> modelRequests_{0};
		    std::unique_ptr<privacy::Privacy> privacy_;
		    std::unique_ptr<quantum::QuantumManager> quantumManager_;
		    std::unique_ptr<web::RpcServer> rpc_;
		    std::unique_ptr<web::WebSearch> webSearch_;
		    std::unique_ptr<web::QueryDetector> webDetector_;
		    std::unique_ptr<web::HtmlExtractor> webExtractor_;
		    std::unique_ptr<web::AIWrapper> webAi_;
		    std::mutex webMtx_;
            std::vector<std::string> naanWebConfigUnknownKeySamples_{};
            mutable std::mutex torBridgeProviderMetaMtx_;
            json torBridgeProviderMeta_ = json::object();
            std::atomic<uint64_t> torBridgeProviderMetaUpdatedAt_{0};
	    
	    std::unordered_map<std::string, uint64_t> peerHeights_;
        mutable std::mutex peerHelloMtx_;
        std::unordered_map<std::string, core::PeerHelloInfo> peerHelloById_;
        std::string torSessionDisplayId_;
    std::unordered_set<std::string> knownTxs_;
    std::unordered_set<std::string> knownKnowledge_;
	    std::unordered_set<std::string> knownBlocks_;
		    std::unordered_set<std::string> knownPoeEntries_;
		    std::unordered_set<std::string> knownPoeVotes_;
		    std::unordered_set<std::string> knownPoeEpochs_;
		    std::unordered_set<std::string> knownUpdateBundles_;
            std::unordered_map<std::string, uint64_t> recentPoeVotes_;
            std::unordered_map<std::string, uint64_t> invGossipSeenAt_;
            std::atomic<uint64_t> invBackpressureDrops_{0};
            std::atomic<uint64_t> getDataBackpressureDrops_{0};
            std::atomic<uint64_t> gossipSuppressed_{0};
            std::atomic<uint64_t> gossipSubsetRouted_{0};
		    std::unordered_map<std::string, uint64_t> knowledgeByHash_;
		    std::unordered_map<std::string, core::UpdateManifest> updateManifestsById_;
		    std::mutex updateApprovalMtx_;
		    std::unordered_map<std::string, std::vector<core::DetachedSignerApproval>> updateDetachedApprovalsByBundle_;
		    std::mutex securityPolicyMtx_;
		    std::string naanPolicyHash_{};
		    std::string implantPolicyHash_{};
		    std::atomic<uint64_t> naanSecurityEvents_{0};
		    std::atomic<uint64_t> naanSecurityHighSeverityEvents_{0};
		    std::atomic<uint64_t> naanSecurityLastEventAt_{0};
		    core::CoordinationHub agentCoordination_{};
		    std::atomic<bool> naanRuntimeInitialized_{false};
		    std::atomic<uint64_t> naanRuntimeStartedAt_{0};
		    core::AgentRuntimeSupervisor naanRuntimeSupervisor_{};
		    std::string naanRuntimeCrashStatePath_{};
		    std::atomic<uint64_t> naanRecoverySkips_{0};
		    core::AgentRuntimeSandbox agentRuntimeSandbox_{};
		    core::AgentAdaptiveScheduler agentAdaptiveScheduler_{};
		    core::AgentTaskScheduler naanTaskScheduler_{};
		    std::string naanSchedulerStatePath_{};
		    std::vector<core::ToolSchemaRule> naanToolSchemas_{};
		    core::AgentDraftQueue agentDraftQueue_{};
		    core::AgentSubmissionPipeline agentSubmissionPipeline_{};
		    core::AgentScoreTracker agentScore_{};
		    core::AgentStorageAuditLog naanAuditLog_{};
		    std::string naanStorageRootPath_{};
		    std::atomic<uint64_t> naanStorageRecoveredLines_{0};
		    std::atomic<uint64_t> naanStorageDroppedSegments_{0};
		    std::atomic<uint64_t> naanIndexRecoveryRuns_{0};
		    std::atomic<uint64_t> naanIndexRecoveryLastAt_{0};
		    std::atomic<uint64_t> naanConsistencyChecks_{0};
		    std::atomic<uint64_t> naanConsistencyRepairs_{0};
		    std::atomic<uint64_t> naanConsistencyLastAt_{0};
		    std::string naanScoreStatePath_{};
		    std::string naanScoreDecayStatePath_{};
		    std::atomic<uint64_t> naanScoreLastDecayTs_{0};
		    std::atomic<uint64_t> naanScoreLastViolationTick_{0};
		    std::atomic<uint8_t> naanLastScoreBand_{0};
		    std::atomic<uint64_t> naanScoreBandTransitions_{0};
		    std::atomic<uint64_t> naanQuarantineRecoveryTransitions_{0};
		    std::atomic<uint64_t> naanQuarantineRecoveryLastAt_{0};
		    std::atomic<uint32_t> naanScoreDecayIntervalSeconds_{60};
		    std::atomic<uint32_t> naanAbuseSpamPenalty_{1};
		    std::atomic<uint32_t> naanAbuseCitationPenalty_{1};
		    std::atomic<uint32_t> naanAbusePolicyPenalty_{2};
		    core::AgentIdentity attachedAgentIdentity_{};
		    std::atomic<uint64_t> naanTickCount_{0};
		    std::atomic<uint64_t> naanTaskResearchRuns_{0};
		    std::atomic<uint64_t> naanTaskVerifyRuns_{0};
		    std::atomic<uint64_t> naanTaskReviewRuns_{0};
		    std::atomic<uint64_t> naanTaskDraftRuns_{0};
		    std::atomic<uint64_t> naanTaskSubmitRuns_{0};
		    std::atomic<uint64_t> naanPipelineRuns_{0};
		    std::atomic<uint64_t> naanPipelineApproved_{0};
		    std::atomic<uint64_t> naanPipelineSubmitted_{0};
		    std::atomic<uint64_t> naanPipelineRejected_{0};
		    std::atomic<uint64_t> naanLastPipelineTs_{0};
		    std::mutex naanPipelineMtx_;
		    std::atomic<uint64_t> naanLastActionTs_{0};
		    std::atomic<uint64_t> naanLastResearchTs_{0};
		    std::atomic<uint64_t> naanLastVerifyTs_{0};
		    std::atomic<uint64_t> naanLastReviewTs_{0};
		    std::atomic<uint64_t> naanLastHeartbeatTs_{0};
		    std::atomic<uint64_t> naanLastDraftTs_{0};
		    web::ConnectorAbuseGuard naanConnectorAbuseGuard_{};
		    std::atomic<uint64_t> naanConnectorAbuseEvents_{0};
		    std::atomic<uint64_t> naanConnectorAbuseLastAt_{0};
            std::atomic<uint64_t> naanConnectorAbuseLastPolicyDelta_{0};
            std::atomic<uint64_t> naanConnectorAbuseLastFailureDelta_{0};
            std::atomic<uint32_t> naanConnectorAbuseLastViolations_{0};
            struct NaanWebResearchSnapshot {
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
            std::mutex naanWebResearchMtx_;
            NaanWebResearchSnapshot naanWebResearchSnapshot_{};
            std::atomic<bool> agentTorRequired_{true};
            std::atomic<bool> agentTorReachable_{false};
            std::atomic<bool> agentTorWebReady_{false};
            std::atomic<bool> agentTorWebProbeInFlight_{false};
            std::atomic<bool> agentTorManaged_{false};
            std::atomic<bool> agentTorManagedRestartInFlight_{false};
            std::atomic<bool> agentTorDegraded_{false};
            std::atomic<uint32_t> agentTorBootstrapPercent_{0};
            std::atomic<bool> agentTorOnionReady_{false};
            std::atomic<int64_t> managedTorPid_{0};
            std::atomic<uint64_t> agentTorManagedRestartLastAttemptAt_{0};
            std::atomic<uint64_t> agentTorManagedRestartNextAllowedAt_{0};
            std::atomic<uint32_t> agentTorManagedRestartConsecutiveFailures_{0};
            std::atomic<uint64_t> agentTorManagedRestartBackoffSkips_{0};
            std::atomic<int> agentTorWebProbeExitCode_{-1};
            std::atomic<uint64_t> agentTorWebProbeLastAt_{0};
            std::atomic<uint64_t> agentTorWebProbeLastOkAt_{0};
            std::atomic<uint32_t> agentTorWebProbeConsecutiveFailures_{0};
            std::atomic<uint32_t> agentTorWebProbeConsecutiveSuccesses_{0};
            std::atomic<uint64_t> agentTorWebProbeUrlRotation_{0};
            std::atomic<uint64_t> agentTorBridgeSubsetPersistCount_{0};
            std::atomic<uint64_t> agentTorBridgeSubsetLastPersistAt_{0};
            std::atomic<uint64_t> agentTorBridgeSubsetLastEpoch_{0};
            std::atomic<uint32_t> agentTorBridgeSubsetLastCount_{0};
            std::atomic<uint64_t> agentTorBridgeRemoteLastFetchAt_{0};
            std::atomic<uint64_t> agentTorBridgeRemoteFetchAttempts_{0};
            std::atomic<uint64_t> agentTorBridgeRemoteFetchSuccesses_{0};
            std::atomic<uint64_t> agentTorBridgeRemoteRateLimitedSkips_{0};
            mutable std::mutex agentTorWebProbeMtx_;
            std::string agentTorWebProbeLastError_;
            mutable std::mutex agentTorReadinessMtx_;
            std::string agentTorReadinessState_;
            std::string agentTorBootstrapReasonCode_;
		    std::atomic<bool> agentAllowClearnetFallback_{false};
		    std::atomic<bool> agentAllowP2PFallback_{false};
            std::atomic<bool> miningActive_{false};
            std::atomic<uint64_t> miningHashAttemptsTotal_{0};
            std::atomic<uint64_t> miningHashAttemptsLast_{0};
            std::atomic<uint64_t> miningLastSolvedAt_{0};
            std::atomic<uint32_t> miningWorkTargetBits_{0};
            std::atomic<uint64_t> miningFailClosedSkips_{0};
            std::atomic<uint64_t> naanWebFailClosedSkips_{0};
            std::atomic<uint64_t> naanWebConfigSanitizedWrites_{0};
            std::atomic<uint64_t> naanWebConfigTotalLines_{0};
            std::atomic<uint64_t> naanWebConfigAppliedLines_{0};
            std::atomic<uint64_t> naanWebConfigInvalidLines_{0};
            std::atomic<uint64_t> naanWebConfigUnknownKeys_{0};
            crypto::Hash256 miningCandidateHash_{};
            mutable std::mutex miningStateMtx_;
            std::atomic<uint64_t> naanRedactionCount_{0};
            std::deque<NaanUiEvent> naanUiEvents_{};
            mutable std::mutex naanUiEventsMtx_;
		    core::UpdateInstaller updateInstaller_;
		    core::ImplantSafetyPipeline implantSafetyPipeline_;
		    core::ImplantCompatibilityPolicy implantCompatibilityPolicy_{};
		    core::ImplantUpdateGovernancePolicy implantUpdatePolicy_{};
		    std::mutex implantPolicyMtx_;
		    std::mutex implantSafetyMtx_;
		    std::mutex updateInstallMtx_;
		    std::mutex invMtx_;
		    std::mutex poeSyncMtx_;
		    std::unordered_map<std::string, PoePeerSyncState> poeSync_;
	    std::unordered_map<uint64_t, uint64_t> requestedBlocks_;
	    std::mutex syncMtx_;
    std::atomic<uint64_t> networkHeight_{0};
    std::atomic<bool> syncing_{false};
    std::atomic<bool> suppressCallbacks_{false};
    std::atomic<uint64_t> autoPoeEpochLastRunAt_{0};
    std::atomic<uint64_t> autoPoeEpochLastFinalizedCount_{0};
    std::atomic<uint64_t> autoPoeLastRewardEpochId_{0};
    std::atomic<uint64_t> autoPoeLastRewardMineAtoms_{0};
    std::atomic<uint64_t> autoPoeLastRewardEntries_{0};
    std::atomic<uint64_t> autoPoeLastRewardAt_{0};

        std::mutex remoteMtx_;
        std::condition_variable remoteCv_;
        std::unordered_map<std::string, RemoteOfferCache> remoteOffers_;
        std::unordered_map<std::string, RemoteSessionInfo> remoteSessions_;
        std::unordered_map<std::string, RemotePending> remotePending_;
        std::unordered_map<std::string, synapse::RemoteModelRentOkMessage> remoteRentOkByOffer_;

        std::mutex remoteProvMtx_;
        std::string localOfferId_;
        uint64_t remotePricePerRequestAtoms_ = 0;
        std::unordered_map<std::string, ProviderSession> providerSessions_;
    
    std::thread networkThread_;
    std::thread consensusThread_;
    std::thread maintenanceThread_;
    std::thread syncThread_;
};

std::string formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return oss.str();
}

std::string formatUptime(uint64_t seconds) {
    uint64_t days = seconds / 86400;
    uint64_t hours = (seconds % 86400) / 3600;
    uint64_t mins = (seconds % 3600) / 60;
    uint64_t secs = seconds % 60;
    
    std::ostringstream oss;
    if (days > 0) oss << days << "d ";
    if (hours > 0 || days > 0) oss << hours << "h ";
    if (mins > 0 || hours > 0 || days > 0) oss << mins << "m ";
    oss << secs << "s";
    return oss.str();
}

// ---------------------------------------------------------------------------
// Factory / lifecycle helpers (called from main.cpp via synapse_net.h)
// ---------------------------------------------------------------------------
void SynapseNetDeleter::operator()(SynapseNet* p) const { delete p; }

SynapseNetPtr createSynapseNet() {
    return SynapseNetPtr(new SynapseNet());
}

bool initializeSynapseNet(SynapseNet& node, const NodeConfig& config) {
    return node.initialize(config);
}

int runSynapseNetCommand(SynapseNet& node, const std::vector<std::string>& args) {
    return node.runCommand(args);
}

int runSynapseNet(SynapseNet& node) {
    return node.run();
}

void shutdownSynapseNet(SynapseNet& node) {
    node.shutdown();
}

}
