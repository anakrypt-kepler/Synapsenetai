#include "node/tor_control_runtime.h"

#include "core/tor_bridge_provider.h"
#include "core/tor_bridge_utils.h"
#include "rpc/rpc_node_views.h"
#include "utils/config.h"
#include "web/curl_fetch.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace synapse::node {

namespace {

using json = nlohmann::json;

void ensureDefaultNaanWebConfig(const std::string& path, const std::string& ahmiaOnion) {
    if (std::filesystem::exists(path)) {
        return;
    }

    std::ofstream naanCfg(path);
    if (!naanCfg.is_open()) {
        return;
    }

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

std::string trimAscii(std::string s) {
    while (!s.empty() &&
           (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    if (start > 0) {
        s.erase(0, start);
    }
    return s;
}

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

void validateReloadInputs(const ReloadNaanWebConfigInputs& inputs) {
    if (!inputs.webSearch || !inputs.webExtractor || !inputs.webConfigTotalLines ||
        !inputs.webConfigAppliedLines || !inputs.webConfigInvalidLines ||
        !inputs.webConfigUnknownKeys || !inputs.webConfigUnknownKeySamples ||
        !inputs.webConfigSanitizedWrites || !inputs.agentTorBridgeRemoteLastFetchAt ||
        !inputs.agentTorBridgeRemoteFetchAttempts ||
        !inputs.agentTorBridgeRemoteFetchSuccesses ||
        !inputs.agentTorBridgeRemoteRateLimitedSkips || !inputs.configuredTorRuntimeMode ||
        !inputs.configuredTorSocksHost || !inputs.configuredTorSocksPort ||
        !inputs.configuredTorControlPort || !inputs.agentTorRequired ||
        !inputs.agentTorReachable || !inputs.refreshTorRoutePolicy ||
        !inputs.setTorBridgeProviderMetaSnapshotJson) {
        throw std::runtime_error("invalid_tor_control_reload_inputs");
    }
}

void validateTorControlCallbacks(const TorControlActionCallbacks& callbacks) {
    if (!callbacks.collectTorControlStatus || !callbacks.reloadNaanWebConfig ||
        !callbacks.refreshTorRoutePolicy || !callbacks.stopManagedTorRuntimeIfOwned ||
        !callbacks.resetManagedTorRestartBackoffState || !callbacks.startManagedTorRuntime ||
        !callbacks.configuredTorRuntimeMode ||
        !callbacks.persistLastKnownGoodBridgeSubset || !callbacks.saveConfig ||
        callbacks.configPath.empty()) {
        throw std::runtime_error("invalid_tor_control_callbacks");
    }
}

uint16_t validatePortOrDefault(const json& params, const char* key, uint16_t fallback) {
    if (!params.contains(key)) return fallback;
    int64_t raw = 0;
    try {
        raw = params.at(key).is_string() ? std::stoll(params.at(key).get<std::string>())
                                         : params.at(key).get<int64_t>();
    } catch (...) {
        throw std::runtime_error(std::string("invalid ") + key);
    }
    if (raw < 1 || raw > 65535) {
        throw std::runtime_error(std::string("invalid ") + key);
    }
    return static_cast<uint16_t>(raw);
}

void writeRuntimeTorConfig(const std::string& mode,
                           const std::string& socksHost,
                           uint16_t socksPort,
                           uint16_t controlPort) {
    auto& cfg = utils::Config::instance();
    cfg.set("agent.tor.mode", mode);
    cfg.set("tor.socks.host", socksHost);
    cfg.set("tor.socks.port", static_cast<int>(socksPort));
    cfg.set("tor.control.port", static_cast<int>(controlPort));
    cfg.set("agent.tor.socks_host", socksHost);
    cfg.set("agent.tor.socks_port", static_cast<int>(socksPort));
    cfg.set("agent.routing.allow_clearnet_fallback", false);
    cfg.set("agent.routing.allow_p2p_clearnet_fallback", false);
}

struct InFlightReset {
    std::atomic<bool>& flag;
    ~InFlightReset() { flag.store(false); }
};

} // namespace

std::string reloadNaanWebConfigJson(ReloadNaanWebConfigInputs& inputs) {
    validateReloadInputs(inputs);

    json meta;
    web::SearchConfig cfg;
    const std::string webCfgPath = inputs.dataDir + "/web_search.conf";
    (void)web::loadSearchConfig(webCfgPath, cfg);

    const std::string ahmiaOnion =
        "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/";
    const std::string naanWebCfgPath = inputs.dataDir + "/naan_agent_web.conf";
    ensureDefaultNaanWebConfig(naanWebCfgPath, ahmiaOnion);

    web::SearchConfigValidationStats validation{};
    (void)web::loadSearchConfigOverlay(naanWebCfgPath, cfg, &validation);
    web::sanitizeSearchConfig(cfg);
    cfg.tor.bridgeManualLines =
        core::sanitizeAndDedupeObfs4BridgeLines(cfg.tor.bridgeManualLines);

    const std::string bridgeFilePath = inputs.dataDir + "/tor/bridges.obfs4.txt";
    const std::string bridgeCachePath = inputs.dataDir + "/tor/bridge_pool_cache.json";
    const uint64_t bridgeNow = static_cast<uint64_t>(std::time(nullptr));
    auto& runtimeCfg = utils::Config::instance();
    const uint64_t bridgeCacheTtlSeconds = static_cast<uint64_t>(std::max<int64_t>(
        60, runtimeCfg.getInt64("agent.tor.bridge.cache_ttl_seconds", 86400)));
    const bool bridgeAllowStaleCacheFallback =
        runtimeCfg.getBool("agent.tor.bridge.allow_stale_cache_fallback", true);
    const bool bridgeRemoteAdapterEnabled =
        runtimeCfg.getBool("agent.tor.bridge.remote_adapter_enabled", true);
    const uint64_t bridgeRemoteMinIntervalSeconds = static_cast<uint64_t>(std::max<int64_t>(
        30, runtimeCfg.getInt64("agent.tor.bridge.remote_min_interval_seconds", 300)));
    const uint32_t bridgeRemoteTimeoutSeconds = static_cast<uint32_t>(std::max<int64_t>(
        5, std::min<int64_t>(120, runtimeCfg.getInt64("agent.tor.bridge.remote_timeout_seconds", 30))));
    const size_t bridgeRemoteMaxBytes = static_cast<size_t>(std::max<int64_t>(
        4096, std::min<int64_t>(4 * 1024 * 1024,
        runtimeCfg.getInt64("agent.tor.bridge.remote_max_bytes", 512 * 1024))));

    json bridgeProviderMeta;
    bridgeProviderMeta["transport"] = cfg.tor.bridgeTransport;
    bridgeProviderMeta["requestedSource"] = cfg.tor.bridgeSource;
    bridgeProviderMeta["filePath"] = bridgeFilePath;
    bridgeProviderMeta["cachePath"] = bridgeCachePath;
    bridgeProviderMeta["cacheTtlSeconds"] = bridgeCacheTtlSeconds;
    bridgeProviderMeta["allowStaleCacheFallback"] = bridgeAllowStaleCacheFallback;
    bridgeProviderMeta["remoteAdapterEnabled"] = bridgeRemoteAdapterEnabled;
    bridgeProviderMeta["remoteMinIntervalSeconds"] = bridgeRemoteMinIntervalSeconds;
    bridgeProviderMeta["remoteTimeoutSeconds"] = bridgeRemoteTimeoutSeconds;
    bridgeProviderMeta["remoteMaxBytes"] = bridgeRemoteMaxBytes;

    auto remoteBridgeFetch = [&](const std::string& url,
                                 std::vector<std::string>& lines,
                                 std::string& errorOut) -> bool {
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        const uint64_t lastFetchAt = inputs.agentTorBridgeRemoteLastFetchAt->load();
        if (lastFetchAt != 0 && now >= lastFetchAt &&
            (now - lastFetchAt) < bridgeRemoteMinIntervalSeconds) {
            const uint64_t retryAfter = bridgeRemoteMinIntervalSeconds - (now - lastFetchAt);
            inputs.agentTorBridgeRemoteRateLimitedSkips->fetch_add(1);
            errorOut = "rate_limited: retry_after_seconds=" + std::to_string(retryAfter);
            return false;
        }

        web::CurlFetchOptions opts;
        opts.timeoutSeconds = bridgeRemoteTimeoutSeconds;
        opts.maxBytes = bridgeRemoteMaxBytes;
        opts.userAgent = "SynapseNet/0.2 bridge-remote-fetch";
        opts.followRedirects = true;

        const bool torRequiredNow = inputs.agentTorRequired();
        const bool torReachableNow = inputs.agentTorReachable();
        if (torRequiredNow) {
            if (!torReachableNow) {
                errorOut = "tor_unreachable: required for remote bridge source";
                return false;
            }
            opts.socksProxyHostPort =
                inputs.configuredTorSocksHost() + ":" +
                std::to_string(inputs.configuredTorSocksPort());
        } else if (cfg.routeClearnetThroughTor && torReachableNow) {
            opts.socksProxyHostPort =
                inputs.configuredTorSocksHost() + ":" +
                std::to_string(inputs.configuredTorSocksPort());
        }

        inputs.agentTorBridgeRemoteFetchAttempts->fetch_add(1);
        inputs.agentTorBridgeRemoteLastFetchAt->store(now);
        const auto fetched = web::curlFetch(url, opts);
        if (fetched.exitCode != 0) {
            errorOut = "remote_fetch_failed: curl_exit=" + std::to_string(fetched.exitCode);
            if (!fetched.error.empty()) {
                errorOut += " " + fetched.error;
            }
            return false;
        }

        std::istringstream iss(fetched.body);
        std::string line;
        while (std::getline(iss, line)) {
            line = trimAscii(std::move(line));
            if (line.empty() || line[0] == '#') {
                continue;
            }
            lines.push_back(line);
        }
        if (!lines.empty()) {
            inputs.agentTorBridgeRemoteFetchSuccesses->fetch_add(1);
            return true;
        }

        const std::string lowerBody = toLowerAscii(fetched.body);
        const bool looksInteractive =
            lowerBody.find("captcha") != std::string::npos ||
            lowerBody.find("verify you are human") != std::string::npos ||
            lowerBody.find("javascript required") != std::string::npos ||
            lowerBody.find("challenge") != std::string::npos ||
            lowerBody.find("<html") != std::string::npos;
        if (looksInteractive) {
            errorOut =
                "manual_step_required: remote source requires interactive verification; "
                "fetch bridges manually and set agent.tor.bridge.manual_lines or use " +
                bridgeFilePath;
        } else {
            errorOut = "remote_fetch_failed: empty_bridge_material";
        }
        return false;
    };

    if (cfg.tor.bridgeTransport == "obfs4") {
        core::TorBridgeProviderResolveInput bridgeInput;
        bridgeInput.requestedSource = cfg.tor.bridgeSource;
        bridgeInput.transport = cfg.tor.bridgeTransport;
        bridgeInput.manualLines = cfg.tor.bridgeManualLines;
        bridgeInput.filePath = bridgeFilePath;
        bridgeInput.cachePath = bridgeCachePath;
        bridgeInput.remoteUrl = cfg.tor.bridgeRemoteUrl;
        bridgeInput.now = bridgeNow;
        bridgeInput.cacheTtlSeconds = bridgeCacheTtlSeconds;
        bridgeInput.minPoolSize = cfg.tor.bridgeMinPoolSize;
        bridgeInput.allowStaleCacheFallback = bridgeAllowStaleCacheFallback;

        const auto bridgeResolved = bridgeRemoteAdapterEnabled
            ? core::resolveObfs4BridgePool(bridgeInput, remoteBridgeFetch)
            : core::resolveObfs4BridgePool(bridgeInput);
        bridgeProviderMeta["ok"] = bridgeResolved.ok;
        bridgeProviderMeta["resolvedSource"] = bridgeResolved.source;
        bridgeProviderMeta["usedFallback"] = bridgeResolved.usedFallback;
        bridgeProviderMeta["cacheHit"] = bridgeResolved.cacheHit;
        bridgeProviderMeta["cacheFresh"] = bridgeResolved.cacheFresh;
        bridgeProviderMeta["cacheUpdated"] = bridgeResolved.cacheUpdated;
        bridgeProviderMeta["cacheSavedAt"] = bridgeResolved.cacheSavedAt;
        bridgeProviderMeta["reasonCode"] = bridgeResolved.reasonCode;
        bridgeProviderMeta["error"] = bridgeResolved.error;
        bridgeProviderMeta["triedSources"] = bridgeResolved.triedSources;
        bridgeProviderMeta["poolChecksum"] = bridgeResolved.checksum;
        bridgeProviderMeta["poolCount"] = bridgeResolved.lines.size();
        bridgeProviderMeta["remoteLastFetchAt"] =
            inputs.agentTorBridgeRemoteLastFetchAt->load();
        bridgeProviderMeta["remoteFetchAttempts"] =
            inputs.agentTorBridgeRemoteFetchAttempts->load();
        bridgeProviderMeta["remoteFetchSuccesses"] =
            inputs.agentTorBridgeRemoteFetchSuccesses->load();
        bridgeProviderMeta["remoteRateLimitedSkips"] =
            inputs.agentTorBridgeRemoteRateLimitedSkips->load();
        if (bridgeResolved.ok) {
            cfg.tor.bridgeManualLines = bridgeResolved.lines;
        }
    }

    cfg.tor.runtimeMode = inputs.configuredTorRuntimeMode();
    cfg.tor.socksHost = inputs.configuredTorSocksHost();
    cfg.tor.socksPort = inputs.configuredTorSocksPort();
    cfg.tor.controlHost = "127.0.0.1";
    cfg.tor.controlPort = inputs.configuredTorControlPort();

    inputs.webConfigTotalLines->store(validation.totalLines);
    inputs.webConfigAppliedLines->store(validation.appliedLines);
    inputs.webConfigInvalidLines->store(validation.invalidLines);
    inputs.webConfigUnknownKeys->store(validation.unknownKeys);
    *inputs.webConfigUnknownKeySamples = validation.unknownKeySamples;
    inputs.webConfigSanitizedWrites->fetch_add(1);

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
        cfg.connectorAuditDir = inputs.dataDir + "/audit/connectors";
    }

    if (inputs.persistSanitized && !web::saveSearchConfig(cfg, naanWebCfgPath)) {
        throw std::runtime_error("failed_to_save_naan_web_config");
    }

    inputs.webExtractor->setRemoveAds(cfg.removeAds);
    inputs.webExtractor->setRemoveScripts(cfg.removeScripts);
    inputs.webExtractor->setRemoveStyles(cfg.removeStyles);
    inputs.webExtractor->setRiskPolicy(cfg.extractionRisk);

    const bool torRequired = inputs.agentTorRequired();
    const auto route = inputs.refreshTorRoutePolicy(true);
    const bool torReachable = inputs.agentTorReachable();
    const bool hardTorOnly = torRequired || cfg.naanForceTorMode;
    const bool onionOptIn = utils::Config::instance().getBool("web.inject.onion", false);
    const bool torClearnetOptIn =
        utils::Config::instance().getBool("web.inject.tor_clearnet", false);
    if (hardTorOnly) {
        cfg.routeClearnetThroughTor = true;
        cfg.enableClearnet = torReachable && route.allowWebClearnet;
        cfg.enableDarknet = torReachable && route.allowWebOnion;
    } else {
        cfg.enableClearnet = true;
        cfg.enableDarknet = onionOptIn;
        cfg.routeClearnetThroughTor = torClearnetOptIn;
    }
    inputs.webSearch->init(cfg);

    json validationJson;
    validationJson["totalLines"] = validation.totalLines;
    validationJson["appliedLines"] = validation.appliedLines;
    validationJson["invalidLines"] = validation.invalidLines;
    validationJson["duplicateKeys"] = validation.duplicateKeys;
    validationJson["unknownKeys"] = validation.unknownKeys;
    validationJson["malformedBridgeLines"] = validation.malformedBridgeLines;

    meta["path"] = naanWebCfgPath;
    meta["persistSanitized"] = inputs.persistSanitized;
    meta["validation"] = validationJson;
    meta["bridgeTransport"] = cfg.tor.bridgeTransport;
    meta["bridgeSource"] = cfg.tor.bridgeSource;
    meta["bridgeManualLinesCount"] = cfg.tor.bridgeManualLines.size();
    meta["bridgeProvider"] = bridgeProviderMeta;
    meta["torRuntimeMode"] = cfg.tor.runtimeMode;
    meta["torSocksHost"] = cfg.tor.socksHost;
    meta["torSocksPort"] = cfg.tor.socksPort;
    meta["torControlPort"] = cfg.tor.controlPort;
    meta["webClearnetEnabled"] = cfg.enableClearnet;
    meta["webOnionEnabled"] = cfg.enableDarknet;

    inputs.setTorBridgeProviderMetaSnapshotJson(bridgeProviderMeta.dump());
    return meta.dump();
}

std::string handleTorControlAction(
    const json& params,
    const std::string& actionRaw,
    TorControlActionCallbacks& callbacks,
    std::atomic<bool>& restartInFlight) {
    validateTorControlCallbacks(callbacks);
    if (actionRaw.empty()) {
        throw std::runtime_error("action required");
    }

    const std::string action = rpc::normalizeRpcTorControlAction(actionRaw);

    if (action == "refresh_bridges") {
        const bool persistSanitized = params.value("persistSanitized", true);
        const auto route = callbacks.refreshTorRoutePolicy(true);
        rpc::RpcNodeTorControlResponseInputs response;
        response.action = action;
        response.tor = callbacks.collectTorControlStatus(route);
        response.includeBridgeRefresh = true;
        response.bridgeRefreshJson = callbacks.reloadNaanWebConfig(persistSanitized);
        response.includeLastKnownGoodBridgeSubsetPersisted = true;
        response.lastKnownGoodBridgeSubsetPersisted =
            callbacks.persistLastKnownGoodBridgeSubset("node.tor.control.refresh_bridges");
        return rpc::buildRpcNodeTorControlResponse(response);
    }

    if (action != "restart_managed_tor" && action != "switch_mode") {
        throw std::runtime_error("unknown action");
    }

    bool expected = false;
    if (!restartInFlight.compare_exchange_strong(expected, true)) {
        throw std::runtime_error("tor_restart_in_progress");
    }
    InFlightReset inFlightReset{restartInFlight};

    if (action == "switch_mode") {
        const std::string requestedMode = params.value("mode", "");
        if (requestedMode.empty()) {
            throw std::runtime_error("mode required");
        }
        const std::string mode = core::normalizeTorRuntimeMode(requestedMode);
        if (mode != "auto" && mode != "external" && mode != "managed") {
            throw std::runtime_error("invalid mode");
        }
        const bool persist = params.value("persist", true);
        const std::string socksHost = params.value("socksHost", std::string("127.0.0.1"));
        const uint16_t defaultSocksPort = (mode == "external") ? 9150 : 9050;
        const uint16_t defaultControlPort = (mode == "external") ? 9151 : 9051;
        const uint16_t socksPort = validatePortOrDefault(params, "socksPort", defaultSocksPort);
        const uint16_t controlPort =
            validatePortOrDefault(params, "controlPort", defaultControlPort);
        const bool reloadWeb = params.value("reloadWebConfig", true);

        writeRuntimeTorConfig(mode, socksHost, socksPort, controlPort);
        bool stoppedManaged = false;
        bool startedManaged = false;
        if (mode == "external") {
            stoppedManaged = callbacks.stopManagedTorRuntimeIfOwned(true);
            callbacks.resetManagedTorRestartBackoffState();
        } else if (mode == "managed") {
            callbacks.resetManagedTorRestartBackoffState();
            startedManaged = callbacks.startManagedTorRuntime();
        }

        rpc::RpcNodeTorControlResponseInputs response;
        response.action = action;
        response.includeModeSwitch = true;
        response.modeSwitch.requestedMode = requestedMode;
        response.modeSwitch.appliedMode = mode;
        response.modeSwitch.socksHost = socksHost;
        response.modeSwitch.socksPort = socksPort;
        response.modeSwitch.controlPort = controlPort;
        response.modeSwitch.persisted = persist;
        response.modeSwitch.stoppedManagedTor = stoppedManaged;
        response.modeSwitch.startedManagedTor = startedManaged;
        if (persist) {
            response.modeSwitch.configSavedSet = true;
            response.modeSwitch.configSaved = callbacks.saveConfig(callbacks.configPath);
            response.modeSwitch.configPath = callbacks.configPath;
        }
        if (reloadWeb) {
            response.includeBridgeRefresh = true;
            response.bridgeRefreshJson = callbacks.reloadNaanWebConfig(false);
        }

        const auto route = callbacks.refreshTorRoutePolicy(true);
        response.tor = callbacks.collectTorControlStatus(route);
        response.includeLastKnownGoodBridgeSubsetPersisted = true;
        response.lastKnownGoodBridgeSubsetPersisted =
            callbacks.persistLastKnownGoodBridgeSubset("node.tor.control.switch_mode");
        return rpc::buildRpcNodeTorControlResponse(response);
    }

    if (callbacks.configuredTorRuntimeMode() == "external") {
        throw std::runtime_error("tor_mode_external");
    }
    const bool reloadWeb = params.value("reloadWebConfig", true);
    const bool stoppedManaged = callbacks.stopManagedTorRuntimeIfOwned(true);
    callbacks.resetManagedTorRestartBackoffState();
    const bool startedManaged = callbacks.startManagedTorRuntime();
    rpc::RpcNodeTorControlResponseInputs response;
    response.action = action;
    response.includeManagedRestart = true;
    response.managedRestart.stoppedManagedTor = stoppedManaged;
    response.managedRestart.startedManagedTor = startedManaged;
    response.managedRestart.mode = callbacks.configuredTorRuntimeMode();
    if (reloadWeb) {
        response.includeBridgeRefresh = true;
        response.bridgeRefreshJson = callbacks.reloadNaanWebConfig(false);
    }
    const auto route = callbacks.refreshTorRoutePolicy(true);
    response.tor = callbacks.collectTorControlStatus(route);
    response.includeLastKnownGoodBridgeSubsetPersisted = true;
    response.lastKnownGoodBridgeSubsetPersisted =
        callbacks.persistLastKnownGoodBridgeSubset("node.tor.control.restart_managed_tor");
    return rpc::buildRpcNodeTorControlResponse(response);
}

} // namespace synapse::node
