#include "core/tor_bridge_provider.h"

#include "core/tor_bridge_utils.h"
#include "crypto/crypto.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

namespace synapse::core {

namespace {

using json = nlohmann::json;

std::string trimAscii(std::string s) {
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

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

void setError(std::string* out, const std::string& value) {
    if (out) *out = value;
}

std::vector<std::string> readLinesFromFile(const std::string& path, std::string* errorOut) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.is_open()) {
        setError(errorOut, "open_failed");
        return {};
    }
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    if (file.bad()) {
        setError(errorOut, "read_failed");
        return {};
    }
    return lines;
}

std::vector<std::string> fallbackChainForSource(const std::string& requested) {
    if (requested == "remote") return {"remote", "cache", "file", "manual"};
    if (requested == "cache") return {"cache", "file", "manual"};
    if (requested == "file") return {"file", "cache", "manual"};
    return {"manual"};
}

bool validateResolvedPool(const std::vector<std::string>& lines,
                         uint32_t minPoolSize,
                         std::string& reasonCode,
                         std::string& error) {
    if (lines.empty()) {
        reasonCode = "empty_bridge_pool";
        error = "bridge pool is empty";
        return false;
    }
    if (minPoolSize == 0) minPoolSize = 1;
    if (lines.size() < minPoolSize) {
        reasonCode = "bridge_pool_too_small";
        error = "bridge pool smaller than min_pool_size";
        return false;
    }
    return true;
}

bool maybeWriteCache(const std::string& cachePath,
                     uint64_t now,
                     uint64_t ttlSeconds,
                     const std::vector<std::string>& lines,
                     bool& cacheUpdated,
                     std::string& cacheWriteError) {
    if (cachePath.empty() || lines.empty()) return true;
    TorBridgePoolCache cache;
    cache.transport = "obfs4";
    cache.savedAt = now;
    cache.ttlSeconds = ttlSeconds;
    cache.lines = lines;
    if (!saveTorBridgePoolCacheFile(cache, cachePath, &cacheWriteError)) {
        return false;
    }
    cacheUpdated = true;
    return true;
}

}

std::string normalizeTorBridgeProviderSource(std::string source) {
    source = toLowerAscii(trimAscii(std::move(source)));
    if (source == "manual" || source == "inline" || source == "lines") return "manual";
    if (source == "file" || source == "local_file" || source == "manual_file") return "file";
    if (source == "cache" || source == "cached") return "cache";
    if (source == "remote" || source == "http" || source == "https") return "remote";
    return "manual";
}

std::string computeObfs4BridgePoolChecksum(const std::vector<std::string>& rawLines) {
    auto lines = sanitizeAndDedupeObfs4BridgeLines(rawLines);
    if (lines.empty()) return "";
    std::sort(lines.begin(), lines.end());
    std::ostringstream payload;
    payload << "tor|bridge_pool|obfs4|v1\n";
    for (const auto& line : lines) {
        payload << line << '\n';
    }
    return crypto::toHex(crypto::sha256(payload.str()));
}

bool isTorBridgePoolCacheFresh(const TorBridgePoolCache& cache, uint64_t now) {
    if (cache.version != 1) return false;
    if (cache.transport != "obfs4") return false;
    if (cache.lines.empty()) return false;
    if (cache.ttlSeconds == 0) return false;
    if (cache.savedAt == 0) return false;
    if (now < cache.savedAt) return false;
    return (now - cache.savedAt) <= cache.ttlSeconds;
}

bool loadTorBridgePoolCacheFile(const std::string& path,
                                TorBridgePoolCache& out,
                                std::string* errorOut) {
    if (path.empty()) {
        setError(errorOut, "empty_path");
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        setError(errorOut, "open_failed");
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const std::exception&) {
        setError(errorOut, "parse_failed");
        return false;
    }

    TorBridgePoolCache cache;
    cache.version = j.value("version", 1u);
    cache.transport = j.value("transport", std::string("obfs4"));
    cache.savedAt = j.value("savedAt", static_cast<uint64_t>(0));
    cache.ttlSeconds = j.value("ttlSeconds", static_cast<uint64_t>(0));
    cache.checksum = trimAscii(j.value("checksum", std::string()));

    if (!j.contains("lines") || !j["lines"].is_array()) {
        setError(errorOut, "lines_missing");
        return false;
    }
    for (const auto& item : j["lines"]) {
        if (!item.is_string()) continue;
        cache.lines.push_back(item.get<std::string>());
    }
    cache.lines = sanitizeAndDedupeObfs4BridgeLines(cache.lines);
    if (cache.lines.empty()) {
        setError(errorOut, "empty_bridge_pool");
        return false;
    }
    if (cache.transport != "obfs4") {
        setError(errorOut, "unsupported_transport");
        return false;
    }

    const std::string computed = computeObfs4BridgePoolChecksum(cache.lines);
    if (computed.empty()) {
        setError(errorOut, "checksum_empty");
        return false;
    }
    if (!cache.checksum.empty() && cache.checksum != computed) {
        setError(errorOut, "checksum_mismatch");
        return false;
    }
    cache.checksum = computed;
    out = std::move(cache);
    return true;
}

bool saveTorBridgePoolCacheFile(const TorBridgePoolCache& input,
                                const std::string& path,
                                std::string* errorOut) {
    if (path.empty()) {
        setError(errorOut, "empty_path");
        return false;
    }

    TorBridgePoolCache cache = input;
    cache.version = 1;
    cache.transport = "obfs4";
    cache.lines = sanitizeAndDedupeObfs4BridgeLines(cache.lines);
    cache.checksum = computeObfs4BridgePoolChecksum(cache.lines);
    if (cache.lines.empty() || cache.checksum.empty()) {
        setError(errorOut, "empty_bridge_pool");
        return false;
    }

    try {
        const auto p = std::filesystem::path(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (const std::exception&) {
        setError(errorOut, "mkdir_failed");
        return false;
    }

    json j;
    j["version"] = cache.version;
    j["transport"] = cache.transport;
    j["savedAt"] = cache.savedAt;
    j["ttlSeconds"] = cache.ttlSeconds;
    j["checksum"] = cache.checksum;
    j["lines"] = cache.lines;

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        setError(errorOut, "open_failed");
        return false;
    }
    file << j.dump(2) << "\n";
    if (!file.good()) {
        setError(errorOut, "write_failed");
        return false;
    }
    return true;
}

TorBridgeProviderResolveResult resolveObfs4BridgePool(
    const TorBridgeProviderResolveInput& input,
    const TorBridgeRemoteFetchFn& remoteFetch) {
    TorBridgeProviderResolveResult result;
    result.ok = false;

    const std::string requestedSource = normalizeTorBridgeProviderSource(input.requestedSource);
    const std::string transport = toLowerAscii(trimAscii(input.transport));
    if (transport != "obfs4") {
        result.reasonCode = "unsupported_transport";
        result.error = "only obfs4 bridge pools are supported";
        return result;
    }

    const uint64_t now = input.now;
    const uint64_t cacheTtl = input.cacheTtlSeconds;
    const uint32_t minPoolSize = (input.minPoolSize == 0) ? 1u : input.minPoolSize;
    const auto chain = fallbackChainForSource(requestedSource);

    auto finalizeSuccess = [&](const std::string& source,
                               const std::vector<std::string>& sanitizedLines,
                               bool cacheHit,
                               bool cacheFresh,
                               uint64_t cacheSavedAt) {
        result.ok = true;
        result.source = source;
        result.lines = sanitizedLines;
        result.checksum = computeObfs4BridgePoolChecksum(result.lines);
        result.usedFallback = (source != requestedSource);
        result.cacheHit = cacheHit;
        result.cacheFresh = cacheFresh;
        result.cacheSavedAt = cacheSavedAt;
        result.reasonCode = "ok";
        result.error.clear();
    };

    std::string lastReason = "bridge_source_unresolved";
    std::string lastError = "no bridge source yielded a valid pool";

    for (const auto& source : chain) {
        result.triedSources.push_back(source);

        if (source == "manual") {
            const auto lines = sanitizeAndDedupeObfs4BridgeLines(input.manualLines);
            std::string reason;
            std::string error;
            if (!validateResolvedPool(lines, minPoolSize, reason, error)) {
                lastReason = "manual_" + reason;
                lastError = error;
                continue;
            }
            std::string cacheWriteError;
            if (!maybeWriteCache(input.cachePath, now, cacheTtl, lines, result.cacheUpdated, cacheWriteError)) {
                lastReason = "cache_write_failed";
                lastError = cacheWriteError;
            }
            finalizeSuccess("manual", lines, false, false, 0);
            return result;
        }

        if (source == "file") {
            std::string readError;
            const auto raw = readLinesFromFile(input.filePath, &readError);
            if (raw.empty()) {
                lastReason = "file_" + (readError.empty() ? std::string("empty") : readError);
                lastError = "failed to load bridge file";
                continue;
            }
            const auto lines = sanitizeAndDedupeObfs4BridgeLines(raw);
            std::string reason;
            std::string error;
            if (!validateResolvedPool(lines, minPoolSize, reason, error)) {
                lastReason = "file_" + reason;
                lastError = error;
                continue;
            }
            std::string cacheWriteError;
            if (!maybeWriteCache(input.cachePath, now, cacheTtl, lines, result.cacheUpdated, cacheWriteError)) {
                lastReason = "cache_write_failed";
                lastError = cacheWriteError;
            }
            finalizeSuccess("file", lines, false, false, 0);
            return result;
        }

        if (source == "cache") {
            TorBridgePoolCache cache;
            std::string cacheError;
            if (!loadTorBridgePoolCacheFile(input.cachePath, cache, &cacheError)) {
                lastReason = "cache_" + (cacheError.empty() ? std::string("load_failed") : cacheError);
                lastError = "failed to load bridge cache";
                continue;
            }
            const bool fresh = isTorBridgePoolCacheFresh(cache, now);
            if (!fresh && !input.allowStaleCacheFallback) {
                lastReason = "cache_stale";
                lastError = "bridge cache is stale";
                continue;
            }
            std::string reason;
            std::string error;
            if (!validateResolvedPool(cache.lines, minPoolSize, reason, error)) {
                lastReason = "cache_" + reason;
                lastError = error;
                continue;
            }
            finalizeSuccess("cache", cache.lines, true, fresh, cache.savedAt);
            return result;
        }

        if (source == "remote") {
            if (trimAscii(input.remoteUrl).empty()) {
                lastReason = "remote_url_missing";
                lastError = "bridge remote URL is empty";
                continue;
            }
            if (!remoteFetch) {
                lastReason = "remote_adapter_unavailable";
                lastError = "remote bridge fetch adapter is not configured";
                continue;
            }
            std::vector<std::string> remoteLines;
            std::string remoteError;
            if (!remoteFetch(input.remoteUrl, remoteLines, remoteError)) {
                lastReason = "remote_fetch_failed";
                lastError = remoteError.empty() ? "remote fetch failed" : remoteError;
                continue;
            }
            const auto lines = sanitizeAndDedupeObfs4BridgeLines(remoteLines);
            std::string reason;
            std::string error;
            if (!validateResolvedPool(lines, minPoolSize, reason, error)) {
                lastReason = "remote_" + reason;
                lastError = error;
                continue;
            }
            std::string cacheWriteError;
            if (!maybeWriteCache(input.cachePath, now, cacheTtl, lines, result.cacheUpdated, cacheWriteError)) {
                lastReason = "cache_write_failed";
                lastError = cacheWriteError;
            }
            finalizeSuccess("remote", lines, false, false, 0);
            return result;
        }
    }

    result.reasonCode = lastReason;
    result.error = lastError;
    return result;
}

}
