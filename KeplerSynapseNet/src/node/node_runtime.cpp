#include "node/node_runtime.h"

#include "utils/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <thread>
#include <unordered_set>
#include <utility>

namespace synapse::node {

void startNodeThreads(
    std::thread& networkThread,
    std::thread& consensusThread,
    std::thread& maintenanceThread,
    std::thread& syncThread,
    LoopCallback networkLoop,
    LoopCallback consensusLoop,
    LoopCallback maintenanceLoop,
    LoopCallback syncLoop) {
    networkThread = std::thread([loop = std::move(networkLoop)]() { loop(); });
    consensusThread = std::thread([loop = std::move(consensusLoop)]() { loop(); });
    maintenanceThread = std::thread([loop = std::move(maintenanceLoop)]() { loop(); });
    syncThread = std::thread([loop = std::move(syncLoop)]() { loop(); });
}

void stopNodeThreads(
    std::thread& networkThread,
    std::thread& consensusThread,
    std::thread& maintenanceThread,
    std::thread& syncThread) {
    if (networkThread.joinable()) networkThread.join();
    if (consensusThread.joinable()) consensusThread.join();
    if (maintenanceThread.joinable()) maintenanceThread.join();
    if (syncThread.joinable()) syncThread.join();
}

int runDaemonLoop(const DaemonRuntimeHooks& hooks) {
    if (!hooks.config || !hooks.startThreads || !hooks.stopThreads || !hooks.reload ||
        !hooks.getStats || !hooks.formatUptime || !hooks.shouldKeepRunning ||
        !hooks.consumeReloadRequested || !hooks.walletReady || !hooks.walletAddress ||
        !hooks.networkOnline) {
        utils::Logger::error("Invalid daemon runtime hooks");
        return 1;
    }

    utils::Logger::info("Running in daemon mode");

    std::cout << "\n=== SynapseNet Node Status ===\n";
    std::cout << "Mode: Daemon (no TUI)\n";
    std::cout << "Data Directory: " << hooks.config->dataDir << "\n";
    std::cout << "Network Port: " << hooks.config->port << "\n";
    std::cout << "RPC Port: " << hooks.config->rpcPort << "\n";

    if (hooks.walletReady()) {
        std::string address = hooks.walletAddress();
        std::cout << "Wallet Address: " << address.substr(0, 16) << "...\n";
    } else {
        std::cout << "Wallet: Not loaded\n";
    }

    if (hooks.networkOnline()) {
        std::cout << "Network: Online\n";
    } else {
        std::cout << "Network: Offline\n";
    }

    std::cout << "\nNode is running. Press Ctrl+C to stop.\n";
    std::cout << "Logs are written to: " << hooks.config->dataDir << "/synapsenet.log\n\n";

    hooks.startThreads();

    int statusCounter = 0;
    while (hooks.shouldKeepRunning()) {
        if (hooks.consumeReloadRequested()) {
            hooks.reload();
        }

        if (statusCounter % 30 == 0) {
            const auto stats = hooks.getStats();
            std::cout << "[" << std::time(nullptr) << "] ";
            std::cout << "Uptime: " << hooks.formatUptime(stats.uptime) << ", ";
            std::cout << "Peers: " << stats.peersConnected << ", ";
            std::cout << "Knowledge: " << stats.knowledgeEntries << ", ";
            std::cout << "Sync: " << std::fixed << std::setprecision(1)
                      << (stats.syncProgress * 100.0) << "%\n";
        }

        ++statusCounter;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    hooks.stopThreads();
    return 0;
}

int runDaemonRuntimeAdapter(const DaemonRuntimeAdapterInputs& inputs) {
    if (!inputs.config || !inputs.networkThread || !inputs.consensusThread ||
        !inputs.maintenanceThread || !inputs.syncThread || !inputs.networkLoop ||
        !inputs.consensusLoop || !inputs.maintenanceLoop || !inputs.syncLoop) {
        utils::Logger::error("Invalid daemon runtime adapter inputs");
        return 1;
    }

    DaemonRuntimeHooks hooks;
    hooks.config = inputs.config;
    hooks.startThreads = [&inputs]() {
        startNodeThreads(
            *inputs.networkThread,
            *inputs.consensusThread,
            *inputs.maintenanceThread,
            *inputs.syncThread,
            inputs.networkLoop,
            inputs.consensusLoop,
            inputs.maintenanceLoop,
            inputs.syncLoop);
    };
    hooks.stopThreads = [&inputs]() {
        stopNodeThreads(
            *inputs.networkThread,
            *inputs.consensusThread,
            *inputs.maintenanceThread,
            *inputs.syncThread);
    };
    hooks.reload = inputs.reload;
    hooks.getStats = inputs.getStats;
    hooks.formatUptime = inputs.formatUptime;
    hooks.shouldKeepRunning = inputs.shouldKeepRunning;
    hooks.consumeReloadRequested = inputs.consumeReloadRequested;
    hooks.walletReady = inputs.walletReady;
    hooks.walletAddress = inputs.walletAddress;
    hooks.networkOnline = inputs.networkOnline;
    return runDaemonLoop(hooks);
}

void runNetworkLoop(const NetworkLoopHooks& hooks) {
    if (!hooks.config || !hooks.shouldKeepRunning || !hooks.torRequired ||
        !hooks.probeTorSocks || !hooks.maybeStartManagedTorRuntimeWithBackoff ||
        !hooks.setTorManaged || !hooks.resetManagedTorRestartBackoffState ||
        !hooks.setTorReachable || !hooks.refreshTorWebReadiness ||
        !hooks.allowClearnetFallback || !hooks.allowP2PFallback ||
        !hooks.setTorDegraded || !hooks.torWebReady ||
        !hooks.updateAndLogTorReadinessState || !hooks.getNetworkConfig ||
        !hooks.setNetworkConfig || !hooks.getPeers || !hooks.disconnectPeer ||
        !hooks.connectPeer || !hooks.configuredTorSocksHost ||
        !hooks.configuredTorSocksPort) {
        utils::Logger::error("Invalid network runtime hooks");
        return;
    }

    uint64_t lastAnnounce = 0;
    uint64_t lastPeerRefresh = 0;
    bool p2pBlockedPrev = false;

    while (hooks.shouldKeepRunning()) {
        uint64_t now = std::time(nullptr);

        const bool torRequired = hooks.torRequired();
        bool torReachable = hooks.probeTorSocks();
        if (torRequired && !torReachable) {
            bool started = hooks.maybeStartManagedTorRuntimeWithBackoff(true, "network_loop");
            if (started) {
                hooks.setTorManaged(true);
                torReachable = hooks.probeTorSocks();
            }
        }
        if (torReachable) {
            hooks.resetManagedTorRestartBackoffState();
        }
        hooks.setTorReachable(torReachable);
        hooks.refreshTorWebReadiness(torReachable, false);

        core::TorRoutePolicyInput routeIn;
        routeIn.torRequired = torRequired;
        routeIn.torReachable = torReachable;
        routeIn.allowClearnetFallback = hooks.allowClearnetFallback();
        routeIn.allowP2PFallback = hooks.allowP2PFallback();
        const auto route = core::evaluateTorRoutePolicy(routeIn);
        hooks.setTorDegraded(route.torDegraded);
        hooks.updateAndLogTorReadinessState(
            torRequired,
            torReachable,
            hooks.torWebReady(),
            route.torDegraded);

        network::NetworkConfig netCfg = hooks.getNetworkConfig();
        bool netCfgChanged = false;
        if (torRequired) {
            const bool useProxy = torReachable;
            const std::string socksHost = hooks.configuredTorSocksHost();
            const uint16_t socksPort = hooks.configuredTorSocksPort();
            const uint32_t maxOutbound = route.allowP2PDiscovery ? hooks.config->maxOutbound : 0;

            if (netCfg.useSocksProxy != useProxy) {
                netCfg.useSocksProxy = useProxy;
                netCfgChanged = true;
            }
            if (netCfg.socksProxyHost != socksHost) {
                netCfg.socksProxyHost = socksHost;
                netCfgChanged = true;
            }
            if (netCfg.socksProxyPort != socksPort) {
                netCfg.socksProxyPort = socksPort;
                netCfgChanged = true;
            }
            if (netCfg.maxOutbound != maxOutbound) {
                netCfg.maxOutbound = maxOutbound;
                netCfgChanged = true;
            }
            if (!netCfg.hybridMode) {
                netCfg.hybridMode = true;
                netCfgChanged = true;
            }
        } else {
            const bool socksAvailable = hooks.probeTorSocks();
            if (socksAvailable) {
                if (!netCfg.hybridMode) {
                    netCfg.hybridMode = true;
                    netCfg.useSocksProxy = false;
                    netCfg.socksProxyHost = hooks.configuredTorSocksHost();
                    netCfg.socksProxyPort = hooks.configuredTorSocksPort();
                    netCfgChanged = true;
                }
            } else {
                if (netCfg.hybridMode) {
                    netCfg.hybridMode = false;
                    netCfgChanged = true;
                }
                if (netCfg.useSocksProxy) {
                    netCfg.useSocksProxy = false;
                    netCfgChanged = true;
                }
            }
            if (netCfg.maxOutbound != hooks.config->maxOutbound) {
                netCfg.maxOutbound = hooks.config->maxOutbound;
                netCfgChanged = true;
            }
        }
        if (netCfgChanged) {
            hooks.setNetworkConfig(netCfg);
        }

        if (!route.allowP2PDiscovery) {
            if (!p2pBlockedPrev) {
                utils::Logger::warn("Tor-required fail-closed mode active: outbound P2P is blocked");
                p2pBlockedPrev = true;
            }
            for (const auto& peer : hooks.getPeers()) {
                if (peer.isOutbound) {
                    hooks.disconnectPeer(peer.id);
                }
            }
            for (int i = 0; i < 300 && hooks.shouldKeepRunning(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }
        if (p2pBlockedPrev) {
            utils::Logger::info("Tor-required fail-closed mode cleared: outbound P2P resumed");
            p2pBlockedPrev = false;
        }

        if (hooks.refreshDiscoveryFromPeers && now - lastPeerRefresh > 30) {
            hooks.refreshDiscoveryFromPeers();
            lastPeerRefresh = now;
        }

        if (hooks.announceDiscovery && now - lastAnnounce > 300) {
            hooks.announceDiscovery();
            lastAnnounce = now;
        }

        if (hooks.getBootstrapNodes) {
            std::unordered_set<std::string> connectedAddrs;
            for (const auto& peer : hooks.getPeers()) {
                connectedAddrs.insert(peer.address + ":" + std::to_string(peer.port));
            }
            auto boots = hooks.getBootstrapNodes();
            for (const auto& bn : boots) {
                const std::string id = bn.address + ":" + std::to_string(bn.port);
                if (connectedAddrs.count(id) == 0) {
                    hooks.connectPeer(bn.address, bn.port);
                }
            }
        }

        if (hooks.config->discovery && hooks.getPeers().size() < hooks.config->maxOutbound && hooks.getRandomPeers) {
            auto peers = hooks.getRandomPeers(10);
            for (const auto& peer : peers) {
                if (hooks.getPeers().size() >= hooks.config->maxOutbound) {
                    break;
                }
                hooks.connectPeer(peer.address, peer.port);
            }
        }

        std::unordered_set<std::string> connected;
        for (const auto& peer : hooks.getPeers()) {
            connected.insert(peer.address + ":" + std::to_string(peer.port));
        }

        auto connectToNode = [&hooks, &connected](const std::string& node) {
            const size_t colonPos = node.find(':');
            if (colonPos == std::string::npos) {
                return;
            }
            const std::string host = node.substr(0, colonPos);
            const uint16_t port = static_cast<uint16_t>(std::stoi(node.substr(colonPos + 1)));
            const std::string id = host + ":" + std::to_string(port);
            if (connected.count(id) == 0) {
                hooks.connectPeer(host, port);
            }
        };

        for (const auto& node : hooks.config->connectNodes) {
            connectToNode(node);
        }
        for (const auto& node : hooks.config->addNodes) {
            connectToNode(node);
        }

        for (int i = 0; i < 100 && hooks.shouldKeepRunning(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void runNetworkRuntimeAdapter(const NetworkRuntimeAdapterInputs& inputs) {
    if (!inputs.network) {
        utils::Logger::error("Invalid network runtime adapter inputs");
        return;
    }

    NetworkLoopHooks hooks;
    hooks.config = inputs.config;
    hooks.shouldKeepRunning = inputs.shouldKeepRunning;
    hooks.torRequired = inputs.torRequired;
    hooks.probeTorSocks = inputs.probeTorSocks;
    hooks.maybeStartManagedTorRuntimeWithBackoff = inputs.maybeStartManagedTorRuntimeWithBackoff;
    hooks.setTorManaged = inputs.setTorManaged;
    hooks.resetManagedTorRestartBackoffState = inputs.resetManagedTorRestartBackoffState;
    hooks.setTorReachable = inputs.setTorReachable;
    hooks.refreshTorWebReadiness = inputs.refreshTorWebReadiness;
    hooks.allowClearnetFallback = inputs.allowClearnetFallback;
    hooks.allowP2PFallback = inputs.allowP2PFallback;
    hooks.setTorDegraded = inputs.setTorDegraded;
    hooks.torWebReady = inputs.torWebReady;
    hooks.updateAndLogTorReadinessState = inputs.updateAndLogTorReadinessState;
    hooks.getNetworkConfig = [&inputs]() { return inputs.network->getConfig(); };
    hooks.setNetworkConfig = [&inputs](const network::NetworkConfig& cfg) { inputs.network->setConfig(cfg); };
    hooks.getPeers = [&inputs]() { return inputs.network->getPeers(); };
    hooks.disconnectPeer = [&inputs](const std::string& peerId) { inputs.network->disconnect(peerId); };
    hooks.connectPeer = [&inputs](const std::string& host, uint16_t port) { inputs.network->connect(host, port); };
    if (inputs.discovery) {
        hooks.refreshDiscoveryFromPeers = [&inputs]() { inputs.discovery->refreshFromPeers(); };
        hooks.announceDiscovery = [&inputs]() { inputs.discovery->announce(); };
        hooks.getBootstrapNodes = [&inputs]() { return inputs.discovery->getBootstrapNodes(); };
        hooks.getRandomPeers = [&inputs](size_t count) { return inputs.discovery->getRandomPeers(count); };
    }
    hooks.configuredTorSocksHost = inputs.configuredTorSocksHost;
    hooks.configuredTorSocksPort = inputs.configuredTorSocksPort;
    runNetworkLoop(hooks);
}

void runSyncLoop(const SyncLoopHooks& hooks) {
    if (!hooks.shouldKeepRunning || !hooks.localHeight || !hooks.networkHeight ||
        !hooks.setSyncProgress || !hooks.hasLedger || !hooks.setSyncing ||
        !hooks.getPeers || !hooks.pruneRequestedBlocks || !hooks.reserveRequestedBlock ||
        !hooks.requestBlock) {
        utils::Logger::error("Invalid sync runtime hooks");
        return;
    }

    while (hooks.shouldKeepRunning()) {
        const uint64_t localHeight = hooks.localHeight();
        const uint64_t netHeight = hooks.networkHeight();
        if (netHeight == 0) {
            hooks.setSyncProgress(1.0);
        } else {
            double progress = static_cast<double>(localHeight) / static_cast<double>(netHeight);
            hooks.setSyncProgress(progress > 1.0 ? 1.0 : progress);
        }

        if (!hooks.hasLedger()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        if (netHeight > localHeight) {
            hooks.setSyncing(true);
            auto peers = hooks.getPeers();
            if (!peers.empty()) {
                const uint64_t now = std::time(nullptr);
                size_t inFlight = hooks.pruneRequestedBlocks(now);
                constexpr size_t maxInFlight = 16;
                uint64_t nextHeight = localHeight;
                while (nextHeight < netHeight && inFlight < maxInFlight) {
                    if (hooks.reserveRequestedBlock(nextHeight, now)) {
                        const auto& peer = peers[nextHeight % peers.size()];
                        hooks.requestBlock(peer.id, nextHeight);
                        ++inFlight;
                    }
                    ++nextHeight;
                }
            }
        } else {
            hooks.setSyncing(false);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void runSyncRuntimeAdapter(const SyncRuntimeAdapterInputs& inputs) {
    if (!inputs.networkHeight || !inputs.syncProgress || !inputs.syncing ||
        !inputs.requestedBlocks || !inputs.syncMtx || !inputs.requestBlock) {
        utils::Logger::error("Invalid sync runtime adapter inputs");
        return;
    }

    SyncLoopHooks hooks;
    hooks.shouldKeepRunning = inputs.shouldKeepRunning;
    hooks.localHeight = [&inputs]() { return inputs.ledger ? inputs.ledger->height() : 0; };
    hooks.networkHeight = [&inputs]() { return inputs.networkHeight->load(); };
    hooks.setSyncProgress = [&inputs](double progress) { *inputs.syncProgress = progress; };
    hooks.hasLedger = [&inputs]() { return inputs.ledger != nullptr; };
    hooks.setSyncing = [&inputs](bool syncing) { inputs.syncing->store(syncing); };
    hooks.getPeers = [&inputs]() {
        if (!inputs.network) {
            return std::vector<network::Peer>{};
        }
        return inputs.network->getPeers();
    };
    hooks.pruneRequestedBlocks = [&inputs](uint64_t now) {
        size_t inFlight = 0;
        std::lock_guard<std::mutex> lock(*inputs.syncMtx);
        for (auto it = inputs.requestedBlocks->begin(); it != inputs.requestedBlocks->end();) {
            if (now - it->second > 10) {
                it = inputs.requestedBlocks->erase(it);
            } else {
                ++inFlight;
                ++it;
            }
        }
        return inFlight;
    };
    hooks.reserveRequestedBlock = [&inputs](uint64_t height, uint64_t now) {
        std::lock_guard<std::mutex> lock(*inputs.syncMtx);
        if (inputs.requestedBlocks->count(height) != 0) {
            return false;
        }
        (*inputs.requestedBlocks)[height] = now;
        return true;
    };
    hooks.requestBlock = inputs.requestBlock;
    runSyncLoop(hooks);
}

void runConsensusRuntimeAdapter(
    const std::function<bool()>& shouldKeepRunning,
    const std::function<void()>& processTimeouts) {
    if (!shouldKeepRunning || !processTimeouts) {
        utils::Logger::error("Invalid consensus runtime adapter inputs");
        return;
    }

    while (shouldKeepRunning()) {
        processTimeouts();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void runMaintenanceLoop(const MaintenanceLoopHooks& hooks) {
    if (!hooks.config || !hooks.shouldKeepRunning || !hooks.tickNaanCoordinationSupervised ||
        !hooks.applyPoeSelfValidatorBootstrapPolicy || !hooks.maybeRunAutoPoeEpoch ||
        !hooks.autoVoteSweepAllPending || !hooks.processPoeSyncRetries ||
        !hooks.compactDatabase || !hooks.performQuantumMaintenance ||
        !hooks.handleBlockBuildTick || !hooks.processRemoteModelMaintenance ||
        !hooks.maybeBroadcastLocalOffer) {
        utils::Logger::error("Invalid maintenance runtime hooks");
        return;
    }

    uint64_t lastCompact = 0;
    uint64_t lastQuantum = 0;
    uint64_t lastBlock = 0;
    uint64_t lastOfferBroadcast = 0;
    uint64_t lastAutoVoteSweep = 0;

    while (hooks.shouldKeepRunning()) {
        const uint64_t now = std::time(nullptr);
        hooks.tickNaanCoordinationSupervised(now);
        hooks.applyPoeSelfValidatorBootstrapPolicy(now);
        hooks.maybeRunAutoPoeEpoch(now);

        if (now - lastAutoVoteSweep >= 5) {
            hooks.autoVoteSweepAllPending();
            lastAutoVoteSweep = now;
        }

        const uint32_t limitEpochs = hooks.config->dev ? 128 : 64;
        hooks.processPoeSyncRetries(now, limitEpochs);

        if (now - lastCompact >= 600) {
            hooks.compactDatabase();
            lastCompact = now;
        }

        if (now - lastQuantum >= 60) {
            hooks.performQuantumMaintenance();
            lastQuantum = now;
        }

        if (now - lastBlock >= 15) {
            hooks.handleBlockBuildTick();
            lastBlock = now;
        }

        hooks.processRemoteModelMaintenance(now);

        if (now - lastOfferBroadcast >= 30) {
            if (hooks.maybeBroadcastLocalOffer(now)) {
                lastOfferBroadcast = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace synapse::node
