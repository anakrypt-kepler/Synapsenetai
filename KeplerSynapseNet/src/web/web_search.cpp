#include "web/web.h"
#include "web/curl_fetch.h"
#include <mutex>
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <regex>
#include <ctime>
#include <iomanip>
#include <limits>
#include <unordered_set>

namespace synapse {
namespace web {

namespace {

static std::string toLowerCopy(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

static std::string trimCopy(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

static bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string normalizedHostFromUrl(const std::string& url) {
    std::string host = toLowerCopy(extractDomain(url));
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host = host.substr(0, colon);
    }
    return host;
}

static std::string normalizeHostRule(std::string value) {
    value = toLowerCopy(trimCopy(value));
    if (startsWith(value, "host:")) {
        value = trimCopy(value.substr(5));
    }
    if (startsWith(value, "http://")) {
        value = value.substr(7);
    } else if (startsWith(value, "https://")) {
        value = value.substr(8);
    }
    size_t slash = value.find('/');
    if (slash != std::string::npos) {
        value = value.substr(0, slash);
    }
    size_t colon = value.find(':');
    if (colon != std::string::npos) {
        value = value.substr(0, colon);
    }
    if (!value.empty() && value.back() == '.') {
        value.pop_back();
    }
    return trimCopy(value);
}

static bool hostRuleMatches(const std::string& host, const std::string& ruleRaw) {
    const std::string rule = toLowerCopy(trimCopy(ruleRaw));
    if (rule.empty()) return false;
    if (rule == "*") return true;

    if (startsWith(rule, "suffix:")) {
        const std::string suffixRaw = trimCopy(rule.substr(7));
        if (suffixRaw.empty()) return false;
        const std::string suffix = normalizeHostRule(suffixRaw);
        if (suffix.empty()) return false;
        return host == suffix || endsWith(host, "." + suffix);
    }

    if (startsWith(rule, "*.")) {
        const std::string suffix = normalizeHostRule(rule.substr(2));
        if (suffix.empty()) return false;
        return host == suffix || endsWith(host, "." + suffix);
    }

    const std::string normalizedRule = normalizeHostRule(rule);
    if (normalizedRule.empty()) return false;
    return host == normalizedRule || endsWith(host, "." + normalizedRule);
}

static bool hostMatchesAnyRule(const std::string& host, const std::vector<std::string>& rules) {
    for (const auto& rule : rules) {
        if (hostRuleMatches(host, rule)) return true;
    }
    return false;
}

static bool isHostAllowedBySitePolicy(const std::string& normalizedUrl,
                                      const SearchConfig& config,
                                      bool* bypassRoutePolicy) {
    if (bypassRoutePolicy) *bypassRoutePolicy = false;
    const std::string host = normalizedHostFromUrl(normalizedUrl);
    if (host.empty()) return false;

    const bool onion = isOnionUrl("http://" + host);
    const auto& allowlist = onion ? config.onionSiteAllowlist : config.clearnetSiteAllowlist;
    const auto& denylist = onion ? config.onionSiteDenylist : config.clearnetSiteDenylist;
    const auto& bypassHosts = onion ? config.onionRouteBypassHosts : config.clearnetRouteBypassHosts;

    if (hostMatchesAnyRule(host, denylist)) {
        return false;
    }
    if (!allowlist.empty() && !hostMatchesAnyRule(host, allowlist)) {
        return false;
    }
    if (bypassRoutePolicy && hostMatchesAnyRule(host, bypassHosts)) {
        *bypassRoutePolicy = true;
    }
    return true;
}

static bool routeRuleMatches(const std::string& ruleRaw,
                             const std::string& normalizedUrl,
                             const std::string& normalizedHost) {
    const std::string rule = toLowerCopy(trimCopy(ruleRaw));
    if (rule.empty()) return false;
    if (rule == "*") return true;

    if (startsWith(rule, "prefix:")) {
        const std::string prefix = trimCopy(rule.substr(7));
        return !prefix.empty() && startsWith(normalizedUrl, prefix);
    }
    if (startsWith(rule, "host:")) {
        const std::string host = trimCopy(rule.substr(5));
        return !host.empty() && normalizedHost == host;
    }
    if (startsWith(rule, "suffix:")) {
        const std::string suffix = trimCopy(rule.substr(7));
        if (suffix.empty()) return false;
        if (suffix[0] == '.') return endsWith(normalizedHost, suffix);
        return normalizedHost == suffix || endsWith(normalizedHost, "." + suffix);
    }

    return normalizedUrl == rule;
}

static uint32_t clampPositiveU32(uint32_t value, uint32_t fallback) {
    return value == 0 ? fallback : value;
}

static ConnectorHealthPolicy sanitizeConnectorHealthPolicy(const ConnectorHealthPolicy& policy) {
    ConnectorHealthPolicy out = policy;
    out.degradedFailureStreak = clampPositiveU32(out.degradedFailureStreak, 2);
    out.offlineFailureStreak = clampPositiveU32(out.offlineFailureStreak, out.degradedFailureStreak);
    if (out.offlineFailureStreak < out.degradedFailureStreak) {
        out.offlineFailureStreak = out.degradedFailureStreak;
    }
    out.onlineRecoverSuccessStreak = clampPositiveU32(out.onlineRecoverSuccessStreak, 2);
    return out;
}

static ConnectorAbusePolicy sanitizeConnectorAbusePolicy(const ConnectorAbusePolicy& policy) {
    ConnectorAbusePolicy out = policy;
    out.policyBlockDeltaThreshold = clampPositiveU32(out.policyBlockDeltaThreshold, 6);
    out.failureDeltaThreshold = clampPositiveU32(out.failureDeltaThreshold, 20);
    out.cooldownTicks = clampPositiveU32(out.cooldownTicks, 30);
    out.violationPenaltySteps = clampPositiveU32(out.violationPenaltySteps, 2);
    if (out.violationPenaltySteps > 16) out.violationPenaltySteps = 16;
    return out;
}

static void incrementU64(uint64_t& value) {
    if (value != std::numeric_limits<uint64_t>::max()) {
        ++value;
    }
}

static void incrementU32(uint32_t& value) {
    if (value != std::numeric_limits<uint32_t>::max()) {
        ++value;
    }
}

static void replaceAllInPlace(std::string& value, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::string decodeBasicHtmlEntities(std::string value) {
    replaceAllInPlace(value, "&amp;", "&");
    replaceAllInPlace(value, "&quot;", "\"");
    replaceAllInPlace(value, "&#39;", "'");
    replaceAllInPlace(value, "&lt;", "<");
    replaceAllInPlace(value, "&gt;", ">");
    return value;
}

static std::string decodeBasicJsEscapes(std::string value) {
    replaceAllInPlace(value, "\\/", "/");
    replaceAllInPlace(value, "\\u002f", "/");
    replaceAllInPlace(value, "\\u002F", "/");
    replaceAllInPlace(value, "\\u003a", ":");
    replaceAllInPlace(value, "\\u003A", ":");
    replaceAllInPlace(value, "\\u0026", "&");
    replaceAllInPlace(value, "\\u003d", "=");
    replaceAllInPlace(value, "\\u003D", "=");
    replaceAllInPlace(value, "\\u003f", "?");
    replaceAllInPlace(value, "\\u003F", "?");
    return value;
}

static std::string extractQueryValue(const std::string& url, const std::string& key) {
    size_t qPos = url.find('?');
    if (qPos == std::string::npos) return "";
    size_t endPos = url.find('#', qPos + 1);
    std::string query = url.substr(qPos + 1, endPos == std::string::npos ? std::string::npos : (endPos - qPos - 1));
    std::stringstream ss(query);
    std::string token;
    while (std::getline(ss, token, '&')) {
        size_t eq = token.find('=');
        std::string k = eq == std::string::npos ? token : token.substr(0, eq);
        if (k != key) continue;
        std::string v = eq == std::string::npos ? "" : token.substr(eq + 1);
        return urlDecode(v);
    }
    return "";
}

static std::string normalizeSearchResultUrl(const std::string& rawHref, SearchEngine engine) {
    std::string href = trimCopy(decodeBasicHtmlEntities(rawHref));
    if (href.empty()) return "";

    if (startsWith(href, "//")) {
        href = "https:" + href;
    } else if (!href.empty() && href[0] == '/') {
        if (engine == SearchEngine::DUCKDUCKGO) {
            href = "https://duckduckgo.com" + href;
        } else if (engine == SearchEngine::BRAVE) {
            href = "https://search.brave.com" + href;
        } else {
            return "";
        }
    }

    href = decodeBasicJsEscapes(href);
    if (engine == SearchEngine::DUCKDUCKGO) {
        std::string lowerHref = toLowerCopy(href);
        if (lowerHref.find("duckduckgo.com/l/?") != std::string::npos ||
            lowerHref.find("duckduckgo.com/l/") != std::string::npos) {
            std::string decoded = extractQueryValue(href, "uddg");
            if (decoded.empty()) decoded = extractQueryValue(href, "u");
            if (!decoded.empty()) {
                href = decodeBasicJsEscapes(decodeBasicHtmlEntities(decoded));
            }
        }
    }

    href = trimCopy(href);
    if (!isValidUrl(href)) return "";
    if (href.find("javascript:") != std::string::npos) return "";
    return href;
}

static bool isProviderInternalResultUrl(const std::string& url, SearchEngine engine) {
    const std::string host = normalizedHostFromUrl(url);
    if (host.empty()) return true;
    if (engine == SearchEngine::DUCKDUCKGO) {
        return host == "duckduckgo.com" || endsWith(host, ".duckduckgo.com");
    }
    if (engine == SearchEngine::BRAVE) {
        return host == "search.brave.com" || host == "brave.com" || endsWith(host, ".brave.com");
    }
    return false;
}

static void transitionConnectorState(ConnectorHealthSnapshot& snapshot, ConnectorHealthState next) {
    if (snapshot.state != next) {
        snapshot.state = next;
        incrementU64(snapshot.transitions);
    }
}

}

std::string connectorHealthStateToString(ConnectorHealthState state) {
    switch (state) {
        case ConnectorHealthState::ONLINE: return "ONLINE";
        case ConnectorHealthState::DEGRADED: return "DEGRADED";
        case ConnectorHealthState::OFFLINE: return "OFFLINE";
        case ConnectorHealthState::POLICY_BLOCKED: return "POLICY_BLOCKED";
    }
    return "OFFLINE";
}

ConnectorHealthMachine::ConnectorHealthMachine(const ConnectorHealthPolicy& policy)
    : policy_(sanitizeConnectorHealthPolicy(policy)) {}

void ConnectorHealthMachine::setPolicy(const ConnectorHealthPolicy& policy) {
    policy_ = sanitizeConnectorHealthPolicy(policy);
}

ConnectorHealthPolicy ConnectorHealthMachine::getPolicy() const {
    return policy_;
}

void ConnectorHealthMachine::recordSuccess() {
    incrementU64(snapshot_.successes);
    snapshot_.consecutiveFailures = 0;
    incrementU32(snapshot_.consecutiveSuccesses);

    ConnectorHealthState next = snapshot_.state;
    if (snapshot_.state == ConnectorHealthState::POLICY_BLOCKED ||
        snapshot_.state == ConnectorHealthState::OFFLINE ||
        snapshot_.state == ConnectorHealthState::DEGRADED) {
        if (snapshot_.consecutiveSuccesses >= policy_.onlineRecoverSuccessStreak) {
            next = ConnectorHealthState::ONLINE;
        } else {
            next = ConnectorHealthState::DEGRADED;
        }
    }
    transitionConnectorState(snapshot_, next);
}

void ConnectorHealthMachine::recordFailure() {
    incrementU64(snapshot_.failures);
    snapshot_.consecutiveSuccesses = 0;
    incrementU32(snapshot_.consecutiveFailures);

    ConnectorHealthState next = snapshot_.state;
    if (snapshot_.state == ConnectorHealthState::POLICY_BLOCKED) {
        next = ConnectorHealthState::OFFLINE;
    }

    if (snapshot_.consecutiveFailures >= policy_.offlineFailureStreak) {
        next = ConnectorHealthState::OFFLINE;
    } else if (snapshot_.consecutiveFailures >= policy_.degradedFailureStreak) {
        next = ConnectorHealthState::DEGRADED;
    }

    transitionConnectorState(snapshot_, next);
}

void ConnectorHealthMachine::recordPolicyBlock() {
    incrementU64(snapshot_.policyBlocks);
    snapshot_.consecutiveSuccesses = 0;
    snapshot_.consecutiveFailures = 0;
    transitionConnectorState(snapshot_, ConnectorHealthState::POLICY_BLOCKED);
}

ConnectorHealthSnapshot ConnectorHealthMachine::snapshot() const {
    return snapshot_;
}

ConnectorAbuseGuard::ConnectorAbuseGuard(const ConnectorAbusePolicy& policy)
    : policy_(sanitizeConnectorAbusePolicy(policy)) {}

void ConnectorAbuseGuard::setPolicy(const ConnectorAbusePolicy& policy) {
    policy_ = sanitizeConnectorAbusePolicy(policy);
}

ConnectorAbusePolicy ConnectorAbuseGuard::getPolicy() const {
    return policy_;
}

ConnectorAbuseDecision ConnectorAbuseGuard::observe(uint64_t tick, const WebConnectorHealth& health) {
    ConnectorAbuseDecision out;
    out.lastPenaltyTick = lastPenaltyTick_;

    const uint64_t observedPolicyBlocks =
        health.clearnet.policyBlocks + health.tor.policyBlocks + health.onion.policyBlocks;
    const uint64_t observedFailures =
        health.clearnet.failures + health.tor.failures + health.onion.failures;

    out.observedPolicyBlocks = observedPolicyBlocks;
    out.observedFailures = observedFailures;

    if (!initialized_) {
        initialized_ = true;
        prevPolicyBlocks_ = observedPolicyBlocks;
        prevFailures_ = observedFailures;
        return out;
    }

    const uint64_t deltaPolicy = observedPolicyBlocks >= prevPolicyBlocks_
        ? (observedPolicyBlocks - prevPolicyBlocks_)
        : 0;
    const uint64_t deltaFailures = observedFailures >= prevFailures_
        ? (observedFailures - prevFailures_)
        : 0;

    prevPolicyBlocks_ = observedPolicyBlocks;
    prevFailures_ = observedFailures;

    out.deltaPolicyBlocks = deltaPolicy;
    out.deltaFailures = deltaFailures;

    const uint64_t cooldownUntil = lastPenaltyTick_ + policy_.cooldownTicks;
    if (tick < cooldownUntil) {
        return out;
    }

    const bool policyExceeded = deltaPolicy >= policy_.policyBlockDeltaThreshold;
    const bool failureExceeded = deltaFailures >= policy_.failureDeltaThreshold;
    if (!policyExceeded && !failureExceeded) {
        return out;
    }

    out.triggered = true;
    out.violations = policy_.violationPenaltySteps;

    if (deltaPolicy >= static_cast<uint64_t>(policy_.policyBlockDeltaThreshold) * 2 ||
        deltaFailures >= static_cast<uint64_t>(policy_.failureDeltaThreshold) * 2) {
        if (out.violations < 16) {
            ++out.violations;
        }
    }

    lastPenaltyTick_ = tick;
    out.lastPenaltyTick = lastPenaltyTick_;
    return out;
}

struct WebSearch::Impl {
    SearchConfig config;
    mutable std::mutex mtx;
    bool initialized = false;
    WebStats stats{};
    ConnectorHealthPolicy connectorHealthPolicy{};
    ConnectorHealthMachine clearnetHealth{};
    ConnectorHealthMachine torHealth{};
    ConnectorHealthMachine onionHealth{};
    std::function<void(const std::vector<SearchResult>&)> searchCallback;
    std::function<void(const std::string&)> errorCallback;
    
    std::string buildSearchUrl(SearchEngine engine, const std::string& query);
    std::vector<SearchResult> parseResults(const std::string& html, SearchEngine engine);
    std::string httpGet(const std::string& host, const std::string& path, uint16_t port = 80);
    double calculateRelevance(const std::string& query, const SearchResult& result);
};

std::string WebSearch::Impl::buildSearchUrl(SearchEngine engine, const std::string& query) {
    std::string encoded = urlEncode(query);
    switch (engine) {
        case SearchEngine::DUCKDUCKGO:
            return "/html/?q=" + encoded;
        case SearchEngine::BRAVE:
            return "/search?q=" + encoded;
        default:
            return "/search?q=" + encoded;
    }
}

std::string WebSearch::Impl::httpGet(const std::string& host, const std::string& path, uint16_t port) {
    std::ostringstream url;
    url << "http://" << host;
    if (port != 0 && port != 80) url << ":" << port;
    if (!path.empty() && path[0] != '/') url << "/";
    url << (path.empty() ? "/" : path);

    CurlFetchOptions opt;
    opt.timeoutSeconds = config.timeoutSeconds;
    opt.maxBytes = config.maxPageSize;
    if (config.routeClearnetThroughTor && !config.tor.socksHost.empty() && config.tor.socksPort != 0) {
        opt.socksProxyHostPort = config.tor.socksHost + ":" + std::to_string(config.tor.socksPort);
    }
    CurlFetchResult res = curlFetch(url.str(), opt);
    if (res.exitCode != 0 || res.body.empty()) {
        stats.failedFetches++;
        if (errorCallback && !res.error.empty()) {
            errorCallback(res.error);
        }
        return "";
    }

    stats.successfulFetches++;
    stats.bytesDownloaded += res.body.size();
    return res.body;
}

std::vector<SearchResult> WebSearch::Impl::parseResults(const std::string& html, SearchEngine engine) {
    std::vector<SearchResult> results;
    std::unordered_set<std::string> seenUrls;
    auto tryPushResult = [&](const std::string& hrefRaw, const std::string& titleRaw) {
        const std::string url = normalizeSearchResultUrl(hrefRaw, engine);
        if (url.empty()) return;
        if (isProviderInternalResultUrl(url, engine)) return;
        if (!seenUrls.insert(url).second) return;

        SearchResult result;
        result.url = url;
        result.title = trimCopy(decodeBasicJsEscapes(decodeBasicHtmlEntities(titleRaw)));
        if (result.title.empty()) result.title = extractDomain(result.url);
        result.domain = extractDomain(result.url);
        result.isOnion = isOnionUrl(result.url);
        result.timestamp = std::time(nullptr);
        result.contentType = ContentType::HTML;
        result.relevanceScore = 0.0;
        results.push_back(std::move(result));
    };

    std::regex linkRegex("<a[^>]*href\\s*=\\s*[\"']([^\"']+)[\"'][^>]*>([^<]*)</a>", std::regex::icase);
    std::smatch match;
    std::string::const_iterator searchStart(html.cbegin());
    while (std::regex_search(searchStart, html.cend(), match, linkRegex)) {
        tryPushResult(match[1].str(), match[2].str());
        searchStart = match.suffix().first;
        if (results.size() >= config.maxResultsPerEngine) break;
    }

    if (engine == SearchEngine::BRAVE && results.size() < config.maxResultsPerEngine) {
        std::regex braveInlineRegex("title:\\s*\"([^\"]+)\"\\s*,\\s*url:\\s*\"([^\"]+)\"");
        std::string::const_iterator it(html.cbegin());
        std::smatch item;
        while (std::regex_search(it, html.cend(), item, braveInlineRegex)) {
            tryPushResult(item[2].str(), item[1].str());
            it = item.suffix().first;
            if (results.size() >= config.maxResultsPerEngine) break;
        }
    }

    return results;
}

double WebSearch::Impl::calculateRelevance(const std::string& query, const SearchResult& result) {
    double score = 0.0;
    std::string lowerQuery = query;
    std::string lowerTitle = result.title;
    std::string lowerSnippet = result.snippet;
    
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
    std::transform(lowerSnippet.begin(), lowerSnippet.end(), lowerSnippet.begin(), ::tolower);
    
    std::istringstream iss(lowerQuery);
    std::string word;
    while (iss >> word) {
        if (lowerTitle.find(word) != std::string::npos) score += 2.0;
        if (lowerSnippet.find(word) != std::string::npos) score += 1.0;
        if (result.domain.find(word) != std::string::npos) score += 1.5;
    }
    
    return score;
}

WebSearch::WebSearch() : impl_(std::make_unique<Impl>()) {
    impl_->config = defaultSearchConfig();
}
WebSearch::~WebSearch() { shutdown(); }

bool WebSearch::init(const SearchConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config = config;
    impl_->clearnetHealth = ConnectorHealthMachine(impl_->connectorHealthPolicy);
    impl_->torHealth = ConnectorHealthMachine(impl_->connectorHealthPolicy);
    impl_->onionHealth = ConnectorHealthMachine(impl_->connectorHealthPolicy);
    impl_->initialized = true;
    return true;
}

void WebSearch::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->initialized = false;
}

std::vector<SearchResult> WebSearch::search(const std::string& query, QueryType type) {
    std::vector<SearchResult> allResults;
    
    if (type == QueryType::CLEARNET || type == QueryType::BOTH) {
        auto clearnetResults = searchClearnet(query);
        allResults.insert(allResults.end(), clearnetResults.begin(), clearnetResults.end());
    }
    
    if (type == QueryType::DARKNET || type == QueryType::BOTH) {
        auto darknetResults = searchDarknet(query);
        allResults.insert(allResults.end(), darknetResults.begin(), darknetResults.end());
    }
    
    for (auto& result : allResults) {
        result.relevanceScore = impl_->calculateRelevance(query, result);
    }
    
    std::sort(allResults.begin(), allResults.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.relevanceScore > b.relevanceScore;
              });
    
    impl_->stats.totalSearches++;
    
    if (impl_->searchCallback) {
        impl_->searchCallback(allResults);
    }
    
    return allResults;
}

std::vector<SearchResult> WebSearch::searchClearnet(const std::string& query) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<SearchResult> results;
    
    if (!impl_->config.enableClearnet) return results;

    auto routeAllowed = [&](const std::string& url) {
        const bool allowed = isUrlAllowedByRoutePolicy(url, impl_->config);
        incrementU64(impl_->stats.routePolicyChecks);
        if (allowed) incrementU64(impl_->stats.routePolicyPassed);
        else incrementU64(impl_->stats.routePolicyBlocked);
        return allowed;
    };
    
    std::string encoded = urlEncode(query);
    
    auto applyTemplate = [&](const std::string& base) {
        std::string url = base;
        size_t pos = url.find("{query}");
        if (pos != std::string::npos) {
            url.replace(pos, 7, encoded);
            return url;
        }
        pos = url.find("%s");
        if (pos != std::string::npos) {
            url.replace(pos, 2, encoded);
            return url;
        }
        if (url.find('?') == std::string::npos) {
            return url + "?q=" + encoded;
        }
        return url + "&q=" + encoded;
    };
    
    auto fetchUrl = [&](const std::string& url) -> std::string {
        CurlFetchOptions opt;
        opt.timeoutSeconds = impl_->config.timeoutSeconds;
        opt.maxBytes = impl_->config.maxPageSize;
        if (impl_->config.routeClearnetThroughTor && !impl_->config.tor.socksHost.empty() && impl_->config.tor.socksPort != 0) {
            opt.socksProxyHostPort = impl_->config.tor.socksHost + ":" + std::to_string(impl_->config.tor.socksPort);
        }
        CurlFetchResult res = curlFetch(url, opt);
        if (res.exitCode != 0) {
            impl_->stats.failedFetches++;
            if (impl_->errorCallback && !res.error.empty()) {
                impl_->errorCallback(res.error);
            }
            return "";
        }
        impl_->stats.successfulFetches++;
        impl_->stats.bytesDownloaded += res.body.size();
        return res.body;
    };
    
    for (auto engine : impl_->config.clearnetEngines) {
        std::string path = impl_->buildSearchUrl(engine, query);
        std::string fullUrl;
        
        switch (engine) {
            case SearchEngine::DUCKDUCKGO:
                fullUrl = "https://html.duckduckgo.com" + path;
                break;
            case SearchEngine::BRAVE:
                fullUrl = "https://search.brave.com" + path;
                break;
            default:
                continue;
        }
        if (!routeAllowed(fullUrl)) {
            impl_->clearnetHealth.recordPolicyBlock();
            if (impl_->config.routeClearnetThroughTor) {
                impl_->torHealth.recordPolicyBlock();
            }
            continue;
        }
        
        std::string html = fetchUrl(fullUrl);
        if (html.empty()) {
            impl_->clearnetHealth.recordFailure();
            if (impl_->config.routeClearnetThroughTor) {
                impl_->torHealth.recordFailure();
            }
            continue;
        }
        impl_->clearnetHealth.recordSuccess();
        if (impl_->config.routeClearnetThroughTor) {
            impl_->torHealth.recordSuccess();
        }
        
        auto engineResults = impl_->parseResults(html, engine);
        if (engine == SearchEngine::DUCKDUCKGO && engineResults.empty()) {
            if (html.find("anomaly-modal") != std::string::npos ||
                html.find("bots use DuckDuckGo") != std::string::npos ||
                html.find("bots use duckduckgo") != std::string::npos) {
                if (impl_->errorCallback) {
                    impl_->errorCallback("duckduckgo_challenge_page");
                }
            }
        }
        for (const auto& item : engineResults) {
            if (!routeAllowed(item.url)) {
                impl_->clearnetHealth.recordPolicyBlock();
                if (impl_->config.routeClearnetThroughTor) {
                    impl_->torHealth.recordPolicyBlock();
                }
                continue;
            }
            results.push_back(item);
        }
    }
    
    for (const auto& base : impl_->config.customClearnetUrls) {
        std::string url = applyTemplate(base);
        if (!routeAllowed(url)) {
            impl_->clearnetHealth.recordPolicyBlock();
            if (impl_->config.routeClearnetThroughTor) {
                impl_->torHealth.recordPolicyBlock();
            }
            continue;
        }
        std::string html = fetchUrl(url);
        if (html.empty()) {
            impl_->clearnetHealth.recordFailure();
            if (impl_->config.routeClearnetThroughTor) {
                impl_->torHealth.recordFailure();
            }
            continue;
        }
        impl_->clearnetHealth.recordSuccess();
        if (impl_->config.routeClearnetThroughTor) {
            impl_->torHealth.recordSuccess();
        }
        auto engineResults = impl_->parseResults(html, SearchEngine::CUSTOM);
        for (const auto& item : engineResults) {
            if (!routeAllowed(item.url)) {
                impl_->clearnetHealth.recordPolicyBlock();
                if (impl_->config.routeClearnetThroughTor) {
                    impl_->torHealth.recordPolicyBlock();
                }
                continue;
            }
            results.push_back(item);
        }
    }
    
    impl_->stats.clearnetSearches++;
    return results;
}

std::vector<SearchResult> WebSearch::searchDarknet(const std::string& query) {
    SearchConfig config;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        config = impl_->config;
    }
    
    std::vector<SearchResult> results;
    if (!config.enableDarknet) return results;

    uint64_t routePolicyChecks = 0;
    uint64_t routePolicyPassed = 0;
    uint64_t routePolicyBlocked = 0;
    auto routeAllowed = [&](const std::string& url) {
        const bool allowed = isUrlAllowedByRoutePolicy(url, config);
        ++routePolicyChecks;
        if (allowed) ++routePolicyPassed;
        else ++routePolicyBlocked;
        return allowed;
    };
    
    QueryDetector detector;
    QueryAnalysis analysis = detector.analyze(query);
    
    DarknetEngines engines;
    OnionSearchRouter router;
    auto routes = router.route(query, analysis, config, engines);
    
    TorEngine tor;
    TorConfig torCfg = config.tor;
    if (torCfg.socksHost.empty() || torCfg.socksPort == 0) {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->stats.darknetSearches++;
        impl_->torHealth.recordFailure();
        impl_->onionHealth.recordFailure();
        if (impl_->errorCallback) {
            impl_->errorCallback("darknet_search_missing_tor_socks_config");
        }
        return results;
    }
    if (torCfg.controlHost.empty()) torCfg.controlHost = "127.0.0.1";
    if (torCfg.controlPort == 0) torCfg.controlPort = 9051;
    torCfg.bypassOnionHttpsFallback = config.bypassOnionHttpsFallback;
    torCfg.circuitTimeout = config.timeoutSeconds;
    const bool torReady = tor.init(torCfg);
    const bool hasDarknetSources = !config.darknetEngines.empty() ||
                                   !config.customDarknetUrls.empty() ||
                                   !config.directOnionLinks.empty();
    if (!torReady) {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->stats.darknetSearches++;
        impl_->torHealth.recordFailure();
        if (hasDarknetSources) {
            impl_->onionHealth.recordFailure();
        }
        return results;
    }

    size_t onionRouteCount = 0;
    for (const auto& route : routes) {
        if (isOnionUrl(route.url)) {
            ++onionRouteCount;
        }
    }
    if (routes.empty() && hasDarknetSources) {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->stats.darknetSearches++;
        impl_->onionHealth.recordPolicyBlock();
        return results;
    }
    
    TorFetch fetcher;
    fetcher.setTorEngine(&tor);
    fetcher.setTimeout(config.timeoutSeconds);
    fetcher.setMaxBytes(config.maxPageSize);
    fetcher.init(torCfg);
    
    DeepSearchWrapper deep;
    auto searchResults = deep.search(query,
                                     routes,
                                     fetcher,
                                     config.maxResultsPerEngine,
                                     config.extractionRisk,
                                     config.connectorAuditEnabled,
                                     config.connectorAuditDir);
    results.insert(results.end(), searchResults.begin(), searchResults.end());
    
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    bool wantsForum = lowerQuery.find("dread") != std::string::npos ||
                      lowerQuery.find("forum") != std::string::npos ||
                      lowerQuery.find("discussion") != std::string::npos ||
                      lowerQuery.find("thread") != std::string::npos;
    
    if (wantsForum) {
        HtmlExtractor extractor;
        ForumCrawler crawler;
        auto forumResults = crawler.crawl(query,
                                          fetcher,
                                          extractor,
                                          config.maxResultsPerEngine,
                                          config.extractionRisk,
                                          config.connectorAuditEnabled,
                                          config.connectorAuditDir);
        results.insert(results.end(), forumResults.begin(), forumResults.end());
    }
    
    uint32_t filteredClearnetPolicyBlocks = 0;
    uint32_t filteredOnionPolicyBlocks = 0;
    {
        std::vector<SearchResult> filtered;
        filtered.reserve(results.size());
        for (const auto& item : results) {
            if (routeAllowed(item.url)) {
                filtered.push_back(item);
                continue;
            }
            if (item.isOnion) {
                incrementU32(filteredOnionPolicyBlocks);
            } else {
                incrementU32(filteredClearnetPolicyBlocks);
            }
        }
        results.swap(filtered);
    }

    auto fetchStats = fetcher.getStats();
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->stats.darknetSearches++;
        impl_->stats.successfulFetches += fetchStats.successes;
        impl_->stats.failedFetches += fetchStats.failures;
        impl_->stats.bytesDownloaded += fetchStats.bytes;
        impl_->stats.routePolicyChecks += routePolicyChecks;
        impl_->stats.routePolicyPassed += routePolicyPassed;
        impl_->stats.routePolicyBlocked += routePolicyBlocked;
        if (fetchStats.requests > 0) {
            if (fetchStats.successes > 0) {
                impl_->torHealth.recordSuccess();
            } else {
                impl_->torHealth.recordFailure();
            }
        }
        if (fetchStats.onionRequests > 0 || onionRouteCount > 0) {
            if (fetchStats.onionSuccesses > 0) {
                impl_->onionHealth.recordSuccess();
            } else {
                impl_->onionHealth.recordFailure();
            }
        }
        for (uint32_t i = 0; i < filteredOnionPolicyBlocks; ++i) {
            impl_->onionHealth.recordPolicyBlock();
        }
        for (uint32_t i = 0; i < filteredClearnetPolicyBlocks; ++i) {
            impl_->clearnetHealth.recordPolicyBlock();
        }
    }
    
    return results;
}

void WebSearch::setConfig(const SearchConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config = config;
}

SearchConfig WebSearch::getConfig() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->config;
}

void WebSearch::addClearnetEngine(SearchEngine engine) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.clearnetEngines.push_back(engine);
}

void WebSearch::addDarknetEngine(SearchEngine engine) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.darknetEngines.push_back(engine);
}

void WebSearch::addCustomEngine(const std::string& url, bool isDarknet) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (isDarknet) {
        impl_->config.customDarknetUrls.push_back(url);
    } else {
        impl_->config.customClearnetUrls.push_back(url);
    }
}

void WebSearch::removeEngine(SearchEngine engine) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto& clearnet = impl_->config.clearnetEngines;
    auto& darknet = impl_->config.darknetEngines;
    clearnet.erase(std::remove(clearnet.begin(), clearnet.end(), engine), clearnet.end());
    darknet.erase(std::remove(darknet.begin(), darknet.end(), engine), darknet.end());
}

void WebSearch::setMaxResults(size_t count) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.maxResultsPerEngine = count;
}

void WebSearch::setTimeout(uint32_t seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.timeoutSeconds = seconds;
}

void WebSearch::onSearchComplete(std::function<void(const std::vector<SearchResult>&)> callback) {
    impl_->searchCallback = callback;
}

void WebSearch::onSearchError(std::function<void(const std::string&)> callback) {
    impl_->errorCallback = callback;
}

WebStats WebSearch::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->stats;
}

void WebSearch::setConnectorHealthPolicy(const ConnectorHealthPolicy& policy) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->connectorHealthPolicy = sanitizeConnectorHealthPolicy(policy);
    impl_->clearnetHealth.setPolicy(impl_->connectorHealthPolicy);
    impl_->torHealth.setPolicy(impl_->connectorHealthPolicy);
    impl_->onionHealth.setPolicy(impl_->connectorHealthPolicy);
}

ConnectorHealthPolicy WebSearch::getConnectorHealthPolicy() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->connectorHealthPolicy;
}

WebConnectorHealth WebSearch::getConnectorHealth() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    WebConnectorHealth out;
    out.clearnet = impl_->clearnetHealth.snapshot();
    out.tor = impl_->torHealth.snapshot();
    out.onion = impl_->onionHealth.snapshot();
    return out;
}

std::string urlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : str) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else if (c == ' ') {
            escaped << '+';
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    
    return escaped.str();
}

std::string urlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int value;
            std::istringstream iss(str.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

bool isOnionUrl(const std::string& url) {
    std::string host = url;
    size_t schemePos = host.find("://");
    if (schemePos != std::string::npos) {
        host = host.substr(schemePos + 3);
    }
    size_t slash = host.find('/');
    if (slash != std::string::npos) {
        host = host.substr(0, slash);
    }
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host = host.substr(0, colon);
    }
    if (host.size() <= 6) return false;
    if (host.size() != 62) return host.find(".onion") != std::string::npos;
    if (host.substr(host.size() - 6) != ".onion") return false;
    std::string stem = host.substr(0, host.size() - 6);
    if (stem.size() != 56) return false;
    for (char c : stem) {
        if (!(c >= 'a' && c <= 'z') && !(c >= '2' && c <= '7')) {
            return false;
        }
    }
    return true;
}

bool isValidUrl(const std::string& url) {
    if (url.empty()) return false;
    if (url.substr(0, 7) != "http://" && url.substr(0, 8) != "https://") return false;
    return true;
}

bool isUrlAllowedByRoutePolicy(const std::string& url, const SearchConfig& config) {
    const std::string normalizedUrl = normalizeUrl(trimCopy(url));
    if (normalizedUrl.empty() || !isValidUrl(normalizedUrl)) return false;

    bool bypassRoutePolicy = false;
    if (!isHostAllowedBySitePolicy(normalizedUrl, config, &bypassRoutePolicy)) {
        return false;
    }
    if (bypassRoutePolicy) {
        return true;
    }

    const std::string normalizedHost = normalizedHostFromUrl(normalizedUrl);

    for (const auto& rule : config.fetchDenylistRoutes) {
        if (routeRuleMatches(rule, normalizedUrl, normalizedHost)) {
            return false;
        }
    }

    if (config.fetchAllowlistRoutes.empty()) {
        return true;
    }

    for (const auto& rule : config.fetchAllowlistRoutes) {
        if (routeRuleMatches(rule, normalizedUrl, normalizedHost)) {
            return true;
        }
    }

    return false;
}

std::string extractDomain(const std::string& url) {
    size_t start = url.find("://");
    if (start == std::string::npos) return "";
    start += 3;
    
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.length();
    
    return url.substr(start, end - start);
}

std::string normalizeUrl(const std::string& url) {
    std::string normalized = url;
    
    if (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    
    return normalized;
}

}
}
