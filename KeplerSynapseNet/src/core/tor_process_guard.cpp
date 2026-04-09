#include "core/tor_process_guard.h"

#include <algorithm>
#include <sstream>

namespace synapse::core {

namespace {

void trimLeftInPlace(std::string& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.erase(s.begin());
    }
}

}

bool isOwnedManagedTorCommandLine(const std::string& cmdline, const std::string& managedTorDataDir) {
    if (cmdline.empty() || managedTorDataDir.empty()) return false;
    if (cmdline.find("tor") == std::string::npos) return false;
    if (cmdline.find(managedTorDataDir) == std::string::npos) return false;
    if (cmdline.find("--RunAsDaemon 1") == std::string::npos) return false;
    return true;
}

std::vector<int64_t> parseOwnedManagedTorPidsFromPsOutput(const std::string& psOutput,
                                                          const std::string& managedTorDataDir) {
    std::vector<int64_t> pids;
    if (psOutput.empty() || managedTorDataDir.empty()) return pids;

    std::istringstream in(psOutput);
    std::string line;
    while (std::getline(in, line)) {
        trimLeftInPlace(line);
        if (line.empty()) continue;

        std::istringstream ls(line);
        int64_t pid = 0;
        if (!(ls >> pid) || pid <= 0) continue;

        std::string cmd;
        std::getline(ls, cmd);
        trimLeftInPlace(cmd);
        if (!isOwnedManagedTorCommandLine(cmd, managedTorDataDir)) continue;
        pids.push_back(pid);
    }

    std::sort(pids.begin(), pids.end());
    pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
    return pids;
}

}
