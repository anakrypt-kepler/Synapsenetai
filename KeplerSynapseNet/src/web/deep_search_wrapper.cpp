#include "web/web.h"
#include "web/connector_audit.h"
#include <regex>
#include <set>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <cctype>

namespace synapse {
namespace web {

static std::vector<std::string> extractTokens(const std::string& query) {
    std::vector<std::string> tokens;
    std::istringstream iss(query);
    std::string token;
    while (iss >> token) {
        std::string cleaned;
        for (char c : token) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                cleaned += std::tolower(static_cast<unsigned char>(c));
            }
        }
        if (cleaned.size() > 2) tokens.push_back(cleaned);
    }
    return tokens;
}

static std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

static std::string buildSnippet(const std::string& text, const std::vector<std::string>& tokens) {
    if (text.empty()) return "";
    std::string lower = toLower(text);
    size_t pos = std::string::npos;
    for (const auto& token : tokens) {
        size_t found = lower.find(token);
        if (found != std::string::npos) {
            pos = found;
            break;
        }
    }
    if (pos == std::string::npos) {
        return text.substr(0, 300);
    }
    size_t start = pos > 120 ? pos - 120 : 0;
    size_t end = std::min(text.size(), start + 300);
    return text.substr(start, end - start);
}

static std::vector<SearchResult> parseResultsFromHtml(const std::string& html,
                                                      const std::string& sourceUrl,
                                                      size_t maxResults,
                                                      const std::vector<std::string>& tokens) {
    std::vector<SearchResult> results;
    std::set<std::string> seen;
    std::regex linkRegex("<a[^>]*href=\"(https?://[^\"]+)\"[^>]*>([^<]*)</a>", std::regex::icase);
    std::sregex_iterator it(html.begin(), html.end(), linkRegex);
    std::sregex_iterator end;
    
    std::string sourceDomain = extractDomain(sourceUrl);
    
    while (it != end && results.size() < maxResults) {
        std::string url = (*it)[1].str();
        std::string title = (*it)[2].str();
        if (url.find("javascript:") != std::string::npos) {
            ++it;
            continue;
        }
        std::string normalized = normalizeUrl(url);
        if (normalized.empty() || seen.count(normalized)) {
            ++it;
            continue;
        }
        if (!tokens.empty()) {
            std::string lowerTitle = toLower(title);
            std::string lowerUrl = toLower(url);
            bool match = false;
            for (const auto& token : tokens) {
                if (lowerTitle.find(token) != std::string::npos || lowerUrl.find(token) != std::string::npos) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                ++it;
                continue;
            }
        }
        std::string domain = extractDomain(url);
        if (!sourceDomain.empty() && domain == sourceDomain) {
            ++it;
            continue;
        }
        seen.insert(normalized);
        SearchResult result;
        result.url = url;
        result.title = title.empty() ? domain : title;
        result.domain = domain;
        result.isOnion = isOnionUrl(url);
        result.timestamp = static_cast<uint64_t>(std::time(nullptr));
        result.contentType = ContentType::HTML;
        result.relevanceScore = 0.0;
        results.push_back(result);
        ++it;
    }
    
    return results;
}

DeepSearchWrapper::DeepSearchWrapper() {
    stats_ = Stats{};
}

std::vector<SearchResult> DeepSearchWrapper::search(const std::string& query,
                                                    const std::vector<RoutedQuery>& routes,
                                                    TorFetch& fetcher,
                                                    size_t maxResultsPerEngine,
                                                    const ExtractionRiskPolicy& riskPolicy,
                                                    bool auditEnabled,
                                                    const std::string& auditDir) {
    stats_ = Stats{};
    std::vector<SearchResult> results;
    std::set<std::string> seen;
    HtmlExtractor extractor;
    extractor.setRiskPolicy(riskPolicy);
    auto tokens = extractTokens(query);
    
    for (const auto& route : routes) {
        std::string html = fetcher.fetch(route.url);
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        stats_.requests++;
        if (html.empty()) {
            stats_.failures++;
            continue;
        }
        stats_.successes++;
        stats_.bytes += html.size();

        auto content = extractor.extract(html, route.url);
        if (auditEnabled) {
            writeConnectorAuditArtifact(auditDir, route.url, html, content, now);
        }
        if (content.riskBlocked) {
            stats_.failures++;
            continue;
        }
        
        if (route.direct) {
            SearchResult result;
            result.url = route.url;
            result.title = content.title.empty() ? extractDomain(route.url) : content.title;
            result.domain = extractDomain(route.url);
            result.isOnion = isOnionUrl(route.url);
            result.timestamp = now;
            result.contentType = ContentType::TEXT;
            result.relevanceScore = 0.0;
            if (!content.mainText.empty()) {
                result.snippet = buildSnippet(content.mainText, tokens);
            }
            std::string normalized = normalizeUrl(result.url);
            if (!normalized.empty() && seen.insert(normalized).second) {
                results.push_back(result);
            }
            continue;
        }
        
        auto parsed = parseResultsFromHtml(html, route.url, maxResultsPerEngine, tokens);
        for (const auto& item : parsed) {
            std::string normalized = normalizeUrl(item.url);
            if (normalized.empty() || seen.count(normalized)) continue;
            seen.insert(normalized);
            results.push_back(item);
        }
    }
    
    return results;
}

DeepSearchWrapper::Stats DeepSearchWrapper::getStats() const {
    return stats_;
}

}
}
