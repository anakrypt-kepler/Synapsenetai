#include "web/web.h"
#include <regex>
#include <set>

namespace synapse {
namespace web {

static std::string applyTemplate(const std::string& base, const std::string& query) {
    std::string encoded = urlEncode(query);
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
}

static std::vector<std::string> extractOnionUrls(const std::string& text) {
    std::vector<std::string> urls;
    std::regex onionRegex("(https?://[a-z2-7]{56}\\.onion[^\\s\"<>]*)", std::regex::icase);
    std::sregex_iterator it(text.begin(), text.end(), onionRegex);
    std::sregex_iterator end;
    while (it != end) {
        urls.push_back((*it)[1].str());
        ++it;
    }
    return urls;
}

static std::string normalizeDirectUrl(const std::string& url) {
    if (url.find("://") != std::string::npos) return url;
    return "http://" + url;
}

std::vector<RoutedQuery> OnionSearchRouter::route(const std::string& query,
                                                  const QueryAnalysis& analysis,
                                                  const SearchConfig& config,
                                                  const DarknetEngines& engines) const {
    std::vector<RoutedQuery> routes;
    std::set<std::string> seen;
    
    auto pushRoute = [&](SearchEngine engine, const std::string& url, bool direct) {
        std::string normalized = normalizeUrl(url);
        if (normalized.empty()) return;
        if (!isUrlAllowedByRoutePolicy(normalized, config)) return;
        if (seen.insert(normalized).second) {
            routes.push_back({engine, url, direct});
        }
    };
    
    auto directFromQuery = extractOnionUrls(query);
    for (const auto& url : directFromQuery) {
        pushRoute(SearchEngine::CUSTOM, url, true);
    }
    
    if (analysis.type == QueryType::DIRECT_LINK && !directFromQuery.empty()) {
        return routes;
    }
    
    for (auto engine : config.darknetEngines) {
        std::string url = engines.buildSearchUrl(engine, query);
        if (!url.empty()) {
            pushRoute(engine, url, false);
        }
    }
    
    for (const auto& base : config.customDarknetUrls) {
        std::string url = applyTemplate(base, query);
        if (!url.empty()) {
            pushRoute(SearchEngine::CUSTOM, url, false);
        }
    }
    
    for (const auto& link : config.directOnionLinks) {
        if (!link.empty()) {
            pushRoute(SearchEngine::CUSTOM, normalizeDirectUrl(link), true);
        }
    }
    
    return routes;
}

}
}
