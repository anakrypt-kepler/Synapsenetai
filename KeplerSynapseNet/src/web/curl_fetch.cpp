#include "web/curl_fetch.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace synapse {
namespace web {

static std::string shellEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out.append("'\\''");
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

static std::string readSmallFile(const std::string& path, size_t maxBytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string out;
    out.resize(maxBytes);
    in.read(out.data(), static_cast<std::streamsize>(maxBytes));
    out.resize(static_cast<size_t>(in.gcount()));
    return out;
}

static int decodeExitCode(int rc) {
#ifdef _WIN32
    return rc;
#else
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    if (WIFSIGNALED(rc)) return 128 + WTERMSIG(rc);
    return rc;
#endif
}

static std::string makeTempPath(const std::string& prefix) {
#ifdef _WIN32
    (void)prefix;
    return {};
#else
    std::string tmpl = "/tmp/" + prefix + "XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd >= 0) close(fd);
    return std::string(buf.data());
#endif
}

CurlFetchResult curlFetch(const std::string& url, const CurlFetchOptions& options) {
    CurlFetchResult result;
#ifdef _WIN32
    (void)url;
    (void)options;
    result.exitCode = -1;
    result.error = "curl fetch not supported on Windows build";
    return result;
#else
    std::string errPath = makeTempPath("synapsenet_curl_err_");
    if (errPath.empty()) {
        result.exitCode = -1;
        result.error = "mkstemp failed";
        return result;
    }

    std::ostringstream cmd;
    cmd << "curl -sS --compressed ";
    if (options.followRedirects) cmd << "-L ";
    cmd << "--max-time " << options.timeoutSeconds << " ";
    cmd << "--connect-timeout " << options.timeoutSeconds << " ";
    if (!options.userAgent.empty()) {
        cmd << "-A " << shellEscape(options.userAgent) << " ";
    }
    if (!options.socksProxyHostPort.empty()) {
        cmd << "--socks5-hostname " << shellEscape(options.socksProxyHostPort) << " ";
    }
    cmd << shellEscape(url) << " 2>" << shellEscape(errPath);

    FILE* fp = popen(cmd.str().c_str(), "r");
    if (!fp) {
        result.exitCode = -1;
        result.error = "popen failed";
        std::remove(errPath.c_str());
        return result;
    }

    const size_t maxBytes = std::max<size_t>(static_cast<size_t>(1), options.maxBytes);
    std::string body;
    body.reserve(std::min<size_t>(maxBytes, 256 * 1024));
    bool truncated = false;
    char buf[4096];
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        if (body.size() < maxBytes) {
            const size_t remaining = maxBytes - body.size();
            const size_t take = std::min(n, remaining);
            if (take > 0) body.append(buf, take);
            if (take < n) truncated = true;
        } else {
            truncated = true;
        }
    }

    int rc = pclose(fp);
    result.exitCode = decodeExitCode(rc);
    result.body = std::move(body);
    result.error = readSmallFile(errPath, 16 * 1024);
    if (truncated && result.exitCode == 23 &&
        result.error.find("Failed writing received data") != std::string::npos) {
        result.exitCode = 0;
        result.error.clear();
    }
    std::remove(errPath.c_str());
    return result;
#endif
}

}
}
