#include "web/web.h"
#include "web/connector_audit.h"
#include "web/curl_fetch.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <mutex>

namespace synapse {
namespace web {

struct AIWrapper::Impl {
    WebSearch* webSearch = nullptr;
    TorEngine* torEngine = nullptr;
    HtmlExtractor* extractor = nullptr;
    QueryDetector* detector = nullptr;
    
    bool autoSearchEnabled = true;
    bool contextInjectionEnabled = true;
    size_t maxContextLength = 8000;
    size_t maxPagesToFetch = 2;
    size_t maxExcerptLength = 1200;
    
    mutable std::mutex mtx;
    WrapperStats stats{};
    
    std::function<void(const std::string&, const std::string&)> queryCallback;
    std::function<void(const std::string&)> contextCallback;
    
    std::string buildContextFromResults(const std::vector<SearchResult>& results,
                                        HtmlExtractor* extractor,
                                        const SearchConfig& cfg,
                                        size_t maxContextLength,
                                        size_t maxPagesToFetch,
                                        size_t maxExcerptLength);
    std::string formatSearchResult(const SearchResult& result);
    std::string truncateContext(const std::string& context, size_t maxLength);
    std::string fetchExcerpt(HtmlExtractor* extractor,
                             const SearchResult& result,
                             const SearchConfig& cfg,
                             size_t maxExcerptLength);
};

std::string AIWrapper::Impl::fetchExcerpt(HtmlExtractor* extractor,
                                          const SearchResult& result,
                                          const SearchConfig& cfg,
                                          size_t maxExcerptLength) {
    (void)extractor;
    if (!isUrlAllowedByRoutePolicy(result.url, cfg)) return "";

    CurlFetchOptions opt;
    opt.timeoutSeconds = cfg.timeoutSeconds > 0 ? cfg.timeoutSeconds : 10;
    opt.maxBytes = std::min<size_t>(cfg.maxPageSize > 0 ? cfg.maxPageSize : (1024 * 1024), 512 * 1024);
    if (!cfg.tor.socksHost.empty() && cfg.tor.socksPort != 0) {
        if (result.isOnion || cfg.routeClearnetThroughTor) {
            opt.socksProxyHostPort = cfg.tor.socksHost + ":" + std::to_string(cfg.tor.socksPort);
        }
    }
    CurlFetchResult res = curlFetch(result.url, opt);
    std::string body = res.body;
    if (cfg.bypassOnionHttpsFallback && res.exitCode != 0 && result.isOnion && result.url.rfind("https://", 0) == 0) {
        std::string fallback = "http://" + result.url.substr(8);
        CurlFetchResult res2 = curlFetch(fallback, opt);
        if (res2.exitCode == 0) body = res2.body;
    }
    if (body.empty()) return "";

    HtmlExtractor localExtractor;
    localExtractor.setRemoveAds(cfg.removeAds);
    localExtractor.setRemoveScripts(cfg.removeScripts);
    localExtractor.setRemoveStyles(cfg.removeStyles);
    localExtractor.setRiskPolicy(cfg.extractionRisk);

    ExtractedContent content = localExtractor.extract(body, result.url);
    if (cfg.connectorAuditEnabled) {
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        writeConnectorAuditArtifact(cfg.connectorAuditDir, result.url, body, content, now);
    }
    if (content.riskBlocked) return "";
    std::string text = content.mainText;
    if (text.empty() && !content.codeBlocks.empty()) {
        text = content.codeBlocks.front();
    }
    if (text.size() > maxExcerptLength) text.resize(maxExcerptLength);
    return text;
}

std::string AIWrapper::Impl::buildContextFromResults(const std::vector<SearchResult>& results,
                                                     HtmlExtractor* extractor,
                                                     const SearchConfig& cfg,
                                                     size_t maxContextLength,
                                                     size_t maxPagesToFetch,
                                                     size_t maxExcerptLength) {
    std::ostringstream context;
    context << "[Web Search Results]\n\n";
    
    size_t currentLength = 0;
    int resultNum = 1;
    size_t fetched = 0;
    
    for (const auto& result : results) {
        if (currentLength >= maxContextLength) break;
        
        std::string formatted = formatSearchResult(result);
        if (fetched < maxPagesToFetch) {
            std::string excerpt = fetchExcerpt(extractor, result, cfg, maxExcerptLength);
            if (!excerpt.empty()) {
                formatted += "Excerpt: " + excerpt + "\n";
            }
            fetched++;
        }
        if (currentLength + formatted.length() > maxContextLength) {
            formatted = truncateContext(formatted, maxContextLength - currentLength);
        }
        
        context << resultNum++ << ". " << formatted << "\n\n";
        currentLength += formatted.length();
    }
    
    context << "[End of Search Results]\n";
    return context.str();
}

std::string AIWrapper::Impl::formatSearchResult(const SearchResult& result) {
    std::ostringstream formatted;
    
    formatted << "Title: " << result.title << "\n";
    formatted << "URL: " << result.url << "\n";
    formatted << "Domain: " << result.domain;
    if (result.isOnion) formatted << " [DARKNET]";
    formatted << "\n";
    
    if (!result.snippet.empty()) {
        formatted << "Snippet: " << result.snippet << "\n";
    }
    
    return formatted.str();
}

std::string AIWrapper::Impl::truncateContext(const std::string& context, size_t maxLength) {
    if (context.length() <= maxLength) return context;
    
    std::string truncated = context.substr(0, maxLength - 20);
    size_t lastSpace = truncated.rfind(' ');
    if (lastSpace != std::string::npos) {
        truncated = truncated.substr(0, lastSpace);
    }
    
    return truncated + "... [truncated]";
}

AIWrapper::AIWrapper() : impl_(std::make_unique<Impl>()) {}
AIWrapper::~AIWrapper() { shutdown(); }

bool AIWrapper::init() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->stats = WrapperStats{};
    return true;
}

void AIWrapper::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->webSearch = nullptr;
    impl_->torEngine = nullptr;
    impl_->extractor = nullptr;
    impl_->detector = nullptr;
}

std::string AIWrapper::processQuery(const std::string& query, const std::string& modelResponse) {
    (void)modelResponse;
    WebSearch* webSearch = nullptr;
    QueryDetector* detector = nullptr;
    bool autoSearchEnabled = false;
    std::function<void(const std::string&, const std::string&)> queryCb;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->stats.queriesProcessed++;
        impl_->stats.lastResultCount = 0;
        impl_->stats.lastClearnetResults = 0;
        impl_->stats.lastDarknetResults = 0;
        autoSearchEnabled = impl_->autoSearchEnabled;
        webSearch = impl_->webSearch;
        detector = impl_->detector;
        queryCb = impl_->queryCallback;
    }

    if (!autoSearchEnabled || !webSearch || !detector) return query;

    auto startTime = std::chrono::high_resolution_clock::now();

    QueryAnalysis analysis = detector->analyze(query);
    
    bool shouldSearch = false;
    
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    if (lowerQuery.find("search") != std::string::npos ||
        lowerQuery.find("find") != std::string::npos ||
        lowerQuery.find("look up") != std::string::npos ||
        lowerQuery.find("what is") != std::string::npos ||
        lowerQuery.find("who is") != std::string::npos ||
        lowerQuery.find("latest") != std::string::npos ||
        lowerQuery.find("current") != std::string::npos ||
        lowerQuery.find("recent") != std::string::npos) {
        shouldSearch = true;
    }
    
    if (analysis.darknetConfidence > 0.3 || analysis.clearnetConfidence > 0.5) {
        shouldSearch = true;
    }
    
    if (!shouldSearch) return query;

    std::vector<SearchResult> results = webSearch->search(query, analysis.type);
    
    if (results.empty()) {
        return query;
    }

    uint64_t clearnet = 0;
    uint64_t darknet = 0;
    for (const auto& r : results) {
        if (r.isOnion) darknet++;
        else clearnet++;
    }
    
    std::string enrichedQuery = injectContext(query, results);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->stats.searchesTriggered++;
        impl_->stats.lastResultCount = static_cast<uint64_t>(results.size());
        impl_->stats.lastClearnetResults = clearnet;
        impl_->stats.lastDarknetResults = darknet;
        double totalTime = impl_->stats.avgProcessingTime * (impl_->stats.queriesProcessed - 1);
        impl_->stats.avgProcessingTime = (totalTime + duration.count()) / impl_->stats.queriesProcessed;
    }

    if (queryCb) queryCb(query, enrichedQuery);
    
    return enrichedQuery;
}

std::string AIWrapper::injectContext(const std::string& query, const std::vector<SearchResult>& results) {
    if (results.empty()) return query;

    HtmlExtractor* extractor = nullptr;
    WebSearch* webSearch = nullptr;
    bool enabled = false;
    size_t maxContextLength = 0;
    size_t maxPagesToFetch = 0;
    size_t maxExcerptLength = 0;
    std::function<void(const std::string&)> contextCb;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        enabled = impl_->contextInjectionEnabled;
        extractor = impl_->extractor;
        webSearch = impl_->webSearch;
        maxContextLength = impl_->maxContextLength;
        maxPagesToFetch = impl_->maxPagesToFetch;
        maxExcerptLength = impl_->maxExcerptLength;
        contextCb = impl_->contextCallback;
        if (enabled) impl_->stats.contextsInjected++;
    }

    if (!enabled) return query;

    SearchConfig cfg{};
    if (webSearch) cfg = webSearch->getConfig();

    std::string context = impl_->buildContextFromResults(results, extractor, cfg, maxContextLength, maxPagesToFetch, maxExcerptLength);
    
    std::ostringstream enriched;
    enriched << "I have searched the web for information related to your query. ";
    enriched << "Here is what I found:\n\n";
    enriched << context << "\n";
    enriched << "Based on this information, please answer the following question:\n\n";
    enriched << query;
    
    if (contextCb) contextCb(context);
    
    return enriched.str();
}

void AIWrapper::setWebSearch(WebSearch* search) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->webSearch = search;
}

void AIWrapper::setTorEngine(TorEngine* tor) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->torEngine = tor;
}

void AIWrapper::setExtractor(HtmlExtractor* extractor) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->extractor = extractor;
}

void AIWrapper::setDetector(QueryDetector* detector) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->detector = detector;
}

void AIWrapper::enableAutoSearch(bool enable) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->autoSearchEnabled = enable;
}

void AIWrapper::enableContextInjection(bool enable) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->contextInjectionEnabled = enable;
}

void AIWrapper::setMaxContextLength(size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxContextLength = length;
}

void AIWrapper::onQueryProcessed(std::function<void(const std::string&, const std::string&)> callback) {
    impl_->queryCallback = callback;
}

void AIWrapper::onContextInjected(std::function<void(const std::string&)> callback) {
    impl_->contextCallback = callback;
}

AIWrapper::WrapperStats AIWrapper::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->stats;
}

}
}
