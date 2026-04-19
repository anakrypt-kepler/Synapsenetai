#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace synapse {
namespace web {

struct CurlFetchOptions {
    std::string socksProxyHostPort;
    uint32_t timeoutSeconds = 10;
    size_t maxBytes = 1024 * 1024;
    std::string userAgent = "Mozilla/5.0 (compatible; SynapseNet/0.1)";
    bool followRedirects = true;
};

struct CurlFetchResult {
    std::string body;
    int exitCode = -1;
    std::string error;
};

CurlFetchResult curlFetch(const std::string& url, const CurlFetchOptions& options);

}
}

