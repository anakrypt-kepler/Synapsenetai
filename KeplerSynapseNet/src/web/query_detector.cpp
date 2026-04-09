#include "web/web.h"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <set>
#include <cmath>
#include <regex>

namespace synapse {
namespace web {

struct QueryDetector::Impl {
    std::vector<std::string> darknetKeywords;
    std::vector<std::string> clearnetKeywords;
    double darknetThreshold = 0.3;
    double clearnetThreshold = 0.3;
    
    void initDefaultKeywords();
    double calculateKeywordScore(const std::string& query, const std::vector<std::string>& keywords);
    std::vector<std::string> tokenize(const std::string& text);
};

void QueryDetector::Impl::initDefaultKeywords() {
    darknetKeywords = {
        "darknet", "dark web", "tor", "onion", ".onion",
        "dread", "hidden service", "hidden wiki",
        "exploit", "vulnerability", "0day", "zero-day",
        "search on tor", "find on darknet", "darkweb",
        "underground", "black market", "deep web",
        "anonymous", "anonymity", "untraceable",
        "leak", "leaked", "dump", "breach",
        "hacking", "hack", "cracking", "crack",
        "malware", "ransomware", "botnet",
        "carding", "fraud", "scam",
        "drugs", "weapons", "illegal"
    };
    
    clearnetKeywords = {
        "search", "google", "find online",
        "latest news", "current", "recent",
        "official website", "documentation", "docs",
        "wikipedia", "github", "stackoverflow",
        "tutorial", "guide", "how to",
        "download", "install", "setup",
        "review", "comparison", "best",
        "price", "buy", "shop", "store",
        "weather", "news", "sports",
        "video", "image", "photo"
    };
}

double QueryDetector::Impl::calculateKeywordScore(const std::string& query, 
                                                   const std::vector<std::string>& keywords) {
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    auto tokens = tokenize(lowerQuery);
    std::set<std::string> tokenSet(tokens.begin(), tokens.end());

    double score = 0.0;
    int matches = 0;

    for (const auto& keyword : keywords) {
        std::string lowerKeyword = keyword;
        std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::tolower);
        if (lowerKeyword.find(' ') != std::string::npos) {
            std::regex phraseRe("\\b" + lowerKeyword + "\\b");
            if (std::regex_search(lowerQuery, phraseRe)) {
                double keywordWeight = 1.0 + (keyword.length() / 10.0);
                score += keywordWeight;
                matches++;
            }
        } else {
            if (tokenSet.find(lowerKeyword) != tokenSet.end()) {
                double keywordWeight = 1.0 + (keyword.length() / 10.0);
                score += keywordWeight;
                matches++;
            }
        }
    }

    if (matches == 0) return 0.0;
    double normalizedScore = score / (1.0 + std::log(1.0 + tokens.size()));
    return std::min(1.0, normalizedScore);
}

std::vector<std::string> QueryDetector::Impl::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string token;
    
    while (iss >> token) {
        std::string cleaned;
        for (char c : token) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                cleaned += std::tolower(static_cast<unsigned char>(c));
            }
        }
        if (!cleaned.empty()) {
            tokens.push_back(cleaned);
        }
    }
    
    return tokens;
}

QueryDetector::QueryDetector() : impl_(std::make_unique<Impl>()) {
    impl_->initDefaultKeywords();
}

QueryDetector::~QueryDetector() = default;

QueryAnalysis QueryDetector::analyze(const std::string& query) {
    QueryAnalysis analysis;
    analysis.normalizedQuery = normalizeQuery(query);
    analysis.keywords = extractKeywords(query);
    
    analysis.darknetConfidence = impl_->calculateKeywordScore(query, impl_->darknetKeywords);
    analysis.clearnetConfidence = impl_->calculateKeywordScore(query, impl_->clearnetKeywords);
    
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    auto tokens = impl_->tokenize(lowerQuery);
    std::set<std::string> tokenSet(tokens.begin(), tokens.end());

    for (const auto& keyword : impl_->darknetKeywords) {
        std::string lower = keyword;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find(' ') != std::string::npos) {
            std::regex phraseRe("\\b" + lower + "\\b");
            if (std::regex_search(lowerQuery, phraseRe)) analysis.darknetTriggers.push_back(keyword);
        } else {
            if (tokenSet.find(lower) != tokenSet.end()) analysis.darknetTriggers.push_back(keyword);
        }
    }

    for (const auto& keyword : impl_->clearnetKeywords) {
        std::string lower = keyword;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find(' ') != std::string::npos) {
            std::regex phraseRe("\\b" + lower + "\\b");
            if (std::regex_search(lowerQuery, phraseRe)) analysis.clearnetTriggers.push_back(keyword);
        } else {
            if (tokenSet.find(lower) != tokenSet.end()) analysis.clearnetTriggers.push_back(keyword);
        }
    }
    
    analysis.type = detectType(query);
    analysis.requiresBoth = (analysis.darknetConfidence > impl_->darknetThreshold && 
                             analysis.clearnetConfidence > impl_->clearnetThreshold);
    
    if (lowerQuery.find("search everywhere") != std::string::npos ||
        lowerQuery.find("full search") != std::string::npos ||
        lowerQuery.find("compare sources") != std::string::npos ||
        lowerQuery.find("all information") != std::string::npos) {
        analysis.requiresBoth = true;
        analysis.type = QueryType::BOTH;
    }
    
    return analysis;
}

QueryType QueryDetector::detectType(const std::string& query) {
    double darknetScore = impl_->calculateKeywordScore(query, impl_->darknetKeywords);
    double clearnetScore = impl_->calculateKeywordScore(query, impl_->clearnetKeywords);
    
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    if (lowerQuery.find(".onion") != std::string::npos) {
        return QueryType::DIRECT_LINK;
    }
    
    if (darknetScore > impl_->darknetThreshold && clearnetScore > impl_->clearnetThreshold) {
        return QueryType::BOTH;
    }
    
    if (darknetScore > impl_->darknetThreshold) {
        return QueryType::DARKNET;
    }
    
    if (clearnetScore > impl_->clearnetThreshold) {
        return QueryType::CLEARNET;
    }
    
    return QueryType::CLEARNET;
}

void QueryDetector::addDarknetKeyword(const std::string& keyword) {
    impl_->darknetKeywords.push_back(keyword);
}

void QueryDetector::addClearnetKeyword(const std::string& keyword) {
    impl_->clearnetKeywords.push_back(keyword);
}

void QueryDetector::removeDarknetKeyword(const std::string& keyword) {
    auto& keywords = impl_->darknetKeywords;
    keywords.erase(std::remove(keywords.begin(), keywords.end(), keyword), keywords.end());
}

void QueryDetector::removeClearnetKeyword(const std::string& keyword) {
    auto& keywords = impl_->clearnetKeywords;
    keywords.erase(std::remove(keywords.begin(), keywords.end(), keyword), keywords.end());
}

std::vector<std::string> QueryDetector::getDarknetKeywords() const {
    return impl_->darknetKeywords;
}

std::vector<std::string> QueryDetector::getClearnetKeywords() const {
    return impl_->clearnetKeywords;
}

void QueryDetector::setDarknetThreshold(double threshold) {
    impl_->darknetThreshold = std::max(0.0, std::min(1.0, threshold));
}

void QueryDetector::setClearnetThreshold(double threshold) {
    impl_->clearnetThreshold = std::max(0.0, std::min(1.0, threshold));
}

bool QueryDetector::isDarknetQuery(const std::string& query) {
    return impl_->calculateKeywordScore(query, impl_->darknetKeywords) > impl_->darknetThreshold;
}

bool QueryDetector::isClearnetQuery(const std::string& query) {
    return impl_->calculateKeywordScore(query, impl_->clearnetKeywords) > impl_->clearnetThreshold;
}

bool QueryDetector::requiresBothNetworks(const std::string& query) {
    return isDarknetQuery(query) && isClearnetQuery(query);
}

std::string QueryDetector::normalizeQuery(const std::string& query) {
    std::string normalized;
    bool lastWasSpace = false;
    
    for (char c : query) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastWasSpace) {
                normalized += ' ';
                lastWasSpace = true;
            }
        } else {
            normalized += std::tolower(static_cast<unsigned char>(c));
            lastWasSpace = false;
        }
    }
    
    size_t start = normalized.find_first_not_of(' ');
    size_t end = normalized.find_last_not_of(' ');
    
    if (start == std::string::npos) return "";
    return normalized.substr(start, end - start + 1);
}

std::vector<std::string> QueryDetector::extractKeywords(const std::string& query) {
    std::set<std::string> stopWords = {
        "a", "an", "the", "is", "are", "was", "were", "be", "been", "being",
        "have", "has", "had", "do", "does", "did", "will", "would", "could",
        "should", "may", "might", "must", "shall", "can", "need", "dare",
        "ought", "used", "to", "of", "in", "for", "on", "with", "at", "by",
        "from", "up", "about", "into", "over", "after", "beneath", "under",
        "above", "and", "but", "or", "nor", "so", "yet", "both", "either",
        "neither", "not", "only", "own", "same", "than", "too", "very",
        "just", "also", "now", "here", "there", "when", "where", "why",
        "how", "all", "each", "every", "both", "few", "more", "most",
        "other", "some", "such", "no", "any", "what", "which", "who"
    };
    
    auto tokens = impl_->tokenize(query);
    std::vector<std::string> keywords;
    
    for (const auto& token : tokens) {
        if (token.length() > 2 && stopWords.find(token) == stopWords.end()) {
            keywords.push_back(token);
        }
    }
    
    return keywords;
}

}
}
