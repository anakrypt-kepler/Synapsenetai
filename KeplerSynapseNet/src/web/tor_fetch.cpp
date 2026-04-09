#include "web/web.h"
#include "web/curl_fetch.h"
#include <mutex>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace synapse {
namespace web {

struct TorFetch::Impl {
    TorEngine* external = nullptr;
    std::unique_ptr<TorEngine> owned;
    TorConfig config{};
    uint32_t timeoutSeconds = 10;
    size_t maxBytes = 1024 * 1024;
    TorFetchStats stats{};
    mutable std::mutex mtx;
    
    std::string httpGet(const std::string& host, const std::string& path, uint16_t port);
};

struct ParsedUrl {
    std::string scheme;
    std::string host;
    uint16_t port;
    std::string path;
    bool valid;
};

static ParsedUrl parseUrl(const std::string& input) {
    ParsedUrl parsed{};
    std::string url = input;
    if (url.find("://") == std::string::npos) {
        url = "http://" + url;
    }
    size_t schemePos = url.find("://");
    if (schemePos == std::string::npos) return parsed;
    parsed.scheme = url.substr(0, schemePos);
    std::transform(parsed.scheme.begin(), parsed.scheme.end(), parsed.scheme.begin(), ::tolower);
    size_t hostStart = schemePos + 3;
    size_t pathStart = url.find('/', hostStart);
    std::string hostPort = pathStart == std::string::npos ? url.substr(hostStart) : url.substr(hostStart, pathStart - hostStart);
    parsed.path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    if (hostPort.empty()) return parsed;
    size_t colon = hostPort.find(':');
    if (colon != std::string::npos) {
        parsed.host = hostPort.substr(0, colon);
        parsed.port = static_cast<uint16_t>(std::stoi(hostPort.substr(colon + 1)));
    } else {
        parsed.host = hostPort;
        parsed.port = parsed.scheme == "https" ? 443 : 80;
    }
    parsed.valid = (parsed.scheme == "http" || parsed.scheme == "https");
    return parsed;
}

std::string TorFetch::Impl::httpGet(const std::string& host, const std::string& path, uint16_t port) {
    std::ostringstream url;
    url << "http://" << host;
    if (port != 0 && port != 80) url << ":" << port;
    if (!path.empty() && path[0] != '/') url << "/";
    url << (path.empty() ? "/" : path);

    CurlFetchOptions opt;
    opt.timeoutSeconds = timeoutSeconds;
    opt.maxBytes = maxBytes;
    CurlFetchResult res = curlFetch(url.str(), opt);
    if (res.exitCode != 0) return "";
    return res.body;
}

TorFetch::TorFetch() : impl_(std::make_unique<Impl>()) {
    impl_->stats = TorFetchStats{};
}

TorFetch::~TorFetch() {
    shutdown();
}

bool TorFetch::init(const TorConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config = config;
    if (!impl_->external) {
        impl_->owned = std::make_unique<TorEngine>();
        return impl_->owned->init(config);
    }
    return impl_->external->isConnected();
}

void TorFetch::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->owned) {
        impl_->owned->shutdown();
        impl_->owned.reset();
    }
}

bool TorFetch::isReady() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->external) return impl_->external->isConnected();
    if (impl_->owned) return impl_->owned->isConnected();
    return false;
}

void TorFetch::setTorEngine(TorEngine* engine) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->external = engine;
    impl_->owned.reset();
}

void TorFetch::setTimeout(uint32_t seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->timeoutSeconds = seconds;
}

void TorFetch::setMaxBytes(size_t bytes) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxBytes = bytes;
}

std::string TorFetch::fetch(const std::string& url) {
    ParsedUrl parsed = parseUrl(url);
    if (!parsed.valid) return "";
    
    bool onion = isOnionUrl(url) || isOnionUrl(parsed.host);
    size_t maxBytes = 0;
    uint32_t timeoutSeconds = 0;
    TorConfig cfg{};
    
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        maxBytes = impl_->maxBytes;
        timeoutSeconds = impl_->timeoutSeconds;
        cfg = impl_->config;
        impl_->stats.requests++;
        if (onion) {
            impl_->stats.onionRequests++;
        }
    }
    
    std::string response;
    std::string socks;
    if (!cfg.socksHost.empty() && cfg.socksPort != 0) {
        socks = cfg.socksHost + ":" + std::to_string(cfg.socksPort);
    }

    if (onion && socks.empty()) {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->stats.failures++;
        impl_->stats.onionFailures++;
        return "";
    }

    CurlFetchOptions opt;
    opt.timeoutSeconds = timeoutSeconds;
    opt.maxBytes = maxBytes;
    opt.socksProxyHostPort = socks;
    CurlFetchResult res = curlFetch(url, opt);
    response = res.body;
    if (cfg.bypassOnionHttpsFallback && res.exitCode != 0 && onion && parsed.scheme == "https") {
        std::string fallback = "http://" + parsed.host + parsed.path;
        CurlFetchResult res2 = curlFetch(fallback, opt);
        if (res2.exitCode == 0) response = res2.body;
    }
    
    if (response.size() > maxBytes) {
        response.resize(maxBytes);
    }
    
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        if (response.empty()) {
            impl_->stats.failures++;
            if (onion) {
                impl_->stats.onionFailures++;
            }
        } else {
            impl_->stats.successes++;
            if (onion) {
                impl_->stats.onionSuccesses++;
            }
            impl_->stats.bytes += response.size();
        }
    }
    
    return response;
}

std::string TorFetch::fetchOnion(const std::string& onionUrl) {
    if (!isOnionUrl(onionUrl)) return "";
    return fetch(onionUrl);
}

TorFetchStats TorFetch::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->stats;
}

}
}
