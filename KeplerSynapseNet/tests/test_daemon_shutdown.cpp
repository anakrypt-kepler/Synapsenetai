#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

uint16_t findFreePort() {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(sock);
        return 0;
    }

    const uint16_t port = ntohs(addr.sin_port);
    ::close(sock);
    return port;
}

bool waitForFile(const std::filesystem::path& path, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(path)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return std::filesystem::exists(path);
}

bool isProcessAlive(pid_t pid) {
    if (pid <= 0) return false;
    if (::kill(pid, 0) == 0) return true;
    return errno == EPERM;
}

pid_t readLockPid(const std::filesystem::path& lockPath) {
    std::ifstream in(lockPath);
    if (!in.is_open()) return 0;
    std::string pidLine;
    std::getline(in, pidLine);
    if (pidLine.empty()) return 0;
    try {
        return static_cast<pid_t>(std::stol(pidLine));
    } catch (...) {
        return 0;
    }
}

bool waitForProcessStart(const std::filesystem::path& lockPath,
                         pid_t& pidOut,
                         std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(lockPath)) {
            pid_t pid = readLockPid(lockPath);
            if (pid > 0 && isProcessAlive(pid)) {
                pidOut = pid;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    pidOut = readLockPid(lockPath);
    return pidOut > 0 && isProcessAlive(pidOut);
}

bool waitForProcessExit(pid_t pid, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!isProcessAlive(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !isProcessAlive(pid);
}

bool waitForLogContains(const std::filesystem::path& logPath,
                        const std::string& needle,
                        std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream in(logPath);
        if (in.is_open()) {
            std::stringstream buffer;
            buffer << in.rdbuf();
            if (buffer.str().find(needle) != std::string::npos) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::ifstream in(logPath);
    if (!in.is_open()) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str().find(needle) != std::string::npos;
}

bool launchDaemon(const std::string& binaryPath,
                  const std::filesystem::path& dataDir,
                  const std::filesystem::path& logPath,
                  uint16_t port,
                  uint16_t rpcPort,
                  uint16_t torSocksPort,
                  pid_t& daemonPidOut) {
    std::filesystem::create_directories(dataDir);
    const auto configPath = dataDir / "daemon_shutdown_test.conf";
    {
        std::ofstream cfg(configPath, std::ios::trunc);
        if (!cfg.is_open()) return false;
        cfg << "agent.tor.mode=external\n";
        cfg << "agent.tor.required=false\n";
        cfg << "agent.tor.socks_host=127.0.0.1\n";
        cfg << "agent.tor.socks_port=" << torSocksPort << "\n";
        cfg << "agent.tor.web_probe_urls=http://127.0.0.1:1/\n";
        cfg << "agent.tor.web_probe_retries=1\n";
        cfg << "agent.tor.web_probe_timeout_seconds=2\n";
        cfg << "agent.tor.web_probe_interval_seconds=3\n";
    }

    pid_t child = ::fork();
    if (child < 0) return false;
    if (child == 0) {
        std::string portArg = std::to_string(port);
        std::string rpcPortArg = std::to_string(rpcPort);
        std::string dataDirArg = dataDir.string();
        std::string configArg = configPath.string();
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(binaryPath.c_str()));
        argv.push_back(const_cast<char*>("-d"));
        argv.push_back(const_cast<char*>("-E"));
        argv.push_back(const_cast<char*>("-D"));
        argv.push_back(const_cast<char*>(dataDirArg.c_str()));
        argv.push_back(const_cast<char*>("-c"));
        argv.push_back(const_cast<char*>(configArg.c_str()));
        argv.push_back(const_cast<char*>("-p"));
        argv.push_back(const_cast<char*>(portArg.c_str()));
        argv.push_back(const_cast<char*>("-r"));
        argv.push_back(const_cast<char*>(rpcPortArg.c_str()));
        argv.push_back(nullptr);
        ::execv(binaryPath.c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    if (::waitpid(child, &status, 0) < 0) return false;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return false;

    const auto lockPath = dataDir / "synapsed.lock";
    if (!waitForProcessStart(lockPath, daemonPidOut, std::chrono::seconds(20))) {
        return false;
    }
    return waitForLogContains(logPath, "Running in daemon mode", std::chrono::seconds(30));
}

bool stopDaemon(pid_t pid) {
    if (::kill(pid, SIGTERM) != 0) return false;
    return waitForProcessExit(pid, std::chrono::seconds(20));
}

}

int main(int argc, char* argv[]) {
    if (!check(argc >= 2, "synapsed binary path is required")) return 1;

    const std::string binaryPath = argv[1];
    if (!check(std::filesystem::exists(binaryPath), "synapsed binary does not exist")) return 1;

    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("synapsenet_daemon_shutdown_" + unique);
    const std::filesystem::path logPath = root / "synapsenet.log";

    try {
        const uint16_t port1 = findFreePort();
        const uint16_t rpcPort1 = findFreePort();
        const uint16_t port2 = findFreePort();
        const uint16_t rpcPort2 = findFreePort();
        const uint16_t torSocksPort1 = findFreePort();
        const uint16_t torSocksPort2 = findFreePort();
        if (!check(port1 != 0 && rpcPort1 != 0 && port2 != 0 && rpcPort2 != 0 &&
                   torSocksPort1 != 0 && torSocksPort2 != 0,
                   "failed to allocate test ports")) return 1;

        pid_t firstPid = 0;
        if (!check(launchDaemon(binaryPath, root, logPath, port1, rpcPort1, torSocksPort1, firstPid), "failed to launch daemon for first run")) return 1;
        if (!check(stopDaemon(firstPid), "daemon did not stop after SIGTERM")) return 1;
        if (!check(waitForLogContains(logPath, "reason=signal:SIGTERM", std::chrono::seconds(10)),
                   "shutdown reason was not written to the daemon log")) return 1;

        pid_t secondPid = 0;
        if (!check(launchDaemon(binaryPath, root, logPath, port2, rpcPort2, torSocksPort2, secondPid), "failed to relaunch daemon after clean shutdown")) return 1;
        if (!check(secondPid > 0 && secondPid != firstPid, "daemon relaunch did not create a fresh process")) return 1;
        if (!check(stopDaemon(secondPid), "relaunched daemon did not stop after SIGTERM")) return 1;
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }

    std::filesystem::remove_all(root);
    std::cout << "daemon shutdown tests passed\n";
    return 0;
}
