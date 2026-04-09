#include "web/web.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static void testRoutePolicyAllowAndDenyRules() {
    synapse::web::SearchConfig cfg = synapse::web::defaultSearchConfig();
    cfg.fetchAllowlistRoutes = {"suffix:.onion", "prefix:https://docs.synapsenet.org/"};
    cfg.fetchDenylistRoutes = {"host:blocked.onion", "prefix:https://docs.synapsenet.org/private"};

    assert(synapse::web::isUrlAllowedByRoutePolicy("http://alpha.onion/index", cfg));
    assert(!synapse::web::isUrlAllowedByRoutePolicy("http://blocked.onion/index", cfg));
    assert(synapse::web::isUrlAllowedByRoutePolicy("https://docs.synapsenet.org/guide", cfg));
    assert(!synapse::web::isUrlAllowedByRoutePolicy("https://docs.synapsenet.org/private/spec", cfg));
    assert(!synapse::web::isUrlAllowedByRoutePolicy("https://unlisted.example/page", cfg));
}

static void testOnionRouterFiltersRoutesByPolicy() {
    synapse::web::SearchConfig cfg = synapse::web::defaultSearchConfig();
    cfg.darknetEngines.clear();
    cfg.customDarknetUrls = {
        "http://allowed.onion/search?q={query}",
        "http://blocked.onion/search?q={query}"
    };
    cfg.directOnionLinks = {
        "directallowed.onion",
        "directblocked.onion"
    };
    cfg.fetchAllowlistRoutes = {"suffix:.onion"};
    cfg.fetchDenylistRoutes = {"host:blocked.onion", "host:directblocked.onion"};

    synapse::web::QueryAnalysis analysis{};
    analysis.type = synapse::web::QueryType::DARKNET;

    synapse::web::DarknetEngines engines;
    synapse::web::OnionSearchRouter router;
    auto routes = router.route("test query", analysis, cfg, engines);

    bool hasAllowed = false;
    bool hasBlocked = false;
    bool hasDirectAllowed = false;
    bool hasDirectBlocked = false;
    for (const auto& route : routes) {
        const std::string n = synapse::web::normalizeUrl(route.url);
        if (n.find("allowed.onion") != std::string::npos) hasAllowed = true;
        if (n.find("blocked.onion") != std::string::npos) hasBlocked = true;
        if (n.find("directallowed.onion") != std::string::npos) hasDirectAllowed = true;
        if (n.find("directblocked.onion") != std::string::npos) hasDirectBlocked = true;
    }

    assert(hasAllowed);
    assert(hasDirectAllowed);
    assert(!hasBlocked);
    assert(!hasDirectBlocked);
}

static void testSearchConfigLoadSaveRouteRules() {
    synapse::web::SearchConfig cfg = synapse::web::defaultSearchConfig();
    cfg.fetchAllowlistRoutes = {"suffix:.onion", "host:docs.synapsenet.org"};
    cfg.fetchDenylistRoutes = {"host:blocked.onion", "prefix:http://127."};
    cfg.clearnetSiteAllowlist = {"nasa.gov"};
    cfg.onionSiteAllowlist = {"allowed.onion"};
    cfg.clearnetRouteBypassHosts = {"status.nasa.gov"};
    cfg.onionRouteBypassHosts = {"special.allowed.onion"};
    cfg.bypassOnionHttpsFallback = false;
    cfg.naanForceTorMode = true;
    cfg.naanAutoSearchEnabled = true;
    cfg.naanAutoSearchMode = "darknet";
    cfg.naanAutoSearchQueries = {"nasa propulsion updates", "distributed ai systems"};
    cfg.naanAutoSearchMaxResults = 6;

    const auto uniq = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path path = fs::temp_directory_path() / ("synapsenet_web_route_policy_" + uniq + ".conf");

    assert(synapse::web::saveSearchConfig(cfg, path.string()));

    synapse::web::SearchConfig loaded;
    assert(synapse::web::loadSearchConfig(path.string(), loaded));
    assert(loaded.fetchAllowlistRoutes == cfg.fetchAllowlistRoutes);
    assert(loaded.fetchDenylistRoutes == cfg.fetchDenylistRoutes);
    assert(loaded.clearnetSiteAllowlist == cfg.clearnetSiteAllowlist);
    assert(loaded.onionSiteAllowlist == cfg.onionSiteAllowlist);
    assert(loaded.clearnetRouteBypassHosts == cfg.clearnetRouteBypassHosts);
    assert(loaded.onionRouteBypassHosts == cfg.onionRouteBypassHosts);
    assert(loaded.bypassOnionHttpsFallback == cfg.bypassOnionHttpsFallback);
    assert(loaded.naanForceTorMode == cfg.naanForceTorMode);
    assert(loaded.naanAutoSearchEnabled == cfg.naanAutoSearchEnabled);
    assert(loaded.naanAutoSearchMode == cfg.naanAutoSearchMode);
    assert(loaded.naanAutoSearchQueries == cfg.naanAutoSearchQueries);
    assert(loaded.naanAutoSearchMaxResults == cfg.naanAutoSearchMaxResults);

    std::error_code ec;
    fs::remove(path, ec);
}

static void testSiteAllowAndDenyByNetwork() {
    synapse::web::SearchConfig cfg = synapse::web::defaultSearchConfig();
    cfg.fetchAllowlistRoutes.clear();
    cfg.fetchDenylistRoutes.clear();
    cfg.clearnetSiteAllowlist = {"nasa.gov"};
    cfg.clearnetSiteDenylist = {"blocked.nasa.gov"};
    cfg.onionSiteAllowlist = {"allowed.onion"};
    cfg.onionSiteDenylist = {"blocked.onion"};

    assert(synapse::web::isUrlAllowedByRoutePolicy("https://www.nasa.gov", cfg));
    assert(!synapse::web::isUrlAllowedByRoutePolicy("https://esa.int", cfg));
    assert(!synapse::web::isUrlAllowedByRoutePolicy("https://blocked.nasa.gov/secret", cfg));

    assert(synapse::web::isUrlAllowedByRoutePolicy("http://allowed.onion/index", cfg));
    assert(!synapse::web::isUrlAllowedByRoutePolicy("http://blocked.onion/index", cfg));
    assert(!synapse::web::isUrlAllowedByRoutePolicy("http://another.onion/index", cfg));
}

static void testBypassHostsOverrideRouteRules() {
    synapse::web::SearchConfig cfg = synapse::web::defaultSearchConfig();
    cfg.fetchAllowlistRoutes = {"suffix:.onion"};
    cfg.fetchDenylistRoutes = {"host:status.nasa.gov", "host:special.allowed.onion"};
    cfg.clearnetRouteBypassHosts = {"status.nasa.gov"};
    cfg.onionRouteBypassHosts = {"special.allowed.onion"};

    assert(synapse::web::isUrlAllowedByRoutePolicy("https://status.nasa.gov", cfg));
    assert(synapse::web::isUrlAllowedByRoutePolicy("http://special.allowed.onion/index", cfg));
    assert(!synapse::web::isUrlAllowedByRoutePolicy("https://other.nasa.gov", cfg));
}

static void testNaanDefaultsAndAhmiaOnionPath() {
    synapse::web::SearchConfig cfg = synapse::web::defaultSearchConfig();
    assert(cfg.clearnetEngines.size() == 1);
    assert(cfg.clearnetEngines[0] == synapse::web::SearchEngine::DUCKDUCKGO);
    assert(cfg.routeClearnetThroughTor);
    assert(cfg.naanForceTorMode);
    assert(cfg.naanAutoSearchEnabled);
    assert(cfg.naanAutoSearchMode == "both");
    assert(cfg.naanAutoSearchMaxResults == 4);

    bool hasAhmiaOnion = false;
    for (const auto& base : cfg.customDarknetUrls) {
        if (synapse::web::normalizeUrl(base) ==
            synapse::web::normalizeUrl("http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/")) {
            hasAhmiaOnion = true;
            break;
        }
    }
    assert(hasAhmiaOnion);

    synapse::web::DarknetEngines engines;
    auto list = engines.list();
    bool ahmiaEngineIsOnion = false;
    for (const auto& engine : list) {
        if (engine.engine != synapse::web::SearchEngine::AHMIA) continue;
        ahmiaEngineIsOnion = engine.onionOnly &&
                             synapse::web::normalizeUrl(engine.baseUrl) ==
                                 synapse::web::normalizeUrl("http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/");
        break;
    }
    assert(ahmiaEngineIsOnion);
}

static void testSearchConfigValidationAndSanitization() {
    const auto uniq = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path path = fs::temp_directory_path() / ("synapsenet_web_cfg_validation_" + uniq + ".conf");
    {
        std::ofstream out(path);
        assert(out.good());
        out << "clearnet_engines=duckduckgo,duckduckgo,ahmia,brave\n";
        out << "darknet_engines=ahmia,ahmia,torch,duckduckgo\n";
        out << "custom_darknet_urls=http://alpha.onion/,http://alpha.onion/,http://beta.onion/\n";
        out << "direct_onion_links=alpha.onion,http://alpha.onion/,beta.onion\n";
        out << "naan_force_tor_mode=1\n";
        out << "route_clearnet_through_tor=0\n";
        out << "naan_auto_search_mode=invalid\n";
        out << "naan_auto_search_max_results=400\n";
        out << "cf_bypass_enabled=1\n";
        out << "cf_js_solver_backend=flaresolverr\n";
        out << "cf_flaresolverr_url=http://127.0.0.1:8191/v1\n";
        out << "cf_retry_on_challenge=1\n";
        out << "cf_retry_max_attempts=5\n";
        out << "agent.tor.mode=managed\n";
        out << "agent.tor.mode=external\n";
        out << "agent.tor.socks_host=127.0.0.1\n";
        out << "agent.tor.socks_port=9150\n";
        out << "agent.tor.control.port=9151\n";
        out << "agent.tor.bootstrap_required=1\n";
        out << "agent.tor.bridge.transport=obfs4\n";
        out << "agent.tor.bridge.source=manual\n";
        out << "agent.tor.bridge.remote_url=https://bridges.example.net/obfs4.txt\n";
        out << "agent.tor.bridge.randomize_per_node=1\n";
        out << "agent.tor.bridge.min_pool_size=0\n";
        out << "agent.tor.bridge.manual_lines="
               "Bridge obfs4 45.95.232.5:443 3CE205F7D7DE61E6E1787024EEDC03DCCFCA3C6A cert=foo iat-mode=2,"
               "badbridge line,"
               "obfs4 185.255.92.205:9104 A77CEEAF241F7B41F7F087A5D89D15D7D098E918 cert=bar iat-mode=0,"
               "Bridge obfs4 45.95.232.5:443 3CE205F7D7DE61E6E1787024EEDC03DCCFCA3C6A cert=foo iat-mode=2\n";
        out << "agent.tor.process.allow_kill_managed_only=1\n";
        out << "agent.tor.process.allow_takeover_external=0\n";
        out << "timeout_seconds=not_a_number\n";
        out << "unknown_custom_key=1\n";
        out << "science.nasa.gov\n";
    }

    synapse::web::SearchConfig cfg;
    synapse::web::SearchConfigValidationStats validation;
    assert(synapse::web::loadSearchConfig(path.string(), cfg, &validation));
    assert(validation.totalLines >= 25);
    assert(validation.appliedLines >= 20);
    assert(validation.invalidLines >= 3);
    assert(validation.duplicateKeys >= 1);
    assert(validation.unknownKeys >= 1);
    assert(validation.malformedBridgeLines >= 1);
    assert(!validation.unknownKeySamples.empty());
    assert(!validation.duplicateKeySamples.empty());
    assert(!validation.malformedBridgeLineSamples.empty());

    assert(cfg.routeClearnetThroughTor);
    assert(cfg.naanForceTorMode);
    assert(cfg.naanAutoSearchMode == "both");
    assert(cfg.naanAutoSearchMaxResults == 32);
    assert(cfg.timeoutSeconds == synapse::web::defaultSearchConfig().timeoutSeconds);
    assert(cfg.tor.runtimeMode == "external");
    assert(cfg.tor.socksHost == "127.0.0.1");
    assert(cfg.tor.socksPort == 9150);
    assert(cfg.tor.controlPort == 9151);
    assert(cfg.tor.bootstrapRequired);
    assert(cfg.tor.bridgeTransport == "obfs4");
    assert(cfg.tor.bridgeSource == "manual");
    assert(cfg.tor.bridgeRemoteUrl == "https://bridges.example.net/obfs4.txt");
    assert(cfg.tor.bridgeRandomizePerNode);
    assert(cfg.tor.bridgeMinPoolSize == 1);
    assert(cfg.tor.processAllowKillManagedOnly);
    assert(!cfg.tor.processAllowTakeoverExternal);
    assert(cfg.tor.bridgeManualLines.size() == 2);

    assert(cfg.clearnetEngines.size() == 2);
    assert(cfg.clearnetEngines[0] == synapse::web::SearchEngine::DUCKDUCKGO);
    assert(cfg.clearnetEngines[1] == synapse::web::SearchEngine::BRAVE);
    assert(cfg.darknetEngines.size() == 2);
    assert(cfg.darknetEngines[0] == synapse::web::SearchEngine::AHMIA);
    assert(cfg.darknetEngines[1] == synapse::web::SearchEngine::TORCH);
    assert(cfg.cfBypassEnabled);
    assert(cfg.cfJsSolverBackend == "flaresolverr");
    assert(cfg.cfFlareSolverrUrl == "http://127.0.0.1:8191/v1");
    assert(cfg.cfRetryOnChallenge);
    assert(cfg.cfRetryMaxAttempts == 5);

    bool hasAlpha = false;
    bool hasBeta = false;
    bool hasDefaultAhmia = false;
    for (const auto& url : cfg.customDarknetUrls) {
        const auto normalized = synapse::web::normalizeUrl(url);
        if (normalized == synapse::web::normalizeUrl("http://alpha.onion/")) hasAlpha = true;
        if (normalized == synapse::web::normalizeUrl("http://beta.onion/")) hasBeta = true;
        if (normalized == synapse::web::normalizeUrl("http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/")) {
            hasDefaultAhmia = true;
        }
    }
    assert(hasAlpha);
    assert(hasBeta);
    assert(hasDefaultAhmia);

    const fs::path canonicalA = fs::temp_directory_path() / ("synapsenet_web_cfg_validation_a_" + uniq + ".conf");
    const fs::path canonicalB = fs::temp_directory_path() / ("synapsenet_web_cfg_validation_b_" + uniq + ".conf");
    assert(synapse::web::saveSearchConfig(cfg, canonicalA.string()));
    synapse::web::SearchConfig replay;
    assert(synapse::web::loadSearchConfig(canonicalA.string(), replay));
    assert(synapse::web::saveSearchConfig(replay, canonicalB.string()));
    assert(replay.routeClearnetThroughTor == cfg.routeClearnetThroughTor);
    assert(replay.naanForceTorMode == cfg.naanForceTorMode);
    assert(replay.naanAutoSearchMode == cfg.naanAutoSearchMode);
    assert(replay.naanAutoSearchMaxResults == cfg.naanAutoSearchMaxResults);
    assert(replay.clearnetEngines == cfg.clearnetEngines);
    assert(replay.darknetEngines == cfg.darknetEngines);
    assert(replay.customDarknetUrls == cfg.customDarknetUrls);
    assert(replay.directOnionLinks == cfg.directOnionLinks);
    assert(replay.cfBypassEnabled == cfg.cfBypassEnabled);
    assert(replay.cfJsSolverBackend == cfg.cfJsSolverBackend);
    assert(replay.cfFlareSolverrUrl == cfg.cfFlareSolverrUrl);
    assert(replay.cfRetryOnChallenge == cfg.cfRetryOnChallenge);
    assert(replay.cfRetryMaxAttempts == cfg.cfRetryMaxAttempts);
    assert(replay.tor.runtimeMode == cfg.tor.runtimeMode);
    assert(replay.tor.socksPort == cfg.tor.socksPort);
    assert(replay.tor.controlPort == cfg.tor.controlPort);
    assert(replay.tor.bootstrapRequired == cfg.tor.bootstrapRequired);
    assert(replay.tor.bridgeTransport == cfg.tor.bridgeTransport);
    assert(replay.tor.bridgeSource == cfg.tor.bridgeSource);
    assert(replay.tor.bridgeRemoteUrl == cfg.tor.bridgeRemoteUrl);
    assert(replay.tor.bridgeRandomizePerNode == cfg.tor.bridgeRandomizePerNode);
    assert(replay.tor.bridgeMinPoolSize == cfg.tor.bridgeMinPoolSize);
    assert(replay.tor.bridgeManualLines == cfg.tor.bridgeManualLines);
    assert(replay.tor.processAllowKillManagedOnly == cfg.tor.processAllowKillManagedOnly);
    assert(replay.tor.processAllowTakeoverExternal == cfg.tor.processAllowTakeoverExternal);

    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(canonicalA, ec);
    fs::remove(canonicalB, ec);
}

static void testDarknetSearchRequiresConfiguredTorSocks() {
    synapse::web::WebSearch ws;
    synapse::web::SearchConfig cfg = synapse::web::defaultSearchConfig();
    cfg.enableDarknet = true;
    cfg.darknetEngines = {synapse::web::SearchEngine::AHMIA};
    cfg.customDarknetUrls.clear();
    cfg.directOnionLinks.clear();
    cfg.tor.socksHost.clear();
    cfg.tor.socksPort = 0;
    ws.setConfig(cfg);

    std::string lastErr;
    ws.onSearchError([&](const std::string& err) { lastErr = err; });
    auto results = ws.searchDarknet("tor network");
    assert(results.empty());
    assert(lastErr == "darknet_search_missing_tor_socks_config");
}

int main() {
    testRoutePolicyAllowAndDenyRules();
    testOnionRouterFiltersRoutesByPolicy();
    testSearchConfigLoadSaveRouteRules();
    testSiteAllowAndDenyByNetwork();
    testBypassHostsOverrideRouteRules();
    testNaanDefaultsAndAhmiaOnionPath();
    testSearchConfigValidationAndSanitization();
    testDarknetSearchRequiresConfiguredTorSocks();
    return 0;
}
