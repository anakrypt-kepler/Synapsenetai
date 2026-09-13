#include <iostream>
#include <string>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "node/synapse_net.h"
#include "node/node_init.h"
#include "cli/cli_parser.h"
#include "cli/cli_rpc_client.h"
#include "utils/single_instance.h"

namespace synapse {

void signalHandler(int signal) {
    // First Ctrl+C / SIGTERM: request a clean stop. Daemon ignores SIGHUP for
    // stop (that reloads config); interactive TUI treats SIGHUP as shutdown too.
    if (signal == SIGINT || signal == SIGTERM
#ifndef _WIN32
        || (!g_daemonMode && signal == SIGHUP)
#endif
    ) {
        if (g_shutdownSignal.load() == 0) {
            g_shutdownSignal.store(signal);
        }
        g_running = false;
    }
#ifndef _WIN32
    else if (signal == SIGHUP) {
        g_reloadConfig = true;
    }
#endif
}

void printBanner() {
    std::cout << R"(
  ____                              _   _      _
 / ___| _   _ _ __   __ _ _ __  ___| \ | | ___| |_
 \___ \| | | | '_ \ / _` | '_ \/ __|  \| |/ _ \ __|
  ___) | |_| | | | | (_| | |_) \__ \ |\  |  __/ |_
 |____/ \__, |_| |_|\__,_| .__/|___/_| \_|\___|\__|
        |___/            |_|
)" << std::endl;
    std::cout << "  Decentralized AI Knowledge Network v0.1.0" << std::endl;
    std::cout << "  ==========================================" << std::endl;
    std::cout << std::endl;
}

} // namespace synapse

// synapsed entry point.
// Parse flags → data dir (~/.synapsenet) → optional daemonize → single-instance
// lock → initializeSynapseNet() → TUI/daemon loop or one-shot CLI command.
int main(int argc, char* argv[]) {
    synapse::registerSignalHandlers(synapse::signalHandler);

    synapse::NodeConfig config;

#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("APPDATA");
    config.dataDir = home ? std::string(home) + "/.synapsenet" : ".synapsenet";
#else
    const char* home = std::getenv("HOME");
    config.dataDir = home ? std::string(home) + "/.synapsenet" : ".synapsenet";
#endif

    if (!synapse::parseArgs(argc, argv, config)) {
        return 1;
    }

    synapse::g_daemonMode = config.daemon;

    if (config.showHelp) {
        synapse::printHelp(argv[0]);
        return 0;
    }

    if (config.showVersion) {
        synapse::printVersion();
        return 0;
    }

    if (!config.daemon && !config.tui && !config.cli) {
        synapse::printBanner();
    }

    if (!synapse::checkSystemRequirements()) {
        return 1;
    }

    synapse::ensureDirectories(config);

    if (!synapse::checkDiskSpace(config.dataDir, 1024 * 1024 * 100)) {
        std::cerr << "Warning: Low disk space in " << config.dataDir << "\n";
    }

    if (config.daemon) {
        // Docker/PID1: -d means "no TUI", not double-fork (that exits the container).
        const char* noFork = std::getenv("SYNAPSENET_NO_FORK");
        const bool skipFork = noFork && (std::string(noFork) == "1" || std::string(noFork) == "true");
        if (!skipFork) {
            synapse::daemonize();
        }
    }

    if (config.cli) {
        auto rc = synapse::runCliViaRpc(config);
        if (rc.has_value()) {
            return *rc;
        }
    }

    std::string instanceErr;
    auto instanceLock = synapse::utils::SingleInstanceLock::acquire(config.dataDir, &instanceErr);
    if (!instanceLock) {
        std::cerr << "SynapseNet: " << instanceErr << "\n";
        return 1;
    }

    auto node = synapse::createSynapseNet();

    if (!synapse::initializeSynapseNet(*node, config)) {
        std::cerr << "Failed to initialize node\n";
        return 1;
    }

    int result = 0;
    if (config.cli) {
        result = synapse::runSynapseNetCommand(*node, config.commandArgs);
    } else {
        result = synapse::runSynapseNet(*node);
    }

    synapse::shutdownSynapseNet(*node);

    return result;
}
