#include "node/node_init.h"

#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>

#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace synapse {

void ensureDirectories(const NodeConfig& config) {
    std::filesystem::create_directories(config.dataDir);
    std::filesystem::create_directories(config.dataDir + "/blocks");
    std::filesystem::create_directories(config.dataDir + "/chaindata");
    std::filesystem::create_directories(config.dataDir + "/wallet");
    std::filesystem::create_directories(config.dataDir + "/models");
    std::filesystem::create_directories(config.dataDir + "/logs");
    std::filesystem::create_directories(config.dataDir + "/ledger");
    std::filesystem::create_directories(config.dataDir + "/knowledge");
    std::filesystem::create_directories(config.dataDir + "/transfer");
    std::filesystem::create_directories(config.dataDir + "/consensus");
}

bool checkDiskSpace(const std::string& path, uint64_t requiredBytes) {
    struct statvfs stat {};
    if (statvfs(path.c_str(), &stat) != 0) {
        return false;
    }
    uint64_t available = stat.f_bavail * stat.f_frsize;
    return available >= requiredBytes;
}

bool checkSystemRequirements() {
    uint32_t cores = std::thread::hardware_concurrency();
    if (cores < 2) {
        std::cerr << "Warning: System has only " << cores << " CPU core(s)\n";
    }
    return true;
}

void registerSignalHandlers(void (*handler)(int)) {
    std::signal(SIGINT, handler);
    std::signal(SIGTERM, handler);
#ifndef _WIN32
    std::signal(SIGHUP, handler);
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

void daemonize() {
#ifndef _WIN32
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Failed to fork daemon process\n";
        exit(1);
    }
    if (pid > 0) {
        exit(0);
    }

    if (setsid() < 0) {
        exit(1);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int nullfd = ::open("/dev/null", O_RDWR);
    if (nullfd >= 0) {
        ::dup2(nullfd, STDIN_FILENO);
        ::dup2(nullfd, STDOUT_FILENO);
        ::dup2(nullfd, STDERR_FILENO);
        if (nullfd > STDERR_FILENO) {
            ::close(nullfd);
        }
    }
#endif
}

} // namespace synapse
