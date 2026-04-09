#include "core/tor_bridge_provider.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

static const char* kFpA = "0123456789ABCDEF0123456789ABCDEF01234567";
static const char* kFpB = "89abcdef0123456789abcdef0123456789abcdef";
static const char* kFpC = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
static const char* kFpD = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";

static std::string bridgeA() {
    return std::string("Bridge obfs4 1.2.3.4:443 ") + kFpA + " cert=a iat-mode=0";
}

static std::string bridgeB() {
    return std::string("Bridge obfs4 5.6.7.8:9001 ") + kFpB + " cert=b iat-mode=1";
}

static std::string bridgeC() {
    return std::string("Bridge obfs4 9.9.9.9:443 ") + kFpC + " cert=c iat-mode=2";
}

static std::string bridgeD() {
    return std::string("Bridge obfs4 8.8.8.8:443 ") + kFpD + " cert=d iat-mode=0";
}

static fs::path makeTempDir() {
    const auto base = fs::temp_directory_path() /
        ("synapse_tor_bridge_provider_" + std::to_string(static_cast<long long>(::getpid())));
    fs::remove_all(base);
    fs::create_directories(base);
    return base;
}

static void writeTextFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    assert(out.is_open());
    out << text;
    assert(out.good());
}

static void testNormalizeSourceAliases() {
    using synapse::core::normalizeTorBridgeProviderSource;
    assert(normalizeTorBridgeProviderSource("manual") == "manual");
    assert(normalizeTorBridgeProviderSource(" INLINE ") == "manual");
    assert(normalizeTorBridgeProviderSource("file") == "file");
    assert(normalizeTorBridgeProviderSource("manual_file") == "file");
    assert(normalizeTorBridgeProviderSource("cache") == "cache");
    assert(normalizeTorBridgeProviderSource("cached") == "cache");
    assert(normalizeTorBridgeProviderSource("remote") == "remote");
    assert(normalizeTorBridgeProviderSource("https") == "remote");
    assert(normalizeTorBridgeProviderSource("weird") == "manual");
}

static void testCacheRoundTripChecksumAndFreshness() {
    const auto dir = makeTempDir();
    const auto cachePath = dir / "bridge_pool_cache.json";

    synapse::core::TorBridgePoolCache cache;
    cache.transport = "obfs4";
    cache.savedAt = 1000;
    cache.ttlSeconds = 120;
    cache.lines = {
        "# comment",
        bridgeA(),
        bridgeA(),
        "Bridge snowflake 192.0.2.3:1",
        bridgeB(),
    };

    std::string err;
    assert(synapse::core::saveTorBridgePoolCacheFile(cache, cachePath.string(), &err));

    synapse::core::TorBridgePoolCache loaded;
    assert(synapse::core::loadTorBridgePoolCacheFile(cachePath.string(), loaded, &err));
    assert(loaded.lines.size() == 2);
    assert(!loaded.checksum.empty());
    assert(loaded.transport == "obfs4");
    assert(synapse::core::isTorBridgePoolCacheFresh(loaded, 1119));
    assert(!synapse::core::isTorBridgePoolCacheFresh(loaded, 1121));

    std::string raw = std::string(
        "{\n"
        "  \"version\": 1,\n"
        "  \"transport\": \"obfs4\",\n"
        "  \"savedAt\": 1000,\n"
        "  \"ttlSeconds\": 120,\n"
        "  \"checksum\": \"deadbeef\",\n"
        "  \"lines\": [\"") + bridgeA() + "\"]\n}\n";
    writeTextFile(cachePath, raw);
    assert(!synapse::core::loadTorBridgePoolCacheFile(cachePath.string(), loaded, &err));
    assert(err == "checksum_mismatch");
}

static void testResolveManualUpdatesCache() {
    const auto dir = makeTempDir();
    const auto cachePath = dir / "cache.json";

    synapse::core::TorBridgeProviderResolveInput in;
    in.requestedSource = "manual";
    in.transport = "obfs4";
    in.manualLines = {
        "# comment",
        bridgeA(),
        bridgeA(),
        "invalid line",
        bridgeB(),
    };
    in.cachePath = cachePath.string();
    in.now = 2000;
    in.cacheTtlSeconds = 3600;
    in.minPoolSize = 2;

    const auto r = synapse::core::resolveObfs4BridgePool(in);
    assert(r.ok);
    assert(r.source == "manual");
    assert(!r.usedFallback);
    assert(r.lines.size() == 2);
    assert(r.cacheUpdated);
    assert(!r.checksum.empty());

    synapse::core::TorBridgePoolCache cache;
    std::string err;
    assert(synapse::core::loadTorBridgePoolCacheFile(cachePath.string(), cache, &err));
    assert(cache.lines == r.lines);
}

static void testResolveFallbackRemoteToCache() {
    const auto dir = makeTempDir();
    const auto cachePath = dir / "cache.json";

    synapse::core::TorBridgePoolCache cache;
    cache.savedAt = 3000;
    cache.ttlSeconds = 600;
    cache.lines = {bridgeC(), bridgeD()};
    std::string err;
    assert(synapse::core::saveTorBridgePoolCacheFile(cache, cachePath.string(), &err));

    synapse::core::TorBridgeProviderResolveInput in;
    in.requestedSource = "remote";
    in.transport = "obfs4";
    in.cachePath = cachePath.string();
    in.now = 3200;
    in.cacheTtlSeconds = 600;
    in.minPoolSize = 2;
    in.remoteUrl = "https://example.invalid/bridges";
    in.manualLines = {bridgeA()};

    bool remoteCalled = false;
    const auto r = synapse::core::resolveObfs4BridgePool(
        in,
        [&](const std::string&, std::vector<std::string>&, std::string& error) {
            remoteCalled = true;
            error = "manual_step_required";
            return false;
        });

    assert(remoteCalled);
    assert(r.ok);
    assert(r.usedFallback);
    assert(r.source == "cache");
    assert(r.cacheHit);
    assert(r.cacheFresh);
    assert(r.lines.size() == 2);
    assert(r.reasonCode == "ok");
    assert(!r.triedSources.empty());
    assert(r.triedSources[0] == "remote");
}

static void testResolveFallbackFileThenManualAndStaleCachePolicy() {
    const auto dir = makeTempDir();
    const auto cachePath = dir / "cache.json";
    const auto filePath = dir / "bridges.obfs4.txt";

    synapse::core::TorBridgePoolCache cache;
    cache.savedAt = 100;
    cache.ttlSeconds = 50;
    cache.lines = {bridgeC()};
    std::string err;
    assert(synapse::core::saveTorBridgePoolCacheFile(cache, cachePath.string(), &err));

    writeTextFile(filePath, bridgeA() + "\n" + bridgeB() + "\n");

    synapse::core::TorBridgeProviderResolveInput in;
    in.requestedSource = "cache";
    in.transport = "obfs4";
    in.cachePath = cachePath.string();
    in.filePath = filePath.string();
    in.now = 1000;
    in.cacheTtlSeconds = 100;
    in.minPoolSize = 2;
    in.allowStaleCacheFallback = false;
    in.manualLines = {bridgeD()};

    const auto r1 = synapse::core::resolveObfs4BridgePool(in);
    assert(r1.ok);
    assert(r1.source == "file");
    assert(r1.usedFallback);
    assert(!r1.cacheHit);
    assert(r1.lines.size() == 2);

    fs::remove(filePath);
    in.requestedSource = "file";
    in.cachePath.clear();
    in.manualLines = {bridgeA(), bridgeB()};
    const auto r2 = synapse::core::resolveObfs4BridgePool(in);
    assert(r2.ok);
    assert(r2.source == "manual");
    assert(r2.usedFallback);
    assert(r2.lines.size() == 2);
}

int main() {
    testNormalizeSourceAliases();
    testCacheRoundTripChecksumAndFreshness();
    testResolveManualUpdatesCache();
    testResolveFallbackRemoteToCache();
    testResolveFallbackFileThenManualAndStaleCachePolicy();
    return 0;
}
