#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace synapse {
namespace web {

// Reject text that could smuggle another argument or a header if a caller
// later joins it into a command line. Embedded NUL is included.
inline bool curlTextHasControlChar(const std::string& value) {
    if (value.find('\0') != std::string::npos) return true;
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

inline bool curlUrlHasPrefix(const std::string& url, const char* prefix) {
    const size_t n = std::char_traits<char>::length(prefix);
    if (url.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char got = static_cast<unsigned char>(url[i]);
        if (std::tolower(got) != static_cast<unsigned char>(prefix[i])) return false;
    }
    return true;
}

// https, plus http only when that caller already fetches it.
inline bool curlUrlIsAllowed(const std::string& url, bool allowHttp) {
    if (curlTextHasControlChar(url)) return false;
    if (curlUrlHasPrefix(url, "https://")) return true;
    return allowHttp && curlUrlHasPrefix(url, "http://");
}

// Call immediately after the program name. -q must be the first option so a
// curl config file cannot add a protocol, a cookie, or a secret.
// Redirects stay in the same set, so file, gopher, dict, ldap, and scp cannot
// be selected on a redirect.
inline void appendCurlContainmentArgs(std::vector<std::string>& args, bool allowHttp) {
    args.push_back("-q");
    args.push_back("--proto");
    args.push_back(allowHttp ? "-all,http,https" : "-all,https");
    args.push_back("--proto-redir");
    args.push_back(allowHttp ? "-all,http,https" : "-all,https");
}

struct CurlFetchOptions {
    std::string socksProxyHostPort;
    uint32_t timeoutSeconds = 10;
    size_t maxBytes = 1024 * 1024;
    std::string userAgent = "Mozilla/5.0 (compatible; SynapseNet/0.1)";
    bool followRedirects = true;
    // Fail closed instead of a clearnet curl when Tor is the required path.
    bool requireSocks = false;
};

struct CurlFetchResult {
    std::string body;
    int exitCode = -1;
    std::string error;
};

CurlFetchResult curlFetch(const std::string& url, const CurlFetchOptions& options);

}
}

