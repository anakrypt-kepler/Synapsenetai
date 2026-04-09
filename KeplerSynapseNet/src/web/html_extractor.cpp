#include "web/web.h"
#include <array>
#include <cmath>
#include <limits>
#include <regex>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stack>

namespace synapse {
namespace web {

namespace {

static uint32_t clampRiskU32(uint32_t value, uint32_t minValue, uint32_t maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static ExtractionRiskPolicy sanitizeRiskPolicy(const ExtractionRiskPolicy& policy) {
    ExtractionRiskPolicy out = policy;
    out.blockScoreThreshold = clampRiskU32(out.blockScoreThreshold, 1, 100);
    out.promptInjectionPenalty = clampRiskU32(out.promptInjectionPenalty, 0, 100);
    out.entropyPenalty = clampRiskU32(out.entropyPenalty, 0, 100);
    out.malformedHtmlPenalty = clampRiskU32(out.malformedHtmlPenalty, 0, 100);
    out.entropyMilliBitsThreshold = clampRiskU32(out.entropyMilliBitsThreshold, 1000, 8000);
    return out;
}

static std::string joinCsv(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out += ',';
        out += values[i];
    }
    return out;
}

static uint32_t estimateEntropyMilliBits(const std::string& input) {
    if (input.empty()) return 0;
    const size_t sampleSize = std::min<size_t>(input.size(), 65536);
    if (sampleSize == 0) return 0;

    std::array<uint32_t, 256> freq{};
    for (size_t i = 0; i < sampleSize; ++i) {
        ++freq[static_cast<uint8_t>(input[i])];
    }

    const double invN = 1.0 / static_cast<double>(sampleSize);
    double entropy = 0.0;
    for (uint32_t f : freq) {
        if (f == 0) continue;
        double p = static_cast<double>(f) * invN;
        entropy -= p * std::log2(p);
    }
    if (entropy < 0.0) entropy = 0.0;
    const double scaled = entropy * 1000.0;
    if (scaled >= static_cast<double>(std::numeric_limits<uint32_t>::max())) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(scaled + 0.5);
}

static bool isTagNameBoundary(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isspace(u) || c == '>' || c == '/' || c == '\0';
}

static void eraseTagBlocksCaseInsensitive(std::string& input, const std::string& tagName) {
    if (input.empty() || tagName.empty()) return;

    std::string lower = toLowerCopy(input);
    const std::string open = "<" + tagName;
    const std::string close = "</" + tagName + ">";

    size_t searchPos = 0;
    while (searchPos < lower.size()) {
        size_t openPos = lower.find(open, searchPos);
        if (openPos == std::string::npos) break;

        const size_t afterOpen = openPos + open.size();
        if (afterOpen < lower.size() && !isTagNameBoundary(lower[afterOpen])) {
            searchPos = afterOpen;
            continue;
        }

        size_t openEnd = lower.find('>', afterOpen);
        if (openEnd == std::string::npos) {
            input.erase(openPos);
            lower.erase(openPos);
            break;
        }

        size_t closePos = lower.find(close, openEnd + 1);
        size_t eraseEnd = (closePos == std::string::npos) ? lower.size() : (closePos + close.size());
        input.erase(openPos, eraseEnd - openPos);
        lower.erase(openPos, eraseEnd - openPos);
        searchPos = openPos;
    }
}

static void eraseCommentBlocks(std::string& input) {
    size_t pos = 0;
    while ((pos = input.find("<!--", pos)) != std::string::npos) {
        size_t end = input.find("-->", pos + 4);
        if (end == std::string::npos) {
            input.erase(pos);
            break;
        }
        input.erase(pos, (end + 3) - pos);
    }
}

}

struct HtmlExtractor::Impl {
    size_t maxTextLength = 50000;
    bool removeAds = true;
    bool removeScripts = true;
    bool removeStyles = true;
    bool removeNavigation = true;
    ExtractionRiskPolicy riskPolicy{};
    
    std::string stripTags(const std::string& html);
    std::string decodeEntities(const std::string& text);
    std::string normalizeWhitespace(const std::string& text);
    std::vector<std::string> findAllMatches(const std::string& html, const std::regex& pattern);
    std::string extractBetweenTags(const std::string& html, const std::string& tag);
    bool isAdElement(const std::string& element);
    bool isNavigationElement(const std::string& element);
    uint32_t scorePromptInjection(const std::string& html, std::vector<std::string>* signals) const;
    uint32_t scorePayloadEntropy(const std::string& html, std::vector<std::string>* signals) const;
    uint32_t scoreMalformedHtml(const std::string& html, std::vector<std::string>* signals) const;
    uint32_t scoreRisk(const std::string& html, std::vector<std::string>* signals) const;
};

std::string HtmlExtractor::Impl::stripTags(const std::string& html) {
    std::string result;
    bool inTag = false;
    bool inScript = false;
    bool inStyle = false;
    
    for (size_t i = 0; i < html.size(); i++) {
        if (html[i] == '<') {
            std::string tagStart = html.substr(i, 10);
            std::transform(tagStart.begin(), tagStart.end(), tagStart.begin(), ::tolower);
            
            if (tagStart.find("<script") == 0) inScript = true;
            if (tagStart.find("<style") == 0) inStyle = true;
            if (tagStart.find("</script") == 0) inScript = false;
            if (tagStart.find("</style") == 0) inStyle = false;
            
            inTag = true;
        } else if (html[i] == '>') {
            inTag = false;
        } else if (!inTag && !inScript && !inStyle) {
            result += html[i];
        }
    }
    
    return result;
}

std::string HtmlExtractor::Impl::decodeEntities(const std::string& text) {
    std::string result = text;
    
    std::vector<std::pair<std::string, std::string>> entities = {
        {"&nbsp;", " "}, {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"}, {"&#39;", "'"}, {"&#x27;", "'"},
        {"&mdash;", "-"}, {"&ndash;", "-"}, {"&hellip;", "..."},
        {"&copy;", "(c)"}, {"&reg;", "(R)"}, {"&trade;", "(TM)"},
        {"&laquo;", "<<"}, {"&raquo;", ">>"}, {"&bull;", "*"},
        {"&middot;", "*"}, {"&deg;", " degrees"}, {"&plusmn;", "+/-"}
    };
    
    for (const auto& [entity, replacement] : entities) {
        size_t pos = 0;
        while ((pos = result.find(entity, pos)) != std::string::npos) {
            result.replace(pos, entity.length(), replacement);
            pos += replacement.length();
        }
    }
    
    std::regex numericEntity("&#(\\d+);");
    std::smatch match;
    while (std::regex_search(result, match, numericEntity)) {
        int code = std::stoi(match[1].str());
        std::string replacement(1, static_cast<char>(code));
        result = match.prefix().str() + replacement + match.suffix().str();
    }
    
    return result;
}

std::string HtmlExtractor::Impl::normalizeWhitespace(const std::string& text) {
    std::string result;
    bool lastWasSpace = false;
    
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastWasSpace) {
                result += ' ';
                lastWasSpace = true;
            }
        } else {
            result += c;
            lastWasSpace = false;
        }
    }
    
    size_t start = result.find_first_not_of(' ');
    size_t end = result.find_last_not_of(' ');
    
    if (start == std::string::npos) return "";
    return result.substr(start, end - start + 1);
}

std::vector<std::string> HtmlExtractor::Impl::findAllMatches(const std::string& html, const std::regex& pattern) {
    std::vector<std::string> matches;
    std::sregex_iterator it(html.begin(), html.end(), pattern);
    std::sregex_iterator end;
    
    while (it != end) {
        matches.push_back((*it)[1].str());
        ++it;
    }
    
    return matches;
}

std::string HtmlExtractor::Impl::extractBetweenTags(const std::string& html, const std::string& tag) {
    std::string openTag = "<" + tag;
    std::string closeTag = "</" + tag + ">";
    
    size_t start = html.find(openTag);
    if (start == std::string::npos) return "";
    
    start = html.find('>', start);
    if (start == std::string::npos) return "";
    start++;
    
    size_t end = html.find(closeTag, start);
    if (end == std::string::npos) return "";
    
    return html.substr(start, end - start);
}

bool HtmlExtractor::Impl::isAdElement(const std::string& element) {
    std::string lower = element;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    std::vector<std::string> adIndicators = {
        "ad-", "ads-", "advertisement", "sponsor", "promo", "banner",
        "adsense", "advert", "commercial", "marketing"
    };
    
    for (const auto& indicator : adIndicators) {
        if (lower.find(indicator) != std::string::npos) return true;
    }
    
    return false;
}

bool HtmlExtractor::Impl::isNavigationElement(const std::string& element) {
    std::string lower = element;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    std::vector<std::string> navIndicators = {
        "nav", "menu", "sidebar", "footer", "header", "breadcrumb",
        "pagination", "toolbar", "topbar", "bottombar"
    };
    
    for (const auto& indicator : navIndicators) {
        if (lower.find(indicator) != std::string::npos) return true;
    }
    
    return false;
}

uint32_t HtmlExtractor::Impl::scorePromptInjection(const std::string& html,
                                                   std::vector<std::string>* signals) const {
    if (riskPolicy.promptInjectionPenalty == 0 || html.empty()) return 0;

    static const std::vector<std::string> indicators = {
        "ignore previous instructions",
        "ignore all previous",
        "system prompt",
        "developer message",
        "you are chatgpt",
        "reveal prompt",
        "do not follow",
        "bypass safety",
        "tool call",
        "execute command",
        "override policy",
        "jailbreak"
    };

    const std::string lower = toLowerCopy(html);
    uint32_t hits = 0;
    for (const auto& token : indicators) {
        if (lower.find(token) != std::string::npos) {
            ++hits;
        }
    }

    if (hits == 0) return 0;
    if (signals) signals->push_back("prompt_injection");

    uint64_t score = 8 + static_cast<uint64_t>(hits) * 8;
    if (score > riskPolicy.promptInjectionPenalty) {
        score = riskPolicy.promptInjectionPenalty;
    }
    if (score > 100) score = 100;
    return static_cast<uint32_t>(score);
}

uint32_t HtmlExtractor::Impl::scorePayloadEntropy(const std::string& html,
                                                  std::vector<std::string>* signals) const {
    if (riskPolicy.entropyPenalty == 0 || html.empty()) return 0;
    const uint32_t entropyMilli = estimateEntropyMilliBits(html);
    if (entropyMilli <= riskPolicy.entropyMilliBitsThreshold) return 0;

    if (signals) signals->push_back("payload_entropy_high");
    return riskPolicy.entropyPenalty;
}

uint32_t HtmlExtractor::Impl::scoreMalformedHtml(const std::string& html,
                                                 std::vector<std::string>* signals) const {
    if (riskPolicy.malformedHtmlPenalty == 0 || html.empty()) return 0;

    bool malformed = false;
    const size_t lt = static_cast<size_t>(std::count(html.begin(), html.end(), '<'));
    const size_t gt = static_cast<size_t>(std::count(html.begin(), html.end(), '>'));
    const size_t diff = (lt > gt) ? (lt - gt) : (gt - lt);
    const size_t allowedDiff = std::max<size_t>(8, html.size() / 200);
    if (diff > allowedDiff) {
        malformed = true;
    }

    const std::string lower = toLowerCopy(html);
    if (lower.find("<html") != std::string::npos && lower.find("</html") == std::string::npos) {
        malformed = true;
    }
    if (lower.find("<body") != std::string::npos && lower.find("</body") == std::string::npos) {
        malformed = true;
    }

    size_t controlCount = 0;
    for (unsigned char c : html) {
        if (c < 32 && c != '\n' && c != '\r' && c != '\t') {
            ++controlCount;
        }
    }
    if (!html.empty() && (controlCount * 100 > html.size() * 2)) {
        malformed = true;
    }

    if (!malformed) return 0;
    if (signals) signals->push_back("malformed_html");
    return riskPolicy.malformedHtmlPenalty;
}

uint32_t HtmlExtractor::Impl::scoreRisk(const std::string& html, std::vector<std::string>* signals) const {
    if (!riskPolicy.enabled) return 0;
    uint64_t score = 0;
    score += scorePromptInjection(html, signals);
    score += scorePayloadEntropy(html, signals);
    score += scoreMalformedHtml(html, signals);
    if (score > 100) score = 100;
    return static_cast<uint32_t>(score);
}

HtmlExtractor::HtmlExtractor() : impl_(std::make_unique<Impl>()) {}
HtmlExtractor::~HtmlExtractor() = default;

ExtractedContent HtmlExtractor::extract(const std::string& html, const std::string& url) {
    ExtractedContent content;
    content.originalSize = html.size();
    
    content.title = extractTitle(html);
    content.mainText = extractMainText(html);
    content.codeBlocks = extractCodeBlocks(html);
    content.onionLinks = extractOnionLinks(html);
    content.clearnetLinks = extractLinks(html);
    content.metadata = extractMetadata(html);
    
    if (!url.empty()) {
        content.metadata["source_url"] = url;
        content.metadata["domain"] = extractDomain(url);
    }

    content.riskScore = impl_->scoreRisk(html, &content.riskSignals);
    content.riskBlocked = impl_->riskPolicy.enabled && content.riskScore >= impl_->riskPolicy.blockScoreThreshold;
    content.metadata["risk_score"] = std::to_string(content.riskScore);
    content.metadata["risk_blocked"] = content.riskBlocked ? "1" : "0";
    if (!content.riskSignals.empty()) {
        content.metadata["risk_signals"] = joinCsv(content.riskSignals);
    }

    if (content.riskBlocked) {
        content.mainText.clear();
        content.codeBlocks.clear();
        content.onionLinks.clear();
        content.clearnetLinks.clear();
    }
    
    if (!content.riskBlocked && content.mainText.size() > impl_->maxTextLength) {
        content.mainText = content.mainText.substr(0, impl_->maxTextLength);
        content.truncated = true;
    }
    
    content.extractedSize = content.title.size() + content.mainText.size();
    for (const auto& code : content.codeBlocks) {
        content.extractedSize += code.size();
    }
    
    return content;
}

std::string HtmlExtractor::extractTitle(const std::string& html) {
    std::regex titleRegex("<title[^>]*>([^<]+)</title>", std::regex::icase);
    std::smatch match;
    
    if (std::regex_search(html, match, titleRegex)) {
        return impl_->normalizeWhitespace(impl_->decodeEntities(match[1].str()));
    }
    
    std::regex h1Regex("<h1[^>]*>([^<]+)</h1>", std::regex::icase);
    if (std::regex_search(html, match, h1Regex)) {
        return impl_->normalizeWhitespace(impl_->decodeEntities(match[1].str()));
    }
    
    std::regex ogTitleRegex("<meta[^>]*property=\"og:title\"[^>]*content=\"([^\"]+)\"", std::regex::icase);
    if (std::regex_search(html, match, ogTitleRegex)) {
        return impl_->normalizeWhitespace(impl_->decodeEntities(match[1].str()));
    }
    
    return "";
}

std::string HtmlExtractor::extractMainText(const std::string& html) {
    std::string processed = html;

    // Avoid std::regex on untrusted remote HTML to prevent catastrophic backtracking/stack overflows.
    if (impl_->removeScripts) eraseTagBlocksCaseInsensitive(processed, "script");
    if (impl_->removeStyles) eraseTagBlocksCaseInsensitive(processed, "style");
    if (impl_->removeNavigation) {
        eraseTagBlocksCaseInsensitive(processed, "nav");
        eraseTagBlocksCaseInsensitive(processed, "header");
        eraseTagBlocksCaseInsensitive(processed, "footer");
    }
    eraseCommentBlocks(processed);

    std::string text = impl_->stripTags(processed);
    text = impl_->decodeEntities(text);
    text = impl_->normalizeWhitespace(text);

    return text;
}

std::vector<std::string> HtmlExtractor::extractCodeBlocks(const std::string& html) {
    std::vector<std::string> codeBlocks;
    
    std::regex preRegex("<pre[^>]*>([\\s\\S]*?)</pre>", std::regex::icase);
    auto preMatches = impl_->findAllMatches(html, preRegex);
    for (const auto& match : preMatches) {
        std::string code = impl_->stripTags(match);
        code = impl_->decodeEntities(code);
        if (!code.empty()) codeBlocks.push_back(code);
    }
    
    std::regex codeRegex("<code[^>]*>([\\s\\S]*?)</code>", std::regex::icase);
    auto codeMatches = impl_->findAllMatches(html, codeRegex);
    for (const auto& match : codeMatches) {
        std::string code = impl_->stripTags(match);
        code = impl_->decodeEntities(code);
        if (!code.empty() && code.size() > 20) codeBlocks.push_back(code);
    }
    
    return codeBlocks;
}

std::vector<std::string> HtmlExtractor::extractLinks(const std::string& html) {
    std::vector<std::string> links;
    std::regex linkRegex("href=\"(https?://[^\"]+)\"", std::regex::icase);
    
    std::sregex_iterator it(html.begin(), html.end(), linkRegex);
    std::sregex_iterator end;
    
    while (it != end) {
        std::string url = (*it)[1].str();
        if (!isOnionUrl(url)) {
            links.push_back(url);
        }
        ++it;
    }
    
    std::sort(links.begin(), links.end());
    links.erase(std::unique(links.begin(), links.end()), links.end());
    
    return links;
}

std::vector<std::string> HtmlExtractor::extractOnionLinks(const std::string& html) {
    std::vector<std::string> onionLinks;
    std::regex onionRegex("(https?://[a-z2-7]{56}\\.onion[^\"\\s<>]*)", std::regex::icase);
    
    std::sregex_iterator it(html.begin(), html.end(), onionRegex);
    std::sregex_iterator end;
    
    while (it != end) {
        onionLinks.push_back((*it)[1].str());
        ++it;
    }
    
    std::sort(onionLinks.begin(), onionLinks.end());
    onionLinks.erase(std::unique(onionLinks.begin(), onionLinks.end()), onionLinks.end());
    
    return onionLinks;
}

std::map<std::string, std::string> HtmlExtractor::extractMetadata(const std::string& html) {
    std::map<std::string, std::string> metadata;
    
    std::regex metaRegex("<meta[^>]*name=\"([^\"]+)\"[^>]*content=\"([^\"]+)\"", std::regex::icase);
    std::sregex_iterator it(html.begin(), html.end(), metaRegex);
    std::sregex_iterator end;
    
    while (it != end) {
        std::string name = (*it)[1].str();
        std::string content = (*it)[2].str();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        metadata[name] = impl_->decodeEntities(content);
        ++it;
    }
    
    std::regex ogRegex("<meta[^>]*property=\"og:([^\"]+)\"[^>]*content=\"([^\"]+)\"", std::regex::icase);
    it = std::sregex_iterator(html.begin(), html.end(), ogRegex);
    
    while (it != end) {
        std::string name = "og:" + (*it)[1].str();
        std::string content = (*it)[2].str();
        metadata[name] = impl_->decodeEntities(content);
        ++it;
    }
    
    std::regex langRegex("<html[^>]*lang=\"([^\"]+)\"", std::regex::icase);
    std::smatch langMatch;
    if (std::regex_search(html, langMatch, langRegex)) {
        metadata["language"] = langMatch[1].str();
    }
    
    return metadata;
}

void HtmlExtractor::setMaxTextLength(size_t length) {
    impl_->maxTextLength = length;
}

void HtmlExtractor::setRemoveAds(bool remove) {
    impl_->removeAds = remove;
}

void HtmlExtractor::setRemoveScripts(bool remove) {
    impl_->removeScripts = remove;
}

void HtmlExtractor::setRemoveStyles(bool remove) {
    impl_->removeStyles = remove;
}

void HtmlExtractor::setRemoveNavigation(bool remove) {
    impl_->removeNavigation = remove;
}

void HtmlExtractor::setRiskPolicy(const ExtractionRiskPolicy& policy) {
    impl_->riskPolicy = sanitizeRiskPolicy(policy);
}

ExtractionRiskPolicy HtmlExtractor::getRiskPolicy() const {
    return impl_->riskPolicy;
}

std::string HtmlExtractor::cleanHtml(const std::string& html) {
    std::string cleaned = html;

    eraseTagBlocksCaseInsensitive(cleaned, "script");
    eraseTagBlocksCaseInsensitive(cleaned, "style");
    eraseCommentBlocks(cleaned);

    std::string compact;
    compact.reserve(cleaned.size());
    bool lastWasSpace = false;
    for (char c : cleaned) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastWasSpace) {
                compact.push_back(' ');
                lastWasSpace = true;
            }
        } else {
            compact.push_back(c);
            lastWasSpace = false;
        }
    }
    cleaned.swap(compact);

    return cleaned;
}

std::string HtmlExtractor::htmlToText(const std::string& html) {
    std::string text = impl_->stripTags(html);
    text = impl_->decodeEntities(text);
    text = impl_->normalizeWhitespace(text);
    return text;
}

}
}
