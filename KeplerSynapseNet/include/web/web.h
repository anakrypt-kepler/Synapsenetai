#ifndef SYNAPSE_WEB_H
#define SYNAPSE_WEB_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <cstdint>

namespace synapse {
namespace web {

enum class SearchEngine {
    GOOGLE,
    BING,
    DUCKDUCKGO,
    BRAVE,
    AHMIA,
    TORCH,
    NOTEVIL,
    DARKSEARCH,
    DEEPSEARCH,
    CUSTOM
};

enum class QueryType {
    CLEARNET,
    DARKNET,
    BOTH,
    KNOWLEDGE_NETWORK,
    DIRECT_LINK
};

enum class ContentType {
    HTML,
    TEXT,
    JSON,
    CODE,
    UNKNOWN
};

struct SearchResult {
    std::string title;
    std::string url;
    std::string snippet;
    std::string domain;
    ContentType contentType;
    double relevanceScore;
    uint64_t timestamp;
    bool isOnion;
    std::vector<std::string> extractedLinks;
};

struct ExtractionRiskPolicy {
    bool enabled = true;
    uint32_t blockScoreThreshold = 60;
    uint32_t promptInjectionPenalty = 40;
    uint32_t entropyPenalty = 25;
    uint32_t malformedHtmlPenalty = 30;
    uint32_t entropyMilliBitsThreshold = 4600;
};

struct ExtractedContent {
    std::string title;
    std::string mainText;
    std::vector<std::string> codeBlocks;
    std::vector<std::string> onionLinks;
    std::vector<std::string> clearnetLinks;
    std::map<std::string, std::string> metadata;
    size_t originalSize = 0;
    size_t extractedSize = 0;
    bool truncated = false;
    uint32_t riskScore = 0;
    bool riskBlocked = false;
    std::vector<std::string> riskSignals;
};

struct TorConfig {
    std::string runtimeMode = "auto";
    std::string socksHost;
    uint16_t socksPort;
    std::string controlHost;
    uint16_t controlPort;
    std::string controlPassword;
    bool bootstrapRequired = true;
    std::string bridgeTransport = "none";
    std::string bridgeSource = "manual";
    std::string bridgeRemoteUrl;
    bool bridgeRandomizePerNode = true;
    uint32_t bridgeMinPoolSize = 2;
    std::vector<std::string> bridgeManualLines;
    bool processAllowKillManagedOnly = true;
    bool processAllowTakeoverExternal = false;
    bool useNewCircuit;
    bool bypassOnionHttpsFallback = true;
    uint32_t circuitTimeout;
};

struct SearchConfig {
    std::vector<SearchEngine> clearnetEngines;
    std::vector<SearchEngine> darknetEngines;
    std::vector<std::string> customClearnetUrls;
    std::vector<std::string> customDarknetUrls;
    std::vector<std::string> directOnionLinks;
    std::vector<std::string> clearnetSiteAllowlist;
    std::vector<std::string> clearnetSiteDenylist;
    std::vector<std::string> onionSiteAllowlist;
    std::vector<std::string> onionSiteDenylist;
    std::vector<std::string> clearnetRouteBypassHosts;
    std::vector<std::string> onionRouteBypassHosts;
    size_t maxResultsPerEngine;
    size_t maxPageSize;
    uint32_t timeoutSeconds;
    bool enableClearnet;
    bool enableDarknet;
    bool enableKnowledgeNetwork;
    bool streamingMode;
    bool removeAds;
    bool removeScripts;
    bool removeStyles;
    TorConfig tor;
    bool routeClearnetThroughTor;
    bool bypassOnionHttpsFallback;
    bool naanForceTorMode = true;
    bool naanAutoSearchEnabled = true;
    std::string naanAutoSearchMode = "both";
    std::vector<std::string> naanAutoSearchQueries;
    uint32_t naanAutoSearchMaxResults = 4;
    std::vector<std::string> fetchAllowlistRoutes;
    std::vector<std::string> fetchDenylistRoutes;
    bool connectorAuditEnabled;
    std::string connectorAuditDir;
    ExtractionRiskPolicy extractionRisk;

    bool cfBypassEnabled = false;
    bool cfUserAgentRotation = true;
    std::vector<std::string> cfUserAgents;
    bool cfTlsFingerprintSpoof = true;
    std::string cfTlsFingerprint = "chrome";
    bool cfHttp2Fingerprint = true;
    bool cfHeaderOrderEmulation = true;
    std::vector<std::string> cfCustomHeaders;
    bool cfCookiePersistence = true;
    bool cfJsChallengeAutoSolve = false;
    std::string cfJsSolverBackend = "none";
    std::string cfFlareSolverrUrl;
    bool cfTurnstileBypass = false;
    std::string cfTurnstileSolverUrl;
    bool cfCanvasFingerprintSpoof = true;
    bool cfWebglFingerprintSpoof = true;
    bool cfWebdriverHide = true;
    bool cfNavigatorSpoof = true;
    bool cfMouseEmulation = true;
    uint32_t cfMouseJitterMs = 50;
    bool cfScrollEmulation = true;
    bool cfTimingRandomization = true;
    uint32_t cfRequestDelayMinMs = 800;
    uint32_t cfRequestDelayMaxMs = 3000;
    bool cfSessionRotation = true;
    uint32_t cfSessionMaxRequests = 25;
    bool cfProxyRotation = false;
    std::string cfProxyType = "residential";
    std::vector<std::string> cfProxyList;
    uint32_t cfProxyRotateAfter = 10;
    bool cfOriginIpResolve = false;
    std::vector<std::string> cfOriginIpHosts;
    bool cfCacheFallback = true;
    std::string cfCacheProvider = "google";
    bool cfHeadlessBrowserMode = false;
    std::string cfHeadlessBrowser = "camoufox";
    bool cfStealthPlugins = true;
    bool cfAiLabyrinthDetect = true;
    uint32_t cfAiLabyrinthMaxDepth = 3;
    bool cfWafBypass = true;
    bool cfRateLimitAdaptive = true;
    uint32_t cfRateLimitWindowSec = 60;
    uint32_t cfRateLimitMaxReqs = 15;
    bool cfClearanceCookieReuse = true;
    uint32_t cfClearanceCookieTtl = 900;
    bool cfRefererSpoof = true;
    bool cfAcceptLanguageRotation = true;
    std::vector<std::string> cfAcceptLanguages;
    bool cfDnsOverHttps = false;
    std::string cfDohProvider = "cloudflare";
    bool cfRetryOnChallenge = true;
    uint32_t cfRetryMaxAttempts = 3;
    uint32_t cfRetryBackoffMs = 2000;
};

struct SearchConfigValidationStats {
    uint64_t totalLines = 0;
    uint64_t appliedLines = 0;
    uint64_t invalidLines = 0;
    uint64_t duplicateKeys = 0;
    uint64_t unknownKeys = 0;
    uint64_t malformedBridgeLines = 0;
    std::vector<std::string> unknownKeySamples;
    std::vector<std::string> duplicateKeySamples;
    std::vector<std::string> malformedBridgeLineSamples;
};

struct QueryAnalysis {
    QueryType type;
    std::vector<std::string> keywords;
    std::vector<std::string> darknetTriggers;
    std::vector<std::string> clearnetTriggers;
    double darknetConfidence;
    double clearnetConfidence;
    bool requiresBoth;
    std::string normalizedQuery;
};

struct WebStats {
    uint64_t totalSearches;
    uint64_t clearnetSearches;
    uint64_t darknetSearches;
    uint64_t successfulFetches;
    uint64_t failedFetches;
    uint64_t bytesDownloaded;
    uint64_t pagesExtracted;
    double avgResponseTime;
    uint64_t routePolicyChecks;
    uint64_t routePolicyPassed;
    uint64_t routePolicyBlocked;
};

enum class ConnectorHealthState {
    ONLINE,
    DEGRADED,
    OFFLINE,
    POLICY_BLOCKED
};

struct ConnectorHealthPolicy {
    uint32_t degradedFailureStreak = 2;
    uint32_t offlineFailureStreak = 4;
    uint32_t onlineRecoverSuccessStreak = 2;
};

struct ConnectorHealthSnapshot {
    ConnectorHealthState state = ConnectorHealthState::OFFLINE;
    uint64_t transitions = 0;
    uint64_t successes = 0;
    uint64_t failures = 0;
    uint64_t policyBlocks = 0;
    uint32_t consecutiveSuccesses = 0;
    uint32_t consecutiveFailures = 0;
};

struct WebConnectorHealth {
    ConnectorHealthSnapshot clearnet;
    ConnectorHealthSnapshot tor;
    ConnectorHealthSnapshot onion;
};

std::string connectorHealthStateToString(ConnectorHealthState state);

class ConnectorHealthMachine {
public:
    explicit ConnectorHealthMachine(const ConnectorHealthPolicy& policy = ConnectorHealthPolicy{});

    void setPolicy(const ConnectorHealthPolicy& policy);
    ConnectorHealthPolicy getPolicy() const;

    void recordSuccess();
    void recordFailure();
    void recordPolicyBlock();

    ConnectorHealthSnapshot snapshot() const;

private:
    ConnectorHealthPolicy policy_{};
    ConnectorHealthSnapshot snapshot_{};
};

struct ConnectorAbusePolicy {
    uint32_t policyBlockDeltaThreshold = 6;
    uint32_t failureDeltaThreshold = 20;
    uint32_t cooldownTicks = 30;
    uint32_t violationPenaltySteps = 2;
};

struct ConnectorAbuseDecision {
    bool triggered = false;
    uint32_t violations = 0;
    uint64_t deltaPolicyBlocks = 0;
    uint64_t deltaFailures = 0;
    uint64_t observedPolicyBlocks = 0;
    uint64_t observedFailures = 0;
    uint64_t lastPenaltyTick = 0;
};

class ConnectorAbuseGuard {
public:
    explicit ConnectorAbuseGuard(const ConnectorAbusePolicy& policy = ConnectorAbusePolicy{});

    void setPolicy(const ConnectorAbusePolicy& policy);
    ConnectorAbusePolicy getPolicy() const;

    ConnectorAbuseDecision observe(uint64_t tick, const WebConnectorHealth& health);

private:
    ConnectorAbusePolicy policy_{};
    bool initialized_ = false;
    uint64_t prevPolicyBlocks_ = 0;
    uint64_t prevFailures_ = 0;
    uint64_t lastPenaltyTick_ = 0;
};

class WebSearch {
public:
    WebSearch();
    ~WebSearch();
    
    bool init(const SearchConfig& config);
    void shutdown();
    
    std::vector<SearchResult> search(const std::string& query, QueryType type = QueryType::BOTH);
    std::vector<SearchResult> searchClearnet(const std::string& query);
    std::vector<SearchResult> searchDarknet(const std::string& query);
    
    void setConfig(const SearchConfig& config);
    SearchConfig getConfig() const;
    
    void addClearnetEngine(SearchEngine engine);
    void addDarknetEngine(SearchEngine engine);
    void addCustomEngine(const std::string& url, bool isDarknet);
    void removeEngine(SearchEngine engine);
    
    void setMaxResults(size_t count);
    void setTimeout(uint32_t seconds);
    
    void onSearchComplete(std::function<void(const std::vector<SearchResult>&)> callback);
    void onSearchError(std::function<void(const std::string&)> callback);

    WebStats getStats() const;
    void setConnectorHealthPolicy(const ConnectorHealthPolicy& policy);
    ConnectorHealthPolicy getConnectorHealthPolicy() const;
    WebConnectorHealth getConnectorHealth() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class TorEngine {
public:
    TorEngine();
    ~TorEngine();
    
    bool init(const TorConfig& config);
    void shutdown();
    bool isConnected() const;
    
    std::string fetch(const std::string& url);
    std::string fetchOnion(const std::string& onionUrl);
    
    bool newCircuit();
    std::string getExitNode() const;
    
    void setConfig(const TorConfig& config);
    TorConfig getConfig() const;
    
    void setTimeout(uint32_t seconds);
    void setMaxRetries(uint32_t retries);
    
    void onConnectionChange(std::function<void(bool)> callback);
    void onCircuitChange(std::function<void(const std::string&)> callback);
    
    struct TorStats {
        uint64_t requestsSent;
        uint64_t bytesTransferred;
        uint32_t circuitsUsed;
        double avgLatency;
        bool connected;
    };
    TorStats getStats() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class HtmlExtractor {
public:
    HtmlExtractor();
    ~HtmlExtractor();
    
    ExtractedContent extract(const std::string& html, const std::string& url = "");
    
    std::string extractTitle(const std::string& html);
    std::string extractMainText(const std::string& html);
    std::vector<std::string> extractCodeBlocks(const std::string& html);
    std::vector<std::string> extractLinks(const std::string& html);
    std::vector<std::string> extractOnionLinks(const std::string& html);
    std::map<std::string, std::string> extractMetadata(const std::string& html);
    
    void setMaxTextLength(size_t length);
    void setRemoveAds(bool remove);
    void setRemoveScripts(bool remove);
    void setRemoveStyles(bool remove);
    void setRemoveNavigation(bool remove);
    void setRiskPolicy(const ExtractionRiskPolicy& policy);
    ExtractionRiskPolicy getRiskPolicy() const;
    
    std::string cleanHtml(const std::string& html);
    std::string htmlToText(const std::string& html);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class QueryDetector {
public:
    QueryDetector();
    ~QueryDetector();
    
    QueryAnalysis analyze(const std::string& query);
    QueryType detectType(const std::string& query);
    
    void addDarknetKeyword(const std::string& keyword);
    void addClearnetKeyword(const std::string& keyword);
    void removeDarknetKeyword(const std::string& keyword);
    void removeClearnetKeyword(const std::string& keyword);
    
    std::vector<std::string> getDarknetKeywords() const;
    std::vector<std::string> getClearnetKeywords() const;
    
    void setDarknetThreshold(double threshold);
    void setClearnetThreshold(double threshold);
    
    bool isDarknetQuery(const std::string& query);
    bool isClearnetQuery(const std::string& query);
    bool requiresBothNetworks(const std::string& query);
    
    std::string normalizeQuery(const std::string& query);
    std::vector<std::string> extractKeywords(const std::string& query);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class AIWrapper {
public:
    AIWrapper();
    ~AIWrapper();
    
    bool init();
    void shutdown();
    
    std::string processQuery(const std::string& query, const std::string& modelResponse = "");
    std::string injectContext(const std::string& query, const std::vector<SearchResult>& results);
    
    void setWebSearch(WebSearch* search);
    void setTorEngine(TorEngine* tor);
    void setExtractor(HtmlExtractor* extractor);
    void setDetector(QueryDetector* detector);
    
    void enableAutoSearch(bool enable);
    void enableContextInjection(bool enable);
    void setMaxContextLength(size_t length);
    
    void onQueryProcessed(std::function<void(const std::string&, const std::string&)> callback);
    void onContextInjected(std::function<void(const std::string&)> callback);
    
    struct WrapperStats {
        uint64_t queriesProcessed;
        uint64_t searchesTriggered;
        uint64_t contextsInjected;
        double avgProcessingTime;
        uint64_t lastResultCount;
        uint64_t lastClearnetResults;
        uint64_t lastDarknetResults;
    };
    WrapperStats getStats() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct DarknetEngineInfo {
    SearchEngine engine;
    std::string name;
    std::string baseUrl;
    bool onionOnly;
};

class DarknetEngines {
public:
    DarknetEngines();
    std::vector<DarknetEngineInfo> list() const;
    std::string buildSearchUrl(SearchEngine engine, const std::string& query) const;
};

struct TorFetchStats {
    uint64_t requests;
    uint64_t successes;
    uint64_t failures;
    uint64_t onionRequests;
    uint64_t onionSuccesses;
    uint64_t onionFailures;
    uint64_t bytes;
};

class TorFetch {
public:
    TorFetch();
    ~TorFetch();
    
    bool init(const TorConfig& config);
    void shutdown();
    bool isReady() const;
    
    void setTorEngine(TorEngine* engine);
    void setTimeout(uint32_t seconds);
    void setMaxBytes(size_t bytes);
    
    std::string fetch(const std::string& url);
    std::string fetchOnion(const std::string& onionUrl);
    
    TorFetchStats getStats() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct RoutedQuery {
    SearchEngine engine;
    std::string url;
    bool direct;
};

class OnionSearchRouter {
public:
    std::vector<RoutedQuery> route(const std::string& query,
                                   const QueryAnalysis& analysis,
                                   const SearchConfig& config,
                                   const DarknetEngines& engines) const;
};

class DeepSearchWrapper {
public:
    struct Stats {
        uint64_t requests;
        uint64_t successes;
        uint64_t failures;
        uint64_t bytes;
    };
    
    DeepSearchWrapper();
    std::vector<SearchResult> search(const std::string& query,
                                     const std::vector<RoutedQuery>& routes,
                                     TorFetch& fetcher,
                                     size_t maxResultsPerEngine,
                                     const ExtractionRiskPolicy& riskPolicy = ExtractionRiskPolicy{},
                                     bool auditEnabled = true,
                                     const std::string& auditDir = "");
    Stats getStats() const;
    
private:
    Stats stats_{};
};

class ForumCrawler {
public:
    ForumCrawler();
    std::vector<SearchResult> crawl(const std::string& query,
                                    TorFetch& fetcher,
                                    HtmlExtractor& extractor,
                                    size_t maxResults,
                                    const ExtractionRiskPolicy& riskPolicy = ExtractionRiskPolicy{},
                                    bool auditEnabled = true,
                                    const std::string& auditDir = "");
};

SearchConfig defaultSearchConfig();
void sanitizeSearchConfig(SearchConfig& config);
bool loadSearchConfig(const std::string& path, SearchConfig& config);
bool loadSearchConfig(const std::string& path, SearchConfig& config, SearchConfigValidationStats* validation);
bool loadSearchConfigOverlay(const std::string& path, SearchConfig& config);
bool loadSearchConfigOverlay(const std::string& path, SearchConfig& config, SearchConfigValidationStats* validation);
bool saveSearchConfig(const SearchConfig& config, const std::string& path);

std::string urlEncode(const std::string& str);
std::string urlDecode(const std::string& str);
bool isOnionUrl(const std::string& url);
bool isValidUrl(const std::string& url);
bool isUrlAllowedByRoutePolicy(const std::string& url, const SearchConfig& config);
std::string extractDomain(const std::string& url);
std::string normalizeUrl(const std::string& url);

}
}

#endif
