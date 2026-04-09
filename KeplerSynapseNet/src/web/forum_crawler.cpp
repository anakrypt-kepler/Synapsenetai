#include "web/web.h"
#include "web/connector_audit.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <set>
#include <sstream>

namespace synapse {
namespace web {

struct ForumSource {
    std::string name;
    std::string baseUrl;
    std::string searchTemplate;
};

static std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

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

static std::string applyTemplate(const std::string& base, const std::string& query) {
    if (base.empty()) return "";
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

ForumCrawler::ForumCrawler() = default;

std::vector<SearchResult> ForumCrawler::crawl(const std::string& query,
                                              TorFetch& fetcher,
                                              HtmlExtractor& extractor,
                                              size_t maxResults,
                                              const ExtractionRiskPolicy& riskPolicy,
                                              bool auditEnabled,
                                              const std::string& auditDir) {
    extractor.setRiskPolicy(riskPolicy);

    std::vector<ForumSource> sources = {
        {"Dread", "http://dreadytofatroptsdj6io7l3xptbet6onoyno2yv7jicoxknyazubrad.onion/d/DarkSearch", ""},
        {"NotEvil", "http://hss3uro2hsxfogfq.onion/", ""}
    };
    
    std::vector<SearchResult> results;
    std::set<std::string> seen;
    auto tokens = extractTokens(query);
    
    for (const auto& source : sources) {
        if (results.size() >= maxResults) break;
        std::string url = source.searchTemplate.empty() ? source.baseUrl : applyTemplate(source.searchTemplate, query);
        if (url.empty()) continue;
        std::string html = fetcher.fetch(url);
        if (html.empty()) continue;
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        auto content = extractor.extract(html, url);
        if (auditEnabled) {
            writeConnectorAuditArtifact(auditDir, url, html, content, now);
        }
        if (content.riskBlocked) continue;
        if (content.mainText.empty()) continue;
        std::string lower = toLower(content.mainText);
        bool match = tokens.empty();
        for (const auto& token : tokens) {
            if (lower.find(token) != std::string::npos) {
                match = true;
                break;
            }
        }
        if (!match) continue;
        SearchResult result;
        result.url = url;
        result.title = content.title.empty() ? source.name : content.title;
        result.domain = extractDomain(url);
        result.isOnion = isOnionUrl(url);
        result.timestamp = now;
        result.contentType = ContentType::TEXT;
        result.relevanceScore = 0.0;
        result.snippet = buildSnippet(content.mainText, tokens);
        std::string normalized = normalizeUrl(result.url);
        if (!normalized.empty() && seen.insert(normalized).second) {
            results.push_back(result);
        }
    }
    
    return results;
}

}
}
