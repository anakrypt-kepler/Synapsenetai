#include "web/curl_fetch.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace synapse {
namespace web {

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

#ifndef _WIN32
// argv execution. stderr goes to errPath. The shell is not involved.
static int runCurlArgv(const std::vector<std::string>& args, const std::string& errPath,
                        std::string& body, size_t maxBytes, bool& truncated) {
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) return -1;
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        const int errFd = open(errPath.c_str(), O_WRONLY | O_TRUNC);
        if (errFd < 0) _exit(127);
        if (dup2(errFd, STDERR_FILENO) < 0) _exit(127);
        if (errFd > 2) close(errFd);
        const int inFd = open("/dev/null", O_RDONLY);
        if (inFd >= 0) {
            dup2(inFd, STDIN_FILENO);
            if (inFd > 2) close(inFd);
        }
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(127);
        close(pipefd[0]);
        close(pipefd[1]);
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(pipefd[1]);
    body.clear();
    body.reserve(std::min<size_t>(maxBytes, 256 * 1024));
    truncated = false;
    char buf[4096];
    while (true) {
        const ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        const size_t got = static_cast<size_t>(n);
        if (body.size() < maxBytes) {
            const size_t remaining = maxBytes - body.size();
            const size_t take = std::min(got, remaining);
            if (take > 0) body.append(buf, take);
            if (take < got) truncated = true;
        } else {
            truncated = true;
        }
    }
    close(pipefd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return status;
}
#endif

CurlFetchResult curlFetch(const std::string& url, const CurlFetchOptions& options) {
    CurlFetchResult result;
    if (options.requireSocks && options.socksProxyHostPort.empty()) {
        result.exitCode = -1;
        result.error = "socks required";
        return result;
    }
    // http stays allowed here because search, probes, and onion fetches already
    // use it. SOCKS is added only when the caller set a proxy. No cookie, RPC
    // cookie, or control password is placed on the command.
    if (!curlUrlIsAllowed(url, true) ||
        curlTextHasControlChar(options.userAgent) ||
        curlTextHasControlChar(options.socksProxyHostPort)) {
        result.exitCode = -1;
        result.error = "protocol not allowed";
        return result;
    }
#ifdef _WIN32
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

    std::vector<std::string> args = {"curl"};
    appendCurlContainmentArgs(args, true);
    args.push_back("-sS");
    args.push_back("--compressed");
    if (options.followRedirects) args.push_back("-L");
    args.push_back("--max-time");
    args.push_back(std::to_string(options.timeoutSeconds));
    args.push_back("--connect-timeout");
    args.push_back(std::to_string(options.timeoutSeconds));
    if (!options.userAgent.empty()) {
        args.push_back("-A");
        args.push_back(options.userAgent);
    }
    if (!options.socksProxyHostPort.empty()) {
        args.push_back("--socks5-hostname");
        args.push_back(options.socksProxyHostPort);
    }
    args.push_back(url);

    const size_t maxBytes = std::max<size_t>(static_cast<size_t>(1), options.maxBytes);
    std::string body;
    bool truncated = false;
    const int rc = runCurlArgv(args, errPath, body, maxBytes, truncated);
    if (rc == -1 && body.empty()) {
        result.exitCode = -1;
        result.error = "curl start failed";
        std::remove(errPath.c_str());
        return result;
    }
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
