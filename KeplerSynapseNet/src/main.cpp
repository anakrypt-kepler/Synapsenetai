#include <iostream>
#include <string>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>

#include "node/synapse_net.h"
#include "node/node_init.h"
#include "cli/cli_parser.h"
#include "cli/cli_rpc_client.h"
#include "utils/single_instance.h"

namespace synapse {

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM || (!g_daemonMode && signal == SIGHUP)) {
        if (g_shutdownSignal.load() == 0) {
            g_shutdownSignal.store(signal);
        }
        g_running = false;
    } else if (signal == SIGHUP) {
        g_reloadConfig = true;
    }
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

int main(int argc, char* argv[]) {
    synapse::registerSignalHandlers(synapse::signalHandler);

    synapse::NodeConfig config;

    const char* home = std::getenv("HOME");
    config.dataDir = home ? std::string(home) + "/.synapsenet" : ".synapsenet";

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
        synapse::daemonize();
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
