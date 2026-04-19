#include "web/web.h"
#include <regex>
#include <set>
#include <cstdio>

namespace synapse {
namespace web {

struct KnownOnionSite {
    const char* hostSubstring;
    const char* searchTemplate;
};

static const KnownOnionSite knownOnionSites[] = {
    {"piratebay",  "/search.php?q={query}"},
    {"galaxy3",    "/search?q={query}"},
    {"breached",   "/search?q={query}"},
    {"dread",      "/search?q={query}"},
    {"breachforums", "/search?q={query}"},
};
static constexpr size_t knownOnionSiteCount = sizeof(knownOnionSites) / sizeof(knownOnionSites[0]);

static std::string extractOnionHost(const std::string& url) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return "";
    size_t hostStart = schemeEnd + 3;
    size_t hostEnd = url.find('/', hostStart);
    if (hostEnd == std::string::npos) hostEnd = url.size();
    auto portPos = url.find(':', hostStart);
    if (portPos != std::string::npos && portPos < hostEnd) hostEnd = portPos;
    return url.substr(hostStart, hostEnd - hostStart);
}

static std::string resolveKnownSearchPath(const std::string& url) {
    std::string host = extractOnionHost(url);
    if (host.empty()) return "";
    std::string lowerHost = host;
    std::transform(lowerHost.begin(), lowerHost.end(), lowerHost.begin(), ::tolower);
    for (size_t i = 0; i < knownOnionSiteCount; ++i) {
        if (lowerHost.find(knownOnionSites[i].hostSubstring) != std::string::npos) {
            return knownOnionSites[i].searchTemplate;
        }
    }
    return "";
}

static std::string baseOrigin(const std::string& url) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return url;
    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos) return url;
    return url.substr(0, pathStart);
}

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
    std::string knownPath = resolveKnownSearchPath(base);
    if (!knownPath.empty()) {
        std::string origin = baseOrigin(base);
        std::string tpl = origin + knownPath;
        pos = tpl.find("{query}");
        if (pos != std::string::npos) {
            tpl.replace(pos, 7, encoded);
        }
        return tpl;
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
