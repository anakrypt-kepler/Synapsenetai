#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace synapse::core {

struct TorBridgePoolCache {
    uint32_t version = 1;
    std::string transport = "obfs4";
    uint64_t savedAt = 0;
    uint64_t ttlSeconds = 0;
    std::string checksum;
    std::vector<std::string> lines;
};

std::string normalizeTorBridgeProviderSource(std::string source);

std::string computeObfs4BridgePoolChecksum(const std::vector<std::string>& rawLines);

bool isTorBridgePoolCacheFresh(const TorBridgePoolCache& cache, uint64_t now);

bool loadTorBridgePoolCacheFile(const std::string& path,
                                TorBridgePoolCache& out,
                                std::string* errorOut = nullptr);

bool saveTorBridgePoolCacheFile(const TorBridgePoolCache& input,
                                const std::string& path,
                                std::string* errorOut = nullptr);

struct TorBridgeProviderResolveInput {
    std::string requestedSource = "manual";
    std::string transport = "obfs4";
    std::vector<std::string> manualLines;
    std::string filePath;
    std::string cachePath;
    std::string remoteUrl;
    uint64_t now = 0;
    uint64_t cacheTtlSeconds = 86400;
    uint32_t minPoolSize = 1;
    bool allowStaleCacheFallback = false;
};

using TorBridgeRemoteFetchFn =
    std::function<bool(const std::string& url, std::vector<std::string>& lines, std::string& errorOut)>;

struct TorBridgeProviderResolveResult {
    bool ok = false;
    std::string source;
    std::vector<std::string> lines;
    std::string checksum;
    std::string error;
    std::string reasonCode;
    std::vector<std::string> triedSources;
    bool usedFallback = false;
    bool cacheHit = false;
    bool cacheFresh = false;
    bool cacheUpdated = false;
    uint64_t cacheSavedAt = 0;
};

TorBridgeProviderResolveResult resolveObfs4BridgePool(
    const TorBridgeProviderResolveInput& input,
    const TorBridgeRemoteFetchFn& remoteFetch = {});

}

