#include "web/web.h"
#include "core/tor_bridge_utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_set>

namespace synapse {
namespace web {

static std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

static std::string trim(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

static std::vector<std::string> splitList(const std::string& value) {
    std::vector<std::string> items;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string trimmed = trim(item);
        if (!trimmed.empty()) items.push_back(trimmed);
    }
    return items;
}

static bool parseBool(const std::string& value) {
    std::string v = toLower(trim(value));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

static std::string engineToString(SearchEngine engine) {
    switch (engine) {
        case SearchEngine::GOOGLE: return "google";
        case SearchEngine::BING: return "bing";
        case SearchEngine::DUCKDUCKGO: return "duckduckgo";
        case SearchEngine::BRAVE: return "brave";
        case SearchEngine::AHMIA: return "ahmia";
        case SearchEngine::TORCH: return "torch";
        case SearchEngine::NOTEVIL: return "notevil";
        case SearchEngine::DARKSEARCH: return "darksearch";
        case SearchEngine::DEEPSEARCH: return "deepsearch";
        case SearchEngine::CUSTOM: return "custom";
    }
    return "custom";
}

static bool parseEngine(const std::string& value, SearchEngine& engine) {
    std::string v = toLower(trim(value));
    if (v == "google") { engine = SearchEngine::GOOGLE; return true; }
    if (v == "bing") { engine = SearchEngine::BING; return true; }
    if (v == "duckduckgo") { engine = SearchEngine::DUCKDUCKGO; return true; }
    if (v == "brave") { engine = SearchEngine::BRAVE; return true; }
    if (v == "ahmia") { engine = SearchEngine::AHMIA; return true; }
    if (v == "torch") { engine = SearchEngine::TORCH; return true; }
    if (v == "notevil") { engine = SearchEngine::NOTEVIL; return true; }
    if (v == "darksearch") { engine = SearchEngine::DARKSEARCH; return true; }
    if (v == "deepsearch") { engine = SearchEngine::DEEPSEARCH; return true; }
    if (v == "custom") { engine = SearchEngine::CUSTOM; return true; }
    return false;
}

static bool isClearnetEngine(SearchEngine engine) {
    switch (engine) {
        case SearchEngine::GOOGLE:
        case SearchEngine::BING:
        case SearchEngine::DUCKDUCKGO:
        case SearchEngine::BRAVE:
        case SearchEngine::CUSTOM:
            return true;
        default:
            return false;
    }
}

static bool isDarknetEngine(SearchEngine engine) {
    switch (engine) {
        case SearchEngine::AHMIA:
        case SearchEngine::TORCH:
        case SearchEngine::NOTEVIL:
        case SearchEngine::DARKSEARCH:
        case SearchEngine::DEEPSEARCH:
        case SearchEngine::CUSTOM:
            return true;
        default:
            return false;
    }
}

static void dedupeEngines(std::vector<SearchEngine>& engines) {
    std::unordered_set<int> seen;
    std::vector<SearchEngine> out;
    out.reserve(engines.size());
    for (auto engine : engines) {
        int key = static_cast<int>(engine);
        if (!seen.insert(key).second) continue;
        out.push_back(engine);
    }
    engines.swap(out);
}

template <typename NormalizeFn>
static void dedupeStrings(std::vector<std::string>& values, NormalizeFn normalizeFn) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> out;
    out.reserve(values.size());
    for (const auto& raw : values) {
        std::string value = trim(raw);
        if (value.empty()) continue;
        const std::string normalized = normalizeFn(value);
        if (normalized.empty()) continue;
        if (!seen.insert(normalized).second) continue;
        out.push_back(normalized);
    }
    values.swap(out);
}

static void pushValidationSample(std::vector<std::string>& samples, const std::string& value) {
    if (value.empty()) return;
    if (samples.size() >= 16) return;
    samples.push_back(value);
}

static std::string normalizeTorRuntimeMode(const std::string& value) {
    const std::string v = toLower(trim(value));
    if (v == "auto" || v == "external" || v == "managed") return v;
    return "auto";
}

static std::string normalizeBridgeTransport(const std::string& value) {
    const std::string v = toLower(trim(value));
    if (v == "obfs4" || v == "snowflake" || v == "none") return v;
    return "none";
}

static std::string normalizeBridgeSource(const std::string& value) {
    const std::string v = toLower(trim(value));
    if (v == "manual" || v == "file" || v == "cache" || v == "remote") return v;
    return "manual";
}

static std::string normalizeBridgeRemoteUrl(const std::string& value) {
    const std::string normalized = normalizeUrl(value);
    if (normalized.empty()) return "";
    if (normalized.rfind("http://", 0) != 0 && normalized.rfind("https://", 0) != 0) return "";
    return normalized;
}

static bool isRecognizedConfigKey(const std::string& key) {
    static const std::unordered_set<std::string> keys = {
        "clearnet_engines",
        "darknet_engines",
        "custom_clearnet_urls",
        "custom_darknet_urls",
        "direct_onion_links",
        "clearnet_site_allowlist",
        "clearnet_sites",
        "naan_clearnet_sites",
        "clearnet_site_denylist",
        "clearnet_block_sites",
        "naan_clearnet_block_sites",
        "onion_site_allowlist",
        "onion_sites",
        "naan_onion_sites",
        "onion_site_denylist",
        "onion_block_sites",
        "naan_onion_block_sites",
        "clearnet_route_bypass_hosts",
        "naan_clearnet_route_bypass_hosts",
        "onion_route_bypass_hosts",
        "naan_onion_route_bypass_hosts",
        "max_results_per_engine",
        "max_page_size",
        "timeout_seconds",
        "enable_clearnet",
        "enable_darknet",
        "enable_knowledge_network",
        "streaming_mode",
        "remove_ads",
        "remove_scripts",
        "remove_styles",
        "route_clearnet_through_tor",
        "naan_force_tor_mode",
        "naan_auto_search_enabled",
        "naan_auto_search_mode",
        "naan_auto_search_queries",
        "naan_auto_search_max_results",
        "bypass_onion_https_fallback",
        "naan_bypass_onion_https_fallback",
        "fetch_allowlist_routes",
        "fetch_denylist_routes",
        "connector_audit_enabled",
        "connector_audit_dir",
        "extraction_risk_enabled",
        "extraction_risk_block_threshold",
        "extraction_risk_prompt_penalty",
        "extraction_risk_entropy_penalty",
        "extraction_risk_malformed_penalty",
        "extraction_risk_entropy_threshold_millibits",
        "tor_socks_host",
        "tor_socks_port",
        "tor_control_host",
        "tor_control_port",
        "tor_control_password",
        "tor_use_new_circuit",
        "tor_circuit_timeout",
        "agent.tor.mode",
        "agent.tor.socks_host",
        "agent.tor.socks_port",
        "agent.tor.control.host",
        "agent.tor.control_host",
        "agent.tor.control.port",
        "agent.tor.control_port",
        "agent.tor.bootstrap_required",
        "agent.tor.bridge.transport",
        "agent.tor.bridge.source",
        "agent.tor.bridge.remote_url",
        "agent.tor.bridge.randomize_per_node",
        "agent.tor.bridge.min_pool_size",
        "agent.tor.bridge.manual_lines",
        "agent.tor.bridge.lines",
        "agent.tor.process.allow_kill_managed_only",
        "agent.tor.process.allow_takeover_external",
        "cf_bypass_enabled",
        "cf_user_agent_rotation",
        "cf_user_agents",
        "cf_tls_fingerprint_spoof",
        "cf_tls_fingerprint",
        "cf_http2_fingerprint",
        "cf_header_order_emulation",
        "cf_custom_headers",
        "cf_cookie_persistence",
        "cf_js_challenge_auto_solve",
        "cf_js_solver_backend",
        "cf_flaresolverr_url",
        "cf_turnstile_bypass",
        "cf_turnstile_solver_url",
        "cf_canvas_fingerprint_spoof",
        "cf_webgl_fingerprint_spoof",
        "cf_webdriver_hide",
        "cf_navigator_spoof",
        "cf_mouse_emulation",
        "cf_mouse_jitter_ms",
        "cf_scroll_emulation",
        "cf_timing_randomization",
        "cf_request_delay_min_ms",
        "cf_request_delay_max_ms",
        "cf_session_rotation",
        "cf_session_max_requests",
        "cf_proxy_rotation",
        "cf_proxy_type",
        "cf_proxy_list",
        "cf_proxy_rotate_after",
        "cf_origin_ip_resolve",
        "cf_origin_ip_hosts",
        "cf_cache_fallback",
        "cf_cache_provider",
        "cf_headless_browser_mode",
        "cf_headless_browser",
        "cf_stealth_plugins",
        "cf_ai_labyrinth_detect",
        "cf_ai_labyrinth_max_depth",
        "cf_waf_bypass",
        "cf_rate_limit_adaptive",
        "cf_rate_limit_window_sec",
        "cf_rate_limit_max_reqs",
        "cf_clearance_cookie_reuse",
        "cf_clearance_cookie_ttl",
        "cf_referer_spoof",
        "cf_accept_language_rotation",
        "cf_accept_languages",
        "cf_dns_over_https",
        "cf_doh_provider",
        "cf_retry_on_challenge",
        "cf_retry_max_attempts",
        "cf_retry_backoff_ms"
    };
    return keys.find(key) != keys.end();
}

static void applyConfigKeyValue(SearchConfig& cfg,
                                const std::string& keyRaw,
                                const std::string& valueRaw,
                                SearchConfigValidationStats* validation) {
    const std::string key = toLower(trim(keyRaw));
    const std::string value = trim(valueRaw);

    if (key == "clearnet_engines") {
        cfg.clearnetEngines.clear();
        for (const auto& item : splitList(value)) {
            SearchEngine engine;
            if (parseEngine(item, engine)) cfg.clearnetEngines.push_back(engine);
        }
        return;
    }
    if (key == "darknet_engines") {
        cfg.darknetEngines.clear();
        for (const auto& item : splitList(value)) {
            SearchEngine engine;
            if (parseEngine(item, engine)) cfg.darknetEngines.push_back(engine);
        }
        return;
    }
    if (key == "custom_clearnet_urls") {
        cfg.customClearnetUrls = splitList(value);
        return;
    }
    if (key == "custom_darknet_urls") {
        cfg.customDarknetUrls = splitList(value);
        return;
    }
    if (key == "direct_onion_links") {
        cfg.directOnionLinks = splitList(value);
        return;
    }
    if (key == "clearnet_site_allowlist" || key == "clearnet_sites" || key == "naan_clearnet_sites") {
        cfg.clearnetSiteAllowlist = splitList(value);
        return;
    }
    if (key == "clearnet_site_denylist" || key == "clearnet_block_sites" || key == "naan_clearnet_block_sites") {
        cfg.clearnetSiteDenylist = splitList(value);
        return;
    }
    if (key == "onion_site_allowlist" || key == "onion_sites" || key == "naan_onion_sites") {
        cfg.onionSiteAllowlist = splitList(value);
        return;
    }
    if (key == "onion_site_denylist" || key == "onion_block_sites" || key == "naan_onion_block_sites") {
        cfg.onionSiteDenylist = splitList(value);
        return;
    }
    if (key == "clearnet_route_bypass_hosts" || key == "naan_clearnet_route_bypass_hosts") {
        cfg.clearnetRouteBypassHosts = splitList(value);
        return;
    }
    if (key == "onion_route_bypass_hosts" || key == "naan_onion_route_bypass_hosts") {
        cfg.onionRouteBypassHosts = splitList(value);
        return;
    }
    if (key == "max_results_per_engine") {
        cfg.maxResultsPerEngine = static_cast<size_t>(std::stoul(value));
        return;
    }
    if (key == "max_page_size") {
        cfg.maxPageSize = static_cast<size_t>(std::stoul(value));
        return;
    }
    if (key == "timeout_seconds") {
        cfg.timeoutSeconds = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "enable_clearnet") {
        cfg.enableClearnet = parseBool(value);
        return;
    }
    if (key == "enable_darknet") {
        cfg.enableDarknet = parseBool(value);
        return;
    }
    if (key == "enable_knowledge_network") {
        cfg.enableKnowledgeNetwork = parseBool(value);
        return;
    }
    if (key == "streaming_mode") {
        cfg.streamingMode = parseBool(value);
        return;
    }
    if (key == "remove_ads") {
        cfg.removeAds = parseBool(value);
        return;
    }
    if (key == "remove_scripts") {
        cfg.removeScripts = parseBool(value);
        return;
    }
    if (key == "remove_styles") {
        cfg.removeStyles = parseBool(value);
        return;
    }
    if (key == "route_clearnet_through_tor") {
        const bool enabled = parseBool(value);
        cfg.routeClearnetThroughTor = enabled;
        cfg.tor.bootstrapRequired = enabled;
        return;
    }
    if (key == "naan_force_tor_mode") {
        const bool enabled = parseBool(value);
        cfg.naanForceTorMode = enabled;
        cfg.tor.bootstrapRequired = enabled;
        return;
    }
    if (key == "naan_auto_search_enabled") {
        cfg.naanAutoSearchEnabled = parseBool(value);
        return;
    }
    if (key == "naan_auto_search_mode") {
        std::string mode = toLower(value);
        if (mode != "clearnet" && mode != "darknet" && mode != "both" && mode != "auto") {
            mode = "both";
        }
        cfg.naanAutoSearchMode = mode;
        return;
    }
    if (key == "naan_auto_search_queries") {
        cfg.naanAutoSearchQueries = splitList(value);
        return;
    }
    if (key == "naan_auto_search_max_results") {
        cfg.naanAutoSearchMaxResults = static_cast<uint32_t>(std::stoul(value));
        if (cfg.naanAutoSearchMaxResults == 0) cfg.naanAutoSearchMaxResults = 1;
        if (cfg.naanAutoSearchMaxResults > 32) cfg.naanAutoSearchMaxResults = 32;
        return;
    }
    if (key == "bypass_onion_https_fallback" || key == "naan_bypass_onion_https_fallback") {
        cfg.bypassOnionHttpsFallback = parseBool(value);
        cfg.tor.bypassOnionHttpsFallback = cfg.bypassOnionHttpsFallback;
        return;
    }
    if (key == "fetch_allowlist_routes") {
        cfg.fetchAllowlistRoutes = splitList(value);
        return;
    }
    if (key == "fetch_denylist_routes") {
        cfg.fetchDenylistRoutes = splitList(value);
        return;
    }
    if (key == "connector_audit_enabled") {
        cfg.connectorAuditEnabled = parseBool(value);
        return;
    }
    if (key == "connector_audit_dir") {
        cfg.connectorAuditDir = value;
        return;
    }
    if (key == "extraction_risk_enabled") {
        cfg.extractionRisk.enabled = parseBool(value);
        return;
    }
    if (key == "extraction_risk_block_threshold") {
        cfg.extractionRisk.blockScoreThreshold = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "extraction_risk_prompt_penalty") {
        cfg.extractionRisk.promptInjectionPenalty = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "extraction_risk_entropy_penalty") {
        cfg.extractionRisk.entropyPenalty = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "extraction_risk_malformed_penalty") {
        cfg.extractionRisk.malformedHtmlPenalty = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "extraction_risk_entropy_threshold_millibits") {
        cfg.extractionRisk.entropyMilliBitsThreshold = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "tor_socks_host") {
        cfg.tor.socksHost = value;
        return;
    }
    if (key == "tor_socks_port") {
        cfg.tor.socksPort = static_cast<uint16_t>(std::stoul(value));
        return;
    }
    if (key == "tor_control_host") {
        cfg.tor.controlHost = value;
        return;
    }
    if (key == "tor_control_port") {
        cfg.tor.controlPort = static_cast<uint16_t>(std::stoul(value));
        return;
    }
    if (key == "tor_control_password") {
        cfg.tor.controlPassword = value;
        return;
    }
    if (key == "tor_use_new_circuit") {
        cfg.tor.useNewCircuit = parseBool(value);
        return;
    }
    if (key == "tor_circuit_timeout") {
        cfg.tor.circuitTimeout = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "agent.tor.mode") {
        cfg.tor.runtimeMode = normalizeTorRuntimeMode(value);
        return;
    }
    if (key == "agent.tor.socks_host") {
        cfg.tor.socksHost = value;
        return;
    }
    if (key == "agent.tor.socks_port") {
        cfg.tor.socksPort = static_cast<uint16_t>(std::stoul(value));
        return;
    }
    if (key == "agent.tor.control.host" || key == "agent.tor.control_host") {
        cfg.tor.controlHost = value;
        return;
    }
    if (key == "agent.tor.control.port" || key == "agent.tor.control_port") {
        cfg.tor.controlPort = static_cast<uint16_t>(std::stoul(value));
        return;
    }
    if (key == "agent.tor.bootstrap_required") {
        const bool enabled = parseBool(value);
        cfg.tor.bootstrapRequired = enabled;
        cfg.naanForceTorMode = enabled;
        if (enabled) {
            cfg.routeClearnetThroughTor = true;
        }
        return;
    }
    if (key == "agent.tor.bridge.transport") {
        cfg.tor.bridgeTransport = normalizeBridgeTransport(value);
        return;
    }
    if (key == "agent.tor.bridge.source") {
        cfg.tor.bridgeSource = normalizeBridgeSource(value);
        return;
    }
    if (key == "agent.tor.bridge.remote_url") {
        cfg.tor.bridgeRemoteUrl = value;
        return;
    }
    if (key == "agent.tor.bridge.randomize_per_node") {
        cfg.tor.bridgeRandomizePerNode = parseBool(value);
        return;
    }
    if (key == "agent.tor.bridge.min_pool_size") {
        cfg.tor.bridgeMinPoolSize = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "agent.tor.bridge.manual_lines" || key == "agent.tor.bridge.lines") {
        cfg.tor.bridgeManualLines.clear();
        for (const auto& rawLine : splitList(value)) {
            const auto parsed = synapse::core::normalizeObfs4BridgeLine(rawLine);
            if (parsed.status == synapse::core::TorBridgeLineParseStatus::Skip) {
                continue;
            }
            if (parsed.status == synapse::core::TorBridgeLineParseStatus::Invalid) {
                if (validation) {
                    validation->malformedBridgeLines += 1;
                    validation->invalidLines += 1;
                    pushValidationSample(validation->malformedBridgeLineSamples, trim(rawLine));
                }
                continue;
            }
            cfg.tor.bridgeManualLines.push_back(parsed.normalized);
        }
        return;
    }
    if (key == "agent.tor.process.allow_kill_managed_only") {
        cfg.tor.processAllowKillManagedOnly = parseBool(value);
        return;
    }
    if (key == "agent.tor.process.allow_takeover_external") {
        cfg.tor.processAllowTakeoverExternal = parseBool(value);
        return;
    }
    if (key == "cf_bypass_enabled") {
        cfg.cfBypassEnabled = parseBool(value);
        return;
    }
    if (key == "cf_user_agent_rotation") {
        cfg.cfUserAgentRotation = parseBool(value);
        return;
    }
    if (key == "cf_user_agents") {
        cfg.cfUserAgents = splitList(value);
        return;
    }
    if (key == "cf_tls_fingerprint_spoof") {
        cfg.cfTlsFingerprintSpoof = parseBool(value);
        return;
    }
    if (key == "cf_tls_fingerprint") {
        cfg.cfTlsFingerprint = value;
        return;
    }
    if (key == "cf_http2_fingerprint") {
        cfg.cfHttp2Fingerprint = parseBool(value);
        return;
    }
    if (key == "cf_header_order_emulation") {
        cfg.cfHeaderOrderEmulation = parseBool(value);
        return;
    }
    if (key == "cf_custom_headers") {
        cfg.cfCustomHeaders = splitList(value);
        return;
    }
    if (key == "cf_cookie_persistence") {
        cfg.cfCookiePersistence = parseBool(value);
        return;
    }
    if (key == "cf_js_challenge_auto_solve") {
        cfg.cfJsChallengeAutoSolve = parseBool(value);
        return;
    }
    if (key == "cf_js_solver_backend") {
        cfg.cfJsSolverBackend = value;
        return;
    }
    if (key == "cf_flaresolverr_url") {
        cfg.cfFlareSolverrUrl = value;
        return;
    }
    if (key == "cf_turnstile_bypass") {
        cfg.cfTurnstileBypass = parseBool(value);
        return;
    }
    if (key == "cf_turnstile_solver_url") {
        cfg.cfTurnstileSolverUrl = value;
        return;
    }
    if (key == "cf_canvas_fingerprint_spoof") {
        cfg.cfCanvasFingerprintSpoof = parseBool(value);
        return;
    }
    if (key == "cf_webgl_fingerprint_spoof") {
        cfg.cfWebglFingerprintSpoof = parseBool(value);
        return;
    }
    if (key == "cf_webdriver_hide") {
        cfg.cfWebdriverHide = parseBool(value);
        return;
    }
    if (key == "cf_navigator_spoof") {
        cfg.cfNavigatorSpoof = parseBool(value);
        return;
    }
    if (key == "cf_mouse_emulation") {
        cfg.cfMouseEmulation = parseBool(value);
        return;
    }
    if (key == "cf_mouse_jitter_ms") {
        cfg.cfMouseJitterMs = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_scroll_emulation") {
        cfg.cfScrollEmulation = parseBool(value);
        return;
    }
    if (key == "cf_timing_randomization") {
        cfg.cfTimingRandomization = parseBool(value);
        return;
    }
    if (key == "cf_request_delay_min_ms") {
        cfg.cfRequestDelayMinMs = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_request_delay_max_ms") {
        cfg.cfRequestDelayMaxMs = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_session_rotation") {
        cfg.cfSessionRotation = parseBool(value);
        return;
    }
    if (key == "cf_session_max_requests") {
        cfg.cfSessionMaxRequests = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_proxy_rotation") {
        cfg.cfProxyRotation = parseBool(value);
        return;
    }
    if (key == "cf_proxy_type") {
        cfg.cfProxyType = value;
        return;
    }
    if (key == "cf_proxy_list") {
        cfg.cfProxyList = splitList(value);
        return;
    }
    if (key == "cf_proxy_rotate_after") {
        cfg.cfProxyRotateAfter = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_origin_ip_resolve") {
        cfg.cfOriginIpResolve = parseBool(value);
        return;
    }
    if (key == "cf_origin_ip_hosts") {
        cfg.cfOriginIpHosts = splitList(value);
        return;
    }
    if (key == "cf_cache_fallback") {
        cfg.cfCacheFallback = parseBool(value);
        return;
    }
    if (key == "cf_cache_provider") {
        cfg.cfCacheProvider = value;
        return;
    }
    if (key == "cf_headless_browser_mode") {
        cfg.cfHeadlessBrowserMode = parseBool(value);
        return;
    }
    if (key == "cf_headless_browser") {
        cfg.cfHeadlessBrowser = value;
        return;
    }
    if (key == "cf_stealth_plugins") {
        cfg.cfStealthPlugins = parseBool(value);
        return;
    }
    if (key == "cf_ai_labyrinth_detect") {
        cfg.cfAiLabyrinthDetect = parseBool(value);
        return;
    }
    if (key == "cf_ai_labyrinth_max_depth") {
        cfg.cfAiLabyrinthMaxDepth = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_waf_bypass") {
        cfg.cfWafBypass = parseBool(value);
        return;
    }
    if (key == "cf_rate_limit_adaptive") {
        cfg.cfRateLimitAdaptive = parseBool(value);
        return;
    }
    if (key == "cf_rate_limit_window_sec") {
        cfg.cfRateLimitWindowSec = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_rate_limit_max_reqs") {
        cfg.cfRateLimitMaxReqs = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_clearance_cookie_reuse") {
        cfg.cfClearanceCookieReuse = parseBool(value);
        return;
    }
    if (key == "cf_clearance_cookie_ttl") {
        cfg.cfClearanceCookieTtl = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_referer_spoof") {
        cfg.cfRefererSpoof = parseBool(value);
        return;
    }
    if (key == "cf_accept_language_rotation") {
        cfg.cfAcceptLanguageRotation = parseBool(value);
        return;
    }
    if (key == "cf_accept_languages") {
        cfg.cfAcceptLanguages = splitList(value);
        return;
    }
    if (key == "cf_dns_over_https") {
        cfg.cfDnsOverHttps = parseBool(value);
        return;
    }
    if (key == "cf_doh_provider") {
        cfg.cfDohProvider = value;
        return;
    }
    if (key == "cf_retry_on_challenge") {
        cfg.cfRetryOnChallenge = parseBool(value);
        return;
    }
    if (key == "cf_retry_max_attempts") {
        cfg.cfRetryMaxAttempts = static_cast<uint32_t>(std::stoul(value));
        return;
    }
    if (key == "cf_retry_backoff_ms") {
        cfg.cfRetryBackoffMs = static_cast<uint32_t>(std::stoul(value));
        return;
    }
}

void sanitizeSearchConfig(SearchConfig& cfg) {
    cfg.maxResultsPerEngine = std::max<size_t>(1, std::min<size_t>(cfg.maxResultsPerEngine, 64));
    cfg.maxPageSize = std::max<size_t>(4096, std::min<size_t>(cfg.maxPageSize, 16ULL * 1024ULL * 1024ULL));
    cfg.timeoutSeconds = std::max<uint32_t>(1, std::min<uint32_t>(cfg.timeoutSeconds, 120));

    cfg.tor.runtimeMode = normalizeTorRuntimeMode(cfg.tor.runtimeMode);
    if (cfg.tor.socksHost.empty()) cfg.tor.socksHost = "127.0.0.1";
    if (cfg.tor.socksPort == 0) cfg.tor.socksPort = 9050;
    if (cfg.tor.controlHost.empty()) cfg.tor.controlHost = "127.0.0.1";
    if (cfg.tor.controlPort == 0) cfg.tor.controlPort = 9051;
    cfg.tor.bridgeTransport = normalizeBridgeTransport(cfg.tor.bridgeTransport);
    cfg.tor.bridgeSource = normalizeBridgeSource(cfg.tor.bridgeSource);
    cfg.tor.bridgeRemoteUrl = normalizeBridgeRemoteUrl(cfg.tor.bridgeRemoteUrl);
    cfg.tor.bridgeMinPoolSize = std::max<uint32_t>(1, std::min<uint32_t>(cfg.tor.bridgeMinPoolSize, 1024));
    cfg.tor.bridgeManualLines = synapse::core::sanitizeAndDedupeObfs4BridgeLines(cfg.tor.bridgeManualLines);
    if (cfg.tor.circuitTimeout == 0) cfg.tor.circuitTimeout = cfg.timeoutSeconds;

    if (cfg.naanAutoSearchMaxResults == 0) cfg.naanAutoSearchMaxResults = 1;
    if (cfg.naanAutoSearchMaxResults > 32) cfg.naanAutoSearchMaxResults = 32;
    cfg.naanAutoSearchMode = toLower(trim(cfg.naanAutoSearchMode));
    if (cfg.naanAutoSearchMode != "clearnet" &&
        cfg.naanAutoSearchMode != "darknet" &&
        cfg.naanAutoSearchMode != "both" &&
        cfg.naanAutoSearchMode != "auto") {
        cfg.naanAutoSearchMode = "both";
    }

    dedupeStrings(cfg.naanAutoSearchQueries, [](const std::string& value) {
        return trim(value);
    });
    if (cfg.naanAutoSearchQueries.empty()) {
        cfg.naanAutoSearchQueries = {
            "latest space engineering research",
            "latest ai research papers",
            "open source systems engineering best practices"
        };
    }

    std::vector<SearchEngine> cleanClearnet;
    for (auto engine : cfg.clearnetEngines) {
        if (isClearnetEngine(engine)) cleanClearnet.push_back(engine);
    }
    cfg.clearnetEngines.swap(cleanClearnet);
    dedupeEngines(cfg.clearnetEngines);
    if (cfg.clearnetEngines.empty()) {
        cfg.clearnetEngines = {SearchEngine::DUCKDUCKGO};
    }

    std::vector<SearchEngine> cleanDarknet;
    for (auto engine : cfg.darknetEngines) {
        if (isDarknetEngine(engine)) cleanDarknet.push_back(engine);
    }
    cfg.darknetEngines.swap(cleanDarknet);
    dedupeEngines(cfg.darknetEngines);
    if (cfg.darknetEngines.empty()) {
        cfg.darknetEngines = {SearchEngine::AHMIA, SearchEngine::TORCH, SearchEngine::DARKSEARCH, SearchEngine::DEEPSEARCH};
    }

    dedupeStrings(cfg.customClearnetUrls, [](const std::string& value) {
        const std::string normalized = normalizeUrl(value);
        if (normalized.empty()) return std::string{};
        if (normalized.rfind("http://", 0) != 0 && normalized.rfind("https://", 0) != 0) {
            return std::string{};
        }
        return normalized;
    });
    dedupeStrings(cfg.customDarknetUrls, [](const std::string& value) {
        const std::string normalized = normalizeUrl(value);
        if (normalized.empty()) return std::string{};
        if (!isOnionUrl(normalized)) return std::string{};
        return normalized;
    });
    dedupeStrings(cfg.directOnionLinks, [](const std::string& value) {
        std::string normalized = normalizeUrl(value);
        if (normalized.empty()) return std::string{};
        if (normalized.rfind("http://", 0) != 0 && normalized.rfind("https://", 0) != 0) {
            normalized = "http://" + normalized;
            normalized = normalizeUrl(normalized);
        }
        if (!isOnionUrl(normalized)) return std::string{};
        return normalized;
    });

    dedupeStrings(cfg.clearnetSiteAllowlist, [](const std::string& value) {
        return toLower(trim(value));
    });
    dedupeStrings(cfg.clearnetSiteDenylist, [](const std::string& value) {
        return toLower(trim(value));
    });
    dedupeStrings(cfg.onionSiteAllowlist, [](const std::string& value) {
        return toLower(trim(value));
    });
    dedupeStrings(cfg.onionSiteDenylist, [](const std::string& value) {
        return toLower(trim(value));
    });
    dedupeStrings(cfg.clearnetRouteBypassHosts, [](const std::string& value) {
        return toLower(trim(value));
    });
    dedupeStrings(cfg.onionRouteBypassHosts, [](const std::string& value) {
        return toLower(trim(value));
    });
    dedupeStrings(cfg.fetchAllowlistRoutes, [](const std::string& value) {
        return toLower(trim(value));
    });
    dedupeStrings(cfg.fetchDenylistRoutes, [](const std::string& value) {
        return toLower(trim(value));
    });

    cfg.bypassOnionHttpsFallback = cfg.bypassOnionHttpsFallback || cfg.tor.bypassOnionHttpsFallback;
    cfg.tor.bypassOnionHttpsFallback = cfg.bypassOnionHttpsFallback;
    if (cfg.naanForceTorMode) {
        cfg.routeClearnetThroughTor = true;
    }
    if (cfg.naanForceTorMode) {
        cfg.tor.bootstrapRequired = true;
    }

    const std::string ahmiaOnion = normalizeUrl("http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/");
    bool hasAhmia = false;
    for (const auto& base : cfg.customDarknetUrls) {
        if (normalizeUrl(base) == ahmiaOnion) {
            hasAhmia = true;
            break;
        }
    }
    if (!hasAhmia) {
        cfg.customDarknetUrls.push_back(ahmiaOnion);
    }
}

static bool loadConfigFromFile(const std::string& path,
                               SearchConfig& cfg,
                               SearchConfigValidationStats* validation) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::unordered_set<std::string> seenKeys;
    std::string line;
    while (std::getline(file, line)) {
        if (validation) validation->totalLines += 1;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            if (validation) validation->invalidLines += 1;
            continue;
        }
        const std::string key = trim(line.substr(0, pos));
        const std::string normalizedKey = toLower(key);
        const std::string value = trim(line.substr(pos + 1));
        if (normalizedKey.empty()) {
            if (validation) validation->invalidLines += 1;
            continue;
        }
        if (!isRecognizedConfigKey(normalizedKey)) {
            if (validation) {
                validation->unknownKeys += 1;
                pushValidationSample(validation->unknownKeySamples, normalizedKey);
            }
            continue;
        }
        if (!seenKeys.insert(normalizedKey).second) {
            if (validation) {
                validation->duplicateKeys += 1;
                pushValidationSample(validation->duplicateKeySamples, normalizedKey);
            }
        }
        try {
            applyConfigKeyValue(cfg, normalizedKey, value, validation);
            if (validation) validation->appliedLines += 1;
        } catch (...) {
            if (validation) validation->invalidLines += 1;
        }
    }

    sanitizeSearchConfig(cfg);
    return true;
}

SearchConfig defaultSearchConfig() {
    SearchConfig cfg;
    cfg.clearnetEngines = {SearchEngine::DUCKDUCKGO};
    cfg.darknetEngines = {SearchEngine::AHMIA, SearchEngine::TORCH, SearchEngine::DARKSEARCH, SearchEngine::DEEPSEARCH};
    cfg.customDarknetUrls = {"http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/"};
    cfg.clearnetSiteAllowlist.clear();
    cfg.clearnetSiteDenylist.clear();
    cfg.onionSiteAllowlist.clear();
    cfg.onionSiteDenylist.clear();
    cfg.clearnetRouteBypassHosts.clear();
    cfg.onionRouteBypassHosts.clear();
    cfg.maxResultsPerEngine = 10;
    cfg.maxPageSize = 1024 * 1024;
    cfg.timeoutSeconds = 10;
    cfg.enableClearnet = true;
    cfg.enableDarknet = true;
    cfg.enableKnowledgeNetwork = false;
    cfg.streamingMode = false;
    cfg.removeAds = true;
    cfg.removeScripts = true;
    cfg.removeStyles = true;
    cfg.routeClearnetThroughTor = true;
    cfg.bypassOnionHttpsFallback = true;
    cfg.naanForceTorMode = true;
    cfg.naanAutoSearchEnabled = true;
    cfg.naanAutoSearchMode = "both";
    cfg.naanAutoSearchQueries = {
        "latest space engineering research",
        "latest ai research papers",
        "open source systems engineering best practices"
    };
    cfg.naanAutoSearchMaxResults = 4;
    cfg.fetchAllowlistRoutes.clear();
    cfg.fetchDenylistRoutes.clear();
    cfg.connectorAuditEnabled = true;
    cfg.connectorAuditDir.clear();
    cfg.tor.runtimeMode = "auto";
    cfg.tor.bootstrapRequired = true;
    cfg.tor.socksHost = "127.0.0.1";
    cfg.tor.socksPort = 9050;
    cfg.tor.controlHost = "127.0.0.1";
    cfg.tor.controlPort = 9051;
    cfg.tor.controlPassword = "";
    cfg.tor.bridgeTransport = "none";
    cfg.tor.bridgeSource = "manual";
    cfg.tor.bridgeRemoteUrl.clear();
    cfg.tor.bridgeRandomizePerNode = true;
    cfg.tor.bridgeMinPoolSize = 2;
    cfg.tor.bridgeManualLines.clear();
    cfg.tor.processAllowKillManagedOnly = true;
    cfg.tor.processAllowTakeoverExternal = false;
    cfg.tor.useNewCircuit = false;
    cfg.tor.bypassOnionHttpsFallback = cfg.bypassOnionHttpsFallback;
    cfg.tor.circuitTimeout = cfg.timeoutSeconds;
    cfg.cfBypassEnabled = false;
    cfg.cfUserAgentRotation = true;
    cfg.cfUserAgents = {
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_5) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Safari/605.1.15",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_5) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0"
    };
    cfg.cfTlsFingerprintSpoof = true;
    cfg.cfTlsFingerprint = "chrome";
    cfg.cfHttp2Fingerprint = true;
    cfg.cfHeaderOrderEmulation = true;
    cfg.cfCustomHeaders = {
        "Sec-CH-UA: \"Chromium\";v=\"131\", \"Google Chrome\";v=\"131\"",
        "Sec-CH-UA-Mobile: ?0",
        "Sec-CH-UA-Platform: \"Windows\"",
        "Sec-Fetch-Dest: document",
        "Sec-Fetch-Mode: navigate",
        "Sec-Fetch-Site: none",
        "Sec-Fetch-User: ?1",
        "Upgrade-Insecure-Requests: 1"
    };
    cfg.cfCookiePersistence = true;
    cfg.cfJsChallengeAutoSolve = false;
    cfg.cfJsSolverBackend = "none";
    cfg.cfFlareSolverrUrl = "";
    cfg.cfTurnstileBypass = false;
    cfg.cfTurnstileSolverUrl = "";
    cfg.cfCanvasFingerprintSpoof = true;
    cfg.cfWebglFingerprintSpoof = true;
    cfg.cfWebdriverHide = true;
    cfg.cfNavigatorSpoof = true;
    cfg.cfMouseEmulation = true;
    cfg.cfMouseJitterMs = 50;
    cfg.cfScrollEmulation = true;
    cfg.cfTimingRandomization = true;
    cfg.cfRequestDelayMinMs = 800;
    cfg.cfRequestDelayMaxMs = 3000;
    cfg.cfSessionRotation = true;
    cfg.cfSessionMaxRequests = 25;
    cfg.cfProxyRotation = false;
    cfg.cfProxyType = "residential";
    cfg.cfProxyList.clear();
    cfg.cfProxyRotateAfter = 10;
    cfg.cfOriginIpResolve = false;
    cfg.cfOriginIpHosts.clear();
    cfg.cfCacheFallback = true;
    cfg.cfCacheProvider = "google";
    cfg.cfHeadlessBrowserMode = false;
    cfg.cfHeadlessBrowser = "camoufox";
    cfg.cfStealthPlugins = true;
    cfg.cfAiLabyrinthDetect = true;
    cfg.cfAiLabyrinthMaxDepth = 3;
    cfg.cfWafBypass = true;
    cfg.cfRateLimitAdaptive = true;
    cfg.cfRateLimitWindowSec = 60;
    cfg.cfRateLimitMaxReqs = 15;
    cfg.cfClearanceCookieReuse = true;
    cfg.cfClearanceCookieTtl = 900;
    cfg.cfRefererSpoof = true;
    cfg.cfAcceptLanguageRotation = true;
    cfg.cfAcceptLanguages = {
        "en-US,en;q=0.9",
        "en-GB,en;q=0.9",
        "de-DE,de;q=0.9,en;q=0.8",
        "fr-FR,fr;q=0.9,en;q=0.8",
        "es-ES,es;q=0.9,en;q=0.8",
        "ru-RU,ru;q=0.9,en;q=0.8",
        "ja-JP,ja;q=0.9,en;q=0.8",
        "zh-CN,zh;q=0.9,en;q=0.8"
    };
    cfg.cfDnsOverHttps = false;
    cfg.cfDohProvider = "cloudflare";
    cfg.cfRetryOnChallenge = true;
    cfg.cfRetryMaxAttempts = 3;
    cfg.cfRetryBackoffMs = 2000;
    return cfg;
}

bool loadSearchConfig(const std::string& path, SearchConfig& config) {
    return loadSearchConfig(path, config, nullptr);
}

bool loadSearchConfig(const std::string& path,
                      SearchConfig& config,
                      SearchConfigValidationStats* validation) {
    if (validation) *validation = SearchConfigValidationStats{};
    SearchConfig cfg = defaultSearchConfig();
    if (!loadConfigFromFile(path, cfg, validation)) {
        sanitizeSearchConfig(cfg);
        config = cfg;
        return false;
    }

    config = cfg;
    return true;
}

bool loadSearchConfigOverlay(const std::string& path, SearchConfig& config) {
    return loadSearchConfigOverlay(path, config, nullptr);
}

bool loadSearchConfigOverlay(const std::string& path,
                             SearchConfig& config,
                             SearchConfigValidationStats* validation) {
    if (validation) *validation = SearchConfigValidationStats{};
    const bool loaded = loadConfigFromFile(path, config, validation);
    sanitizeSearchConfig(config);
    return loaded;
}

bool saveSearchConfig(const SearchConfig& inputConfig, const std::string& path) {
    SearchConfig config = inputConfig;
    const bool preserveBypassOnionHttpsFallback = config.bypassOnionHttpsFallback;
    const bool preserveTorBypassOnionHttpsFallback = config.tor.bypassOnionHttpsFallback;
    sanitizeSearchConfig(config);
    config.bypassOnionHttpsFallback = preserveBypassOnionHttpsFallback;
    config.tor.bypassOnionHttpsFallback = preserveTorBypassOnionHttpsFallback;
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    auto writeList = [&](const std::vector<std::string>& items) {
        for (size_t i = 0; i < items.size(); i++) {
            if (i > 0) file << ",";
            file << items[i];
        }
    };
    
    auto writeEngines = [&](const std::vector<SearchEngine>& engines) {
        for (size_t i = 0; i < engines.size(); i++) {
            if (i > 0) file << ",";
            file << engineToString(engines[i]);
        }
    };
    
    file << "clearnet_engines=";
    writeEngines(config.clearnetEngines);
    file << "\n";
    
    file << "darknet_engines=";
    writeEngines(config.darknetEngines);
    file << "\n";
    
    file << "custom_clearnet_urls=";
    writeList(config.customClearnetUrls);
    file << "\n";
    
    file << "custom_darknet_urls=";
    writeList(config.customDarknetUrls);
    file << "\n";
    
    file << "direct_onion_links=";
    writeList(config.directOnionLinks);
    file << "\n";
    file << "clearnet_site_allowlist=";
    writeList(config.clearnetSiteAllowlist);
    file << "\n";
    file << "clearnet_site_denylist=";
    writeList(config.clearnetSiteDenylist);
    file << "\n";
    file << "onion_site_allowlist=";
    writeList(config.onionSiteAllowlist);
    file << "\n";
    file << "onion_site_denylist=";
    writeList(config.onionSiteDenylist);
    file << "\n";
    file << "clearnet_route_bypass_hosts=";
    writeList(config.clearnetRouteBypassHosts);
    file << "\n";
    file << "onion_route_bypass_hosts=";
    writeList(config.onionRouteBypassHosts);
    file << "\n";
    
    file << "max_results_per_engine=" << config.maxResultsPerEngine << "\n";
    file << "max_page_size=" << config.maxPageSize << "\n";
    file << "timeout_seconds=" << config.timeoutSeconds << "\n";
    file << "enable_clearnet=" << (config.enableClearnet ? "1" : "0") << "\n";
    file << "enable_darknet=" << (config.enableDarknet ? "1" : "0") << "\n";
    file << "enable_knowledge_network=" << (config.enableKnowledgeNetwork ? "1" : "0") << "\n";
    file << "streaming_mode=" << (config.streamingMode ? "1" : "0") << "\n";
    file << "remove_ads=" << (config.removeAds ? "1" : "0") << "\n";
    file << "remove_scripts=" << (config.removeScripts ? "1" : "0") << "\n";
    file << "remove_styles=" << (config.removeStyles ? "1" : "0") << "\n";
    file << "route_clearnet_through_tor=" << (config.routeClearnetThroughTor ? "1" : "0") << "\n";
    file << "naan_force_tor_mode=" << (config.naanForceTorMode ? "1" : "0") << "\n";
    file << "naan_auto_search_enabled=" << (config.naanAutoSearchEnabled ? "1" : "0") << "\n";
    file << "naan_auto_search_mode=" << config.naanAutoSearchMode << "\n";
    file << "naan_auto_search_queries=";
    writeList(config.naanAutoSearchQueries);
    file << "\n";
    file << "naan_auto_search_max_results=" << config.naanAutoSearchMaxResults << "\n";
    file << "bypass_onion_https_fallback=" << (config.bypassOnionHttpsFallback ? "1" : "0") << "\n";
    file << "fetch_allowlist_routes=";
    writeList(config.fetchAllowlistRoutes);
    file << "\n";
    file << "fetch_denylist_routes=";
    writeList(config.fetchDenylistRoutes);
    file << "\n";
    file << "connector_audit_enabled=" << (config.connectorAuditEnabled ? "1" : "0") << "\n";
    file << "connector_audit_dir=" << config.connectorAuditDir << "\n";
    file << "extraction_risk_enabled=" << (config.extractionRisk.enabled ? "1" : "0") << "\n";
    file << "extraction_risk_block_threshold=" << config.extractionRisk.blockScoreThreshold << "\n";
    file << "extraction_risk_prompt_penalty=" << config.extractionRisk.promptInjectionPenalty << "\n";
    file << "extraction_risk_entropy_penalty=" << config.extractionRisk.entropyPenalty << "\n";
    file << "extraction_risk_malformed_penalty=" << config.extractionRisk.malformedHtmlPenalty << "\n";
    file << "extraction_risk_entropy_threshold_millibits=" << config.extractionRisk.entropyMilliBitsThreshold << "\n";
    file << "agent.tor.mode=" << config.tor.runtimeMode << "\n";
    file << "agent.tor.socks_host=" << config.tor.socksHost << "\n";
    file << "agent.tor.socks_port=" << config.tor.socksPort << "\n";
    file << "agent.tor.control.host=" << config.tor.controlHost << "\n";
    file << "agent.tor.control.port=" << config.tor.controlPort << "\n";
    file << "agent.tor.bootstrap_required=" << (config.tor.bootstrapRequired ? "1" : "0") << "\n";
    file << "agent.tor.bridge.transport=" << config.tor.bridgeTransport << "\n";
    file << "agent.tor.bridge.source=" << config.tor.bridgeSource << "\n";
    file << "agent.tor.bridge.remote_url=" << config.tor.bridgeRemoteUrl << "\n";
    file << "agent.tor.bridge.randomize_per_node=" << (config.tor.bridgeRandomizePerNode ? "1" : "0") << "\n";
    file << "agent.tor.bridge.min_pool_size=" << config.tor.bridgeMinPoolSize << "\n";
    file << "agent.tor.bridge.manual_lines=";
    writeList(config.tor.bridgeManualLines);
    file << "\n";
    file << "agent.tor.process.allow_kill_managed_only=" << (config.tor.processAllowKillManagedOnly ? "1" : "0") << "\n";
    file << "agent.tor.process.allow_takeover_external=" << (config.tor.processAllowTakeoverExternal ? "1" : "0") << "\n";
    file << "tor_socks_host=" << config.tor.socksHost << "\n";
    file << "tor_socks_port=" << config.tor.socksPort << "\n";
    file << "tor_control_host=" << config.tor.controlHost << "\n";
    file << "tor_control_port=" << config.tor.controlPort << "\n";
    file << "tor_control_password=" << config.tor.controlPassword << "\n";
    file << "tor_use_new_circuit=" << (config.tor.useNewCircuit ? "1" : "0") << "\n";
    file << "tor_circuit_timeout=" << config.tor.circuitTimeout << "\n";
    file << "cf_bypass_enabled=" << (config.cfBypassEnabled ? "1" : "0") << "\n";
    file << "cf_user_agent_rotation=" << (config.cfUserAgentRotation ? "1" : "0") << "\n";
    file << "cf_user_agents=";
    writeList(config.cfUserAgents);
    file << "\n";
    file << "cf_tls_fingerprint_spoof=" << (config.cfTlsFingerprintSpoof ? "1" : "0") << "\n";
    file << "cf_tls_fingerprint=" << config.cfTlsFingerprint << "\n";
    file << "cf_http2_fingerprint=" << (config.cfHttp2Fingerprint ? "1" : "0") << "\n";
    file << "cf_header_order_emulation=" << (config.cfHeaderOrderEmulation ? "1" : "0") << "\n";
    file << "cf_custom_headers=";
    writeList(config.cfCustomHeaders);
    file << "\n";
    file << "cf_cookie_persistence=" << (config.cfCookiePersistence ? "1" : "0") << "\n";
    file << "cf_js_challenge_auto_solve=" << (config.cfJsChallengeAutoSolve ? "1" : "0") << "\n";
    file << "cf_js_solver_backend=" << config.cfJsSolverBackend << "\n";
    file << "cf_flaresolverr_url=" << config.cfFlareSolverrUrl << "\n";
    file << "cf_turnstile_bypass=" << (config.cfTurnstileBypass ? "1" : "0") << "\n";
    file << "cf_turnstile_solver_url=" << config.cfTurnstileSolverUrl << "\n";
    file << "cf_canvas_fingerprint_spoof=" << (config.cfCanvasFingerprintSpoof ? "1" : "0") << "\n";
    file << "cf_webgl_fingerprint_spoof=" << (config.cfWebglFingerprintSpoof ? "1" : "0") << "\n";
    file << "cf_webdriver_hide=" << (config.cfWebdriverHide ? "1" : "0") << "\n";
    file << "cf_navigator_spoof=" << (config.cfNavigatorSpoof ? "1" : "0") << "\n";
    file << "cf_mouse_emulation=" << (config.cfMouseEmulation ? "1" : "0") << "\n";
    file << "cf_mouse_jitter_ms=" << config.cfMouseJitterMs << "\n";
    file << "cf_scroll_emulation=" << (config.cfScrollEmulation ? "1" : "0") << "\n";
    file << "cf_timing_randomization=" << (config.cfTimingRandomization ? "1" : "0") << "\n";
    file << "cf_request_delay_min_ms=" << config.cfRequestDelayMinMs << "\n";
    file << "cf_request_delay_max_ms=" << config.cfRequestDelayMaxMs << "\n";
    file << "cf_session_rotation=" << (config.cfSessionRotation ? "1" : "0") << "\n";
    file << "cf_session_max_requests=" << config.cfSessionMaxRequests << "\n";
    file << "cf_proxy_rotation=" << (config.cfProxyRotation ? "1" : "0") << "\n";
    file << "cf_proxy_type=" << config.cfProxyType << "\n";
    file << "cf_proxy_list=";
    writeList(config.cfProxyList);
    file << "\n";
    file << "cf_proxy_rotate_after=" << config.cfProxyRotateAfter << "\n";
    file << "cf_origin_ip_resolve=" << (config.cfOriginIpResolve ? "1" : "0") << "\n";
    file << "cf_origin_ip_hosts=";
    writeList(config.cfOriginIpHosts);
    file << "\n";
    file << "cf_cache_fallback=" << (config.cfCacheFallback ? "1" : "0") << "\n";
    file << "cf_cache_provider=" << config.cfCacheProvider << "\n";
    file << "cf_headless_browser_mode=" << (config.cfHeadlessBrowserMode ? "1" : "0") << "\n";
    file << "cf_headless_browser=" << config.cfHeadlessBrowser << "\n";
    file << "cf_stealth_plugins=" << (config.cfStealthPlugins ? "1" : "0") << "\n";
    file << "cf_ai_labyrinth_detect=" << (config.cfAiLabyrinthDetect ? "1" : "0") << "\n";
    file << "cf_ai_labyrinth_max_depth=" << config.cfAiLabyrinthMaxDepth << "\n";
    file << "cf_waf_bypass=" << (config.cfWafBypass ? "1" : "0") << "\n";
    file << "cf_rate_limit_adaptive=" << (config.cfRateLimitAdaptive ? "1" : "0") << "\n";
    file << "cf_rate_limit_window_sec=" << config.cfRateLimitWindowSec << "\n";
    file << "cf_rate_limit_max_reqs=" << config.cfRateLimitMaxReqs << "\n";
    file << "cf_clearance_cookie_reuse=" << (config.cfClearanceCookieReuse ? "1" : "0") << "\n";
    file << "cf_clearance_cookie_ttl=" << config.cfClearanceCookieTtl << "\n";
    file << "cf_referer_spoof=" << (config.cfRefererSpoof ? "1" : "0") << "\n";
    file << "cf_accept_language_rotation=" << (config.cfAcceptLanguageRotation ? "1" : "0") << "\n";
    file << "cf_accept_languages=";
    writeList(config.cfAcceptLanguages);
    file << "\n";
    file << "cf_dns_over_https=" << (config.cfDnsOverHttps ? "1" : "0") << "\n";
    file << "cf_doh_provider=" << config.cfDohProvider << "\n";
    file << "cf_retry_on_challenge=" << (config.cfRetryOnChallenge ? "1" : "0") << "\n";
    file << "cf_retry_max_attempts=" << config.cfRetryMaxAttempts << "\n";
    file << "cf_retry_backoff_ms=" << config.cfRetryBackoffMs << "\n";
    
    return true;
}

}
}
