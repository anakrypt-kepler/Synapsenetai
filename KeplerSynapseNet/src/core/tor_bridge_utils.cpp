#include "core/tor_bridge_utils.h"

#include "crypto/crypto.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <tuple>
#include <unordered_set>

namespace synapse::core {

namespace {

std::string trimAsciiWhitespace(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    if (start > 0) s.erase(0, start);
    return s;
}

std::vector<std::string> splitAsciiWhitespace(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) {
        out.push_back(token);
    }
    return out;
}

bool isAsciiHex(const std::string& s, size_t expectedLen) {
    if (s.size() != expectedLen) return false;
    for (char c : s) {
        const bool digit = c >= '0' && c <= '9';
        const bool upper = c >= 'A' && c <= 'F';
        const bool lower = c >= 'a' && c <= 'f';
        if (!digit && !upper && !lower) return false;
    }
    return true;
}

bool parsePortSuffix(const std::string& addressToken) {
    const auto colon = addressToken.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= addressToken.size()) return false;
    uint32_t port = 0;
    for (size_t i = colon + 1; i < addressToken.size(); ++i) {
        const char c = addressToken[i];
        if (c < '0' || c > '9') return false;
        port = port * 10u + static_cast<uint32_t>(c - '0');
        if (port > 65535u) return false;
    }
    return port >= 1u && port <= 65535u;
}

bool isUnsignedInteger(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

}

TorBridgeLineParseResult normalizeObfs4BridgeLine(const std::string& raw) {
    TorBridgeLineParseResult out;
    std::string line = trimAsciiWhitespace(raw);
    if (line.empty()) {
        out.status = TorBridgeLineParseStatus::Skip;
        return out;
    }
    if (!line.empty() && line.front() == '#') {
        out.status = TorBridgeLineParseStatus::Skip;
        return out;
    }
    if (line.rfind("obfs4 ", 0) == 0) {
        line = "Bridge " + line;
    }
    if (line.rfind("Bridge obfs4 ", 0) != 0) {
        out.status = TorBridgeLineParseStatus::Invalid;
        out.error = "expected 'Bridge obfs4 ...'";
        return out;
    }

    const auto tokens = splitAsciiWhitespace(line);
    if (tokens.size() < 6) {
        out.status = TorBridgeLineParseStatus::Invalid;
        out.error = "bridge line too short";
        return out;
    }
    if (tokens[0] != "Bridge" || tokens[1] != "obfs4") {
        out.status = TorBridgeLineParseStatus::Invalid;
        out.error = "expected 'Bridge obfs4'";
        return out;
    }
    if (!parsePortSuffix(tokens[2])) {
        out.status = TorBridgeLineParseStatus::Invalid;
        out.error = "invalid address:port";
        return out;
    }
    if (!isAsciiHex(tokens[3], 40)) {
        out.status = TorBridgeLineParseStatus::Invalid;
        out.error = "invalid bridge fingerprint";
        return out;
    }

    bool hasCert = false;
    bool hasIatMode = false;
    for (size_t i = 4; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (token.rfind("cert=", 0) == 0) {
            hasCert = token.size() > 5;
        } else if (token.rfind("iat-mode=", 0) == 0) {
            const std::string value = token.substr(9);
            if (!isUnsignedInteger(value)) {
                out.status = TorBridgeLineParseStatus::Invalid;
                out.error = "invalid iat-mode";
                return out;
            }
            hasIatMode = true;
        }
    }
    if (!hasCert) {
        out.status = TorBridgeLineParseStatus::Invalid;
        out.error = "missing cert=";
        return out;
    }
    if (!hasIatMode) {
        out.status = TorBridgeLineParseStatus::Invalid;
        out.error = "missing iat-mode=";
        return out;
    }

    std::ostringstream normalized;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) normalized << ' ';
        normalized << tokens[i];
    }
    out.status = TorBridgeLineParseStatus::Valid;
    out.normalized = normalized.str();
    return out;
}

std::vector<std::string> sanitizeAndDedupeObfs4BridgeLines(const std::vector<std::string>& rawLines) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    out.reserve(rawLines.size());
    for (const auto& raw : rawLines) {
        const auto parsed = normalizeObfs4BridgeLine(raw);
        if (parsed.status != TorBridgeLineParseStatus::Valid) continue;
        if (seen.insert(parsed.normalized).second) {
            out.push_back(parsed.normalized);
        }
    }
    return out;
}

std::vector<std::string> selectDeterministicObfs4BridgeSubset(const std::vector<std::string>& rawLines,
                                                              const std::string& nodeId,
                                                              uint64_t epoch,
                                                              size_t maxCount) {
    auto bridges = sanitizeAndDedupeObfs4BridgeLines(rawLines);
    if (bridges.empty() || maxCount == 0) return {};
    if (maxCount >= bridges.size()) return bridges;

    struct RankedBridge {
        crypto::Hash256 score;
        std::string line;
    };

    std::vector<RankedBridge> ranked;
    ranked.reserve(bridges.size());

    const std::string seedPrefix =
        "tor|bridge_subset|v1|node=" + nodeId + "|epoch=" + std::to_string(epoch) + "|";
    for (const auto& bridge : bridges) {
        RankedBridge item;
        item.line = bridge;
        item.score = crypto::sha256(seedPrefix + bridge);
        ranked.push_back(std::move(item));
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedBridge& a, const RankedBridge& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.line < b.line;
    });

    std::vector<std::string> out;
    out.reserve(maxCount);
    for (size_t i = 0; i < maxCount; ++i) {
        out.push_back(ranked[i].line);
    }
    return out;
}

std::vector<std::string> describeTorReadinessTransitions(const TorReadinessState& prev,
                                                         const TorReadinessState& next) {
    std::vector<std::string> events;

    if (prev.socksReachable != next.socksReachable) {
        events.push_back(next.socksReachable ? "SOCKS_UP" : "SOCKS_DOWN");
    }
    if (prev.webReady != next.webReady) {
        events.push_back(next.webReady ? "WEB_READY_GAINED" : "WEB_READY_LOST");
    }
    if (prev.degraded != next.degraded) {
        events.push_back(next.degraded ? "DEGRADED_ENTER" : "DEGRADED_EXIT");
    }

    return events;
}

}
