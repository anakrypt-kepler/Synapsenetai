#include "core/text_usefulness.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace synapse::core {
namespace {

std::string lowerCopy(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string fieldAfter(const std::string& body, const char* key) {
    const std::string needle = std::string("\n") + key;
    const auto pos = body.find(needle);
    if (pos == std::string::npos) return {};
    const auto start = pos + needle.size();
    auto end = body.find('\n', start);
    if (end == std::string::npos) end = body.size();
    std::string v = body.substr(start, end - start);
    while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
    return lowerCopy(v);
}

std::string excerptOf(const std::string& body) {
    const auto pos = body.find("\ntopic: ");
    if (pos == std::string::npos) return body;
    return body.substr(0, pos);
}

std::string normalizeTopic(const std::string& in) {
    size_t begin = 0;
    size_t end = in.size();
    while (begin < end) {
        const unsigned char c = static_cast<unsigned char>(in[begin]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++begin;
    }
    while (end > begin) {
        const unsigned char c = static_cast<unsigned char>(in[end - 1]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        --end;
    }
    return lowerCopy(in.substr(begin, end - begin));
}

// Last "topic: <expected>" line that introduces the caller trailer ("url:" is
// the next line). An earlier topic line belongs to the page text.
size_t callerTopicTrailer(const std::string& body, const std::string& normalizedTopic) {
    if (normalizedTopic.empty()) return std::string::npos;
    const std::string needle = "\ntopic: ";
    size_t last = std::string::npos;
    size_t pos = 0;
    while ((pos = body.find(needle, pos)) != std::string::npos) {
        const size_t valueStart = pos + needle.size();
        const size_t lineEnd = body.find('\n', valueStart);
        if (lineEnd == std::string::npos) break;
        std::string value = body.substr(valueStart, lineEnd - valueStart);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '\t'))
            value.pop_back();
        const bool urlFollows = lineEnd + 1 < body.size()
            && body.compare(lineEnd + 1, 4, "url:") == 0;
        if (urlFollows && normalizeTopic(value) == normalizedTopic) last = pos;
        pos = valueStart;
    }
    return last;
}

std::string excerptBeforeCallerTopic(const std::string& body, const std::string& normalizedTopic) {
    const size_t pos = callerTopicTrailer(body, normalizedTopic);
    // No caller trailer: keep the previous excerpt bound so record metadata
    // is not counted as page text.
    if (pos == std::string::npos) return excerptOf(body);
    return body.substr(0, pos);
}

bool isWordChar(unsigned char c) {
    return std::isalnum(c) != 0;
}

void countWords(const std::string& text, uint16_t& words, uint16_t& unique, std::unordered_set<std::string>& bag) {
    words = 0;
    unique = 0;
    bag.clear();
    std::string cur;
    auto flush = [&]() {
        if (cur.size() < 3) {
            cur.clear();
            return;
        }
        if (words < 60000) ++words;
        if (bag.insert(cur).second && unique < 60000) ++unique;
        cur.clear();
    };
    for (unsigned char c : text) {
        if (isWordChar(c)) cur.push_back(static_cast<char>(std::tolower(c)));
        else flush();
    }
    flush();
}

uint16_t capCount(uint16_t v) {
    return v > 1000 ? 1000 : v;
}

bool containsAny(const std::string& hay, const char* const* needles) {
    for (const char* const* n = needles; *n; ++n) {
        if (hay.find(*n) != std::string::npos) return true;
    }
    return false;
}

bool termHit(const std::string& hay, const char* needle) {
    const size_t n = std::char_traits<char>::length(needle);
    if (n == 0) return false;
    // Short tokens must be whole words. "tor" is inside "history" and "directory".
    const bool bounded = n <= 4;
    for (size_t pos = 0; (pos = hay.find(needle, pos)) != std::string::npos; pos += n) {
        if (!bounded) return true;
        const bool left = pos == 0 || !isWordChar(static_cast<unsigned char>(hay[pos - 1]));
        const bool right = pos + n >= hay.size() || !isWordChar(static_cast<unsigned char>(hay[pos + n]));
        if (left && right) return true;
    }
    return false;
}

uint16_t topicHitsFor(const std::string& topic, const std::string& excerpt) {
    const char* crypto[] = {
        "cipher", "encrypt", "signature", "protocol", "hash", "bitcoin",
        "tls", "rsa", "aes", "cryptograph", "zero-knowledge", "key exchange", nullptr
    };
    const char* ai[] = {
        "neural", "dataset", "transformer", "inference", "training",
        "language model", "machine learning", "token", "weight", nullptr
    };
    const char* dark[] = {
        "tor", "onion", "anonym", "hidden service", "censorship",
        "relay", "circuit", "browser", "privacy", nullptr
    };
    const char* const* list = nullptr;
    if (topic.find("crypto") != std::string::npos || topic.find("cipher") != std::string::npos)
        list = crypto;
    else if (topic.find("ai") != std::string::npos || topic.find("machine") != std::string::npos)
        list = ai;
    else if (topic.find("dark") != std::string::npos || topic.find("tor") != std::string::npos ||
             topic.find("onion") != std::string::npos)
        list = dark;
    if (!list) return 0;
    uint16_t hits = 0;
    for (const char* const* n = list; *n; ++n) {
        if (termHit(excerpt, *n)) ++hits;
    }
    return hits;
}

size_t countNeedle(const std::string& hay, const char* needle) {
    size_t n = 0;
    for (size_t pos = 0; (pos = hay.find(needle, pos)) != std::string::npos; pos += std::char_traits<char>::length(needle))
        ++n;
    return n;
}

TextUsefulness assessFromParts(const std::string& title, const std::string& body,
                               const std::string& excerptRaw, const std::string& topic) {
    TextUsefulness out;
    out.harvest = isHarvestKnowledge(body);
    if (!out.harvest) {
        out.passed = true;
        out.reason = "not_harvest";
        out.words = 100;
        out.uniqueWords = 100;
        out.topicHits = 100;
        return out;
    }

    const std::string excerpt = lowerCopy(excerptRaw + "\n" + title);
    std::unordered_set<std::string> bag;
    countWords(excerpt, out.words, out.uniqueWords, bag);

    const bool shortPage = out.words < 80;
    static const char* errors[] = {
        "403. that's an error",
        "404. that's an error",
        "error 403 (forbidden)",
        "403 forbidden",
        "404 not found",
        "access denied",
        "enable javascript",
        "just a moment",
        "attention required",
        nullptr
    };
    if (shortPage && containsAny(excerpt, errors)) {
        out.reason = "error_page";
        return out;
    }
    if (countNeedle(excerpt, ".onion") >= 4) {
        out.reason = "link_catalog";
        return out;
    }
    if (out.words < 40 || out.uniqueWords < 20) {
        out.reason = "thin_page";
        return out;
    }

    out.topicHits = topicHitsFor(topic, excerpt);
    if (out.topicHits < 2) {
        out.reason = "topic_mismatch";
        return out;
    }

    out.passed = true;
    out.reason = "ok";
    return out;
}

} // namespace

bool isHarvestKnowledge(const std::string& body) {
    return body.find("\ntopic: ") != std::string::npos
        && body.find("\nurl: ") != std::string::npos
        && body.find("\nvia: ") != std::string::npos;
}

TextUsefulness assessTextUsefulness(const std::string& title, const std::string& body) {
    return assessFromParts(title, body, excerptOf(body), fieldAfter(body, "topic: "));
}

TextUsefulness assessTextUsefulness(const std::string& title, const std::string& body,
                                    const std::string& topic) {
    const std::string normalized = normalizeTopic(topic);
    return assessFromParts(title, body, excerptBeforeCallerTopic(body, normalized), normalized);
}

void bindUsefulnessVote(poe_v1::ValidationVoteV1& vote, const TextUsefulness& use) {
    vote.scores[0] = capCount(use.words);
    vote.scores[1] = use.topicHits;
    vote.scores[2] = capCount(use.uniqueWords);
}

bool usefulnessVoteMatches(const poe_v1::ValidationVoteV1& vote, const TextUsefulness& use) {
    return vote.scores[0] == capCount(use.words)
        && vote.scores[1] == use.topicHits
        && vote.scores[2] == capCount(use.uniqueWords);
}

} // namespace synapse::core
