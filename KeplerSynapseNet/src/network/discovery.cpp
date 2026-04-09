#include "network/discovery.h"
#include "infrastructure/messages.h"
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <random>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <utility>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cctype>
#include <cmath>

namespace synapse {
namespace network {

struct Discovery::Impl {
    std::vector<BootstrapNode> bootstrapNodes;
    std::vector<std::string> dnsSeeds;
    std::unordered_map<std::string, PeerInfo> knownPeers;
    std::unordered_set<std::string> bannedPeers;
    std::unordered_set<std::string> triedPeers;
    DiscoveryConfig config;
    mutable std::mutex mtx;
    std::atomic<bool> running{false};
    uint16_t localPort = 0;
    std::thread refreshThread;
    std::function<void(const PeerInfo&)> discoveredCallback;
    std::function<void(const std::string&)> lostCallback;
    std::function<bool(const std::string& peerId, const std::string& command, const std::vector<uint8_t>& payload)> sendMessageCallback;
    std::function<std::vector<std::string>()> getConnectedPeersCallback;
    std::mt19937 rng{std::random_device{}()};
    uint64_t startTime = 0;
    uint64_t totalDiscovered = 0;
    uint64_t totalConnected = 0;
    uint64_t totalFailed = 0;
    uint64_t dnsQueries = 0;
    uint64_t peerExchanges = 0;
    uint64_t peerExchangeSuccesses = 0;
    uint64_t lastPeerRefresh = 0;
    uint64_t lastAnnounceTime = 0;
    std::unordered_map<std::string, uint64_t> lastGetPeersRequest; // peerId -> timestamp
    uint64_t lastGetPeersCleanup = 0;
    std::string externalAddress;
    uint64_t totalPeersReportedByNeighbors = 0;
    uint64_t neighborReportCount = 0;
    std::string dataDir;
    uint64_t estimatedNetworkSize = 0; // Cached estimate
    size_t bootstrapCursor = 0;
    
    void refreshLoop();
    std::vector<std::string> resolveDNS(const std::string& hostname);
    uint64_t estimateNetworkSize() const;
    void refreshBootstrapHealthLocked(uint64_t now);
    void markBootstrapSuccessLocked(const std::string& address, uint16_t port, uint64_t now);
    void markBootstrapFailureLocked(const std::string& address, uint16_t port, uint64_t now);
    std::vector<BootstrapNode> activeBootstrapSortedLocked(uint64_t now) const;
    void loadBansFromFile();
    void saveBansToFile() const;
};

void Discovery::Impl::refreshBootstrapHealthLocked(uint64_t now) {
    for (auto& node : bootstrapNodes) {
        if (node.quarantineUntil > 0 && now >= node.quarantineUntil) {
            node.quarantineUntil = 0;
            node.active = true;
            if (node.failures > 0) node.failures -= 1;
            node.healthScore = std::max(0.10, node.healthScore);
        }
        if (node.quarantineUntil > now) {
            node.active = false;
        } else if (node.quarantineUntil == 0) {
            node.active = true;
        }
    }
}

void Discovery::Impl::markBootstrapSuccessLocked(const std::string& address, uint16_t port, uint64_t now) {
    for (auto& node : bootstrapNodes) {
        if (node.address != address) continue;
        if (port != 0 && node.port != port) continue;
        node.lastSeen = now;
        node.lastFailureAt = 0;
        node.failures = 0;
        node.quarantineUntil = 0;
        node.active = true;
        node.healthScore = std::min(1.0, node.healthScore + 0.20);
    }
}

void Discovery::Impl::markBootstrapFailureLocked(const std::string& address, uint16_t port, uint64_t now) {
    for (auto& node : bootstrapNodes) {
        if (node.address != address) continue;
        if (port != 0 && node.port != port) continue;
        node.lastFailureAt = now;
        node.failures += 1;
        node.healthScore = std::max(0.0, node.healthScore - 0.25);
        if (node.failures >= config.maxFailures || node.healthScore < 0.15) {
            node.quarantineUntil = now + std::max<uint32_t>(30, config.bootstrapQuarantineSeconds);
            node.active = false;
        }
    }
}

std::vector<BootstrapNode> Discovery::Impl::activeBootstrapSortedLocked(uint64_t now) const {
    std::vector<BootstrapNode> out;
    out.reserve(bootstrapNodes.size());
    for (const auto& node : bootstrapNodes) {
        if (node.quarantineUntil > now) continue;
        if (!node.active) continue;
        out.push_back(node);
    }
    std::sort(out.begin(), out.end(), [](const BootstrapNode& a, const BootstrapNode& b) {
        if (a.healthScore != b.healthScore) return a.healthScore > b.healthScore;
        if (a.failures != b.failures) return a.failures < b.failures;
        if (a.lastSeen != b.lastSeen) return a.lastSeen > b.lastSeen;
        if (a.address != b.address) return a.address < b.address;
        return a.port < b.port;
    });
    return out;
}

void Discovery::Impl::refreshLoop() {
    uint64_t lastRefresh = 0;
    
    while (running) {
        uint64_t now = std::time(nullptr);
        std::vector<std::pair<std::string, uint16_t>> dnsTargets;
        {
            std::lock_guard<std::mutex> lock(mtx);
            refreshBootstrapHealthLocked(now);
            for (const auto& seed : dnsSeeds) {
                uint16_t seedPort = localPort;
                bool blocked = false;
                for (const auto& node : bootstrapNodes) {
                    if (node.address != seed) continue;
                    seedPort = node.port;
                    if (node.quarantineUntil > now || !node.active) {
                        blocked = true;
                    }
                    break;
                }
                if (!blocked) dnsTargets.emplace_back(seed, seedPort);
            }
        }
        
        if (config.enableDNS && now - lastRefresh > config.refreshInterval) {
            for (const auto& target : dnsTargets) {
                auto addrs = resolveDNS(target.first);
                dnsQueries++;
                std::lock_guard<std::mutex> lock(mtx);
                for (const auto& addr : addrs) {
                    if (knownPeers.size() >= config.maxKnownPeers) break;
                    PeerInfo info{};
                    info.address = addr;
                    info.port = target.second;
                    info.timestamp = now;
                    info.lastSeen = now;
                    info.services = 1;
                    info.state = DiscoveryPeerState::UNKNOWN;
                    info.attempts = 0;
                    std::string key = addr + ":" + std::to_string(info.port);
                    if (knownPeers.find(key) == knownPeers.end()) {
                        knownPeers[key] = info;
                        totalDiscovered++;
                        if (discoveredCallback) discoveredCallback(info);
                    }
                }
                if (!addrs.empty()) {
                    markBootstrapSuccessLocked(target.first, target.second, now);
                } else {
                    markBootstrapFailureLocked(target.first, target.second, now);
                }
            }
            lastRefresh = now;
        }
        
        // Peer exchange refresh
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (config.enablePeerExchange && now - lastPeerRefresh > config.discoveryInterval) {
                if (sendMessageCallback && getConnectedPeersCallback) {
                    auto connectedPeers = getConnectedPeersCallback();
                    for (const auto& peerId : connectedPeers) {
                        // Rate limiting: max 1 request per minute per peer
                        auto it = lastGetPeersRequest.find(peerId);
                        if (it != lastGetPeersRequest.end() && now - it->second < 60) {
                            continue; // Skip if requested too recently
                        }
                        lastGetPeersRequest[peerId] = now;
                        sendMessageCallback(peerId, "getpeers", {});
                    }
                    lastPeerRefresh = now;

                    if (now - lastGetPeersCleanup > 60) {
                        for (auto it = lastGetPeersRequest.begin(); it != lastGetPeersRequest.end(); ) {
                            if (now - it->second > 600) it = lastGetPeersRequest.erase(it);
                            else ++it;
                        }
                        lastGetPeersCleanup = now;
                    }
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

uint64_t Discovery::Impl::estimateNetworkSize() const {
    size_t known = knownPeers.size();
    if (known == 0) return 0;

    const uint64_t MAX_ESTIMATE = 10000000ULL;
    uint64_t estimate = 0;
    if (neighborReportCount > 0) {
        double avgReported = static_cast<double>(totalPeersReportedByNeighbors) / static_cast<double>(neighborReportCount);
        uint64_t avg = static_cast<uint64_t>(std::ceil(avgReported));
        estimate = std::max<uint64_t>(static_cast<uint64_t>(known), avg);
        if (estimate < static_cast<uint64_t>(known) * 3) estimate = static_cast<uint64_t>(known) * 3;
        if (estimate > MAX_ESTIMATE) estimate = MAX_ESTIMATE;
    } else {
        estimate = static_cast<uint64_t>(known) * 10;
        if (estimate > MAX_ESTIMATE) estimate = MAX_ESTIMATE;
    }
    return estimate;
}

std::vector<std::string> Discovery::Impl::resolveDNS(const std::string& hostname) {
    std::vector<std::string> results;
    struct addrinfo hints{}, *res = nullptr, *p = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &res) != 0) {
        return results;
    }

    char ipstr[INET6_ADDRSTRLEN];
    for (p = res; p != nullptr; p = p->ai_next) {
        if (p->ai_family == AF_INET) {
            struct sockaddr_in* ipv4 = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);
            inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, INET_ADDRSTRLEN);
            results.push_back(ipstr);
        } else if (p->ai_family == AF_INET6) {
            struct sockaddr_in6* ipv6 = reinterpret_cast<struct sockaddr_in6*>(p->ai_addr);
            inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipstr, INET6_ADDRSTRLEN);
            results.push_back(ipstr);
        } else {
            continue;
        }
    }

    freeaddrinfo(res);
    return results;
}

void Discovery::Impl::loadBansFromFile() {
    if (dataDir.empty()) return;
    std::string path = dataDir + "/banned_peers.txt";
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        // Trim whitespace
        size_t b = 0;
        while (b < line.size() && std::isspace(static_cast<unsigned char>(line[b]))) b++;
        size_t e = line.size();
        while (e > b && std::isspace(static_cast<unsigned char>(line[e-1]))) e--;
        if (e <= b) continue;
        if (line[b] == '#') continue;
        std::string addr = line.substr(b, e - b);
        if (!addr.empty()) bannedPeers.insert(addr);
    }
}

void Discovery::Impl::saveBansToFile() const {
    if (dataDir.empty()) return;
    std::string path = dataDir + "/banned_peers.txt";
    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    out << "# SynapseNet banned peers - auto-generated\n";
    for (const auto& addr : bannedPeers) {
        out << addr << "\n";
    }
}

Discovery::Discovery() : impl_(std::make_unique<Impl>()) {}
Discovery::~Discovery() { stop(); }

bool Discovery::start(uint16_t localPort) {
    if (impl_->running) return false;
    impl_->localPort = localPort;
    impl_->loadBansFromFile();
    impl_->running = true;
    impl_->startTime = std::time(nullptr);
    impl_->refreshThread = std::thread(&Discovery::Impl::refreshLoop, impl_.get());
    return true;
}

void Discovery::stop() {
    impl_->running = false;
    if (impl_->refreshThread.joinable()) {
        impl_->refreshThread.join();
    }
}

bool Discovery::isRunning() const {
    return impl_->running;
}

void Discovery::setConfig(const DiscoveryConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config = config;
}

DiscoveryConfig Discovery::getConfig() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->config;
}

void Discovery::addBootstrap(const std::string& address, uint16_t port) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t now = std::time(nullptr);
    BootstrapNode node;
    node.address = address;
    node.port = port;
    node.lastSeen = 0;
    node.failures = 0;
    node.active = true;
    node.quarantineUntil = 0;
    node.lastFailureAt = 0;
    node.healthScore = 1.0;
    impl_->bootstrapNodes.push_back(node);
    
    PeerInfo info{};
    info.address = address;
    info.port = port;
    info.timestamp = now;
    info.lastSeen = info.timestamp;
    info.services = 1;
    info.state = DiscoveryPeerState::UNKNOWN;
    info.attempts = 0;
    std::string key = address + ":" + std::to_string(port);
    impl_->knownPeers[key] = info;
}

void Discovery::addDnsSeed(const std::string& hostname) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->dnsSeeds.push_back(hostname);
}

void Discovery::addPeer(const PeerInfo& peer) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->knownPeers.size() >= impl_->config.maxKnownPeers) return;
    std::string key = peer.address + ":" + std::to_string(peer.port);
    impl_->knownPeers[key] = peer;
    impl_->totalDiscovered++;
}

void Discovery::addKnownPeer(const PeerInfo& peer) {
    addPeer(peer);
}

void Discovery::removePeer(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    for (auto it = impl_->knownPeers.begin(); it != impl_->knownPeers.end(); ) {
        if (it->second.address == address) {
            it = impl_->knownPeers.erase(it);
        } else {
            ++it;
        }
    }
}

void Discovery::markPeerFailed(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    const uint64_t now = std::time(nullptr);
    for (auto& [key, peer] : impl_->knownPeers) {
        if (peer.address == address) {
            peer.attempts++;
            // exponential backoff: cap exponent to 6 (1<<6 = 64)
            unsigned exp = std::min<uint32_t>(peer.attempts, 6);
            uint64_t backoff = 30ULL * (1ULL << exp);
            if (backoff > 3600ULL) backoff = 3600ULL;
            peer.nextRetryAt = now + backoff;
            impl_->totalFailed++;
            if (peer.attempts >= impl_->config.maxFailures) {
                peer.state = DiscoveryPeerState::DISCONNECTED;
            }
            break;
        }
    }
    impl_->markBootstrapFailureLocked(address, 0, now);
    impl_->refreshBootstrapHealthLocked(now);
}

void Discovery::markPeerSuccess(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    const uint64_t now = std::time(nullptr);
    for (auto& [key, peer] : impl_->knownPeers) {
        if (peer.address == address) {
            peer.attempts = 0;
            peer.lastSeen = now;
            peer.state = DiscoveryPeerState::CONNECTED;
            impl_->totalConnected++;
            break;
        }
    }
    impl_->markBootstrapSuccessLocked(address, 0, now);
    impl_->refreshBootstrapHealthLocked(now);
}

void Discovery::banPeer(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->bannedPeers.insert(address);
    impl_->saveBansToFile();
}

void Discovery::unbanPeer(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->bannedPeers.erase(address);
    impl_->saveBansToFile();
}

bool Discovery::isBanned(const std::string& address) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->bannedPeers.count(address) > 0;
}

std::vector<std::string> Discovery::getBannedPeers() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return std::vector<std::string>(impl_->bannedPeers.begin(), impl_->bannedPeers.end());
}

std::vector<BootstrapNode> Discovery::getBootstrapNodes() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t now = std::time(nullptr);
    impl_->refreshBootstrapHealthLocked(now);
    auto nodes = impl_->bootstrapNodes;
    std::sort(nodes.begin(), nodes.end(), [](const BootstrapNode& a, const BootstrapNode& b) {
        if (a.active != b.active) return a.active > b.active;
        if (a.healthScore != b.healthScore) return a.healthScore > b.healthScore;
        if (a.failures != b.failures) return a.failures < b.failures;
        if (a.address != b.address) return a.address < b.address;
        return a.port < b.port;
    });
    return nodes;
}

std::vector<std::string> Discovery::getDnsSeeds() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->dnsSeeds;
}

std::vector<PeerInfo> Discovery::getKnownPeers(size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<PeerInfo> result;
    for (const auto& [key, peer] : impl_->knownPeers) {
        if (result.size() >= limit) break;
        result.push_back(peer);
    }
    return result;
}

std::vector<PeerInfo> Discovery::getRandomPeers(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<PeerInfo> all;
    uint64_t now = std::time(nullptr);
    for (const auto& [key, peer] : impl_->knownPeers) {
        if (peer.nextRetryAt > now) continue;
        all.push_back(peer);
    }
    std::shuffle(all.begin(), all.end(), impl_->rng);
    if (all.size() > count) all.resize(count);
    return all;
}

std::vector<std::string> Discovery::discoverPeers(size_t count) {
    std::vector<std::string> result;
    result.reserve(count);
    std::unordered_set<std::string> quarantinedBootstrap;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        const uint64_t now = std::time(nullptr);
        impl_->refreshBootstrapHealthLocked(now);
        for (const auto& node : impl_->bootstrapNodes) {
            if (node.quarantineUntil > now) {
                quarantinedBootstrap.insert(node.address);
            }
        }
    }

    auto peers = getRandomPeers(count);
    for (const auto& peer : peers) {
        if (result.size() >= count) break;
        if (isBanned(peer.address)) continue;
        if (quarantinedBootstrap.count(peer.address) > 0) continue;
        result.push_back(peer.address + ":" + std::to_string(peer.port));
    }

    if (result.size() >= count) return result;

    std::lock_guard<std::mutex> lock(impl_->mtx);
    const uint64_t now = std::time(nullptr);
    impl_->refreshBootstrapHealthLocked(now);
    auto active = impl_->activeBootstrapSortedLocked(now);
    if (active.empty()) return result;

    size_t cursor = impl_->bootstrapCursor % active.size();
    size_t visited = 0;
    while (result.size() < count && visited < active.size()) {
        const auto& node = active[cursor];
        std::string entry = node.address + ":" + std::to_string(node.port);
        if (std::find(result.begin(), result.end(), entry) == result.end()) {
            result.push_back(entry);
        }
        cursor = (cursor + 1) % active.size();
        visited += 1;
    }
    impl_->bootstrapCursor = cursor;
    return result;
}

PeerInfo Discovery::getPeer(const std::string& address) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    for (const auto& [key, peer] : impl_->knownPeers) {
        if (peer.address == address) return peer;
    }
    return PeerInfo{};
}

bool Discovery::hasPeer(const std::string& address) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    for (const auto& [key, peer] : impl_->knownPeers) {
        if (peer.address == address) return true;
    }
    return false;
}

void Discovery::refreshFromDNS() {
    std::vector<std::pair<std::string, uint16_t>> targets;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        const uint64_t now = std::time(nullptr);
        impl_->refreshBootstrapHealthLocked(now);
        for (const auto& seed : impl_->dnsSeeds) {
            uint16_t seedPort = impl_->localPort;
            bool blocked = false;
            for (const auto& bn : impl_->bootstrapNodes) {
                if (bn.address != seed) continue;
                seedPort = bn.port;
                if (!bn.active || bn.quarantineUntil > now) blocked = true;
                break;
            }
            if (!blocked) targets.emplace_back(seed, seedPort);
        }
    }

    for (const auto& target : targets) {
        auto addrs = impl_->resolveDNS(target.first);
        impl_->dnsQueries++;
        std::lock_guard<std::mutex> lock(impl_->mtx);
        const uint64_t now = std::time(nullptr);
        for (const auto& addr : addrs) {
            if (impl_->knownPeers.size() >= impl_->config.maxKnownPeers) break;
            PeerInfo info{};
            info.address = addr;
            info.port = target.second;
            info.timestamp = now;
            info.lastSeen = info.timestamp;
            info.state = DiscoveryPeerState::UNKNOWN;
            std::string key = addr + ":" + std::to_string(info.port);
            if (impl_->knownPeers.find(key) == impl_->knownPeers.end()) {
                impl_->knownPeers[key] = info;
                impl_->totalDiscovered++;
            }
        }
        if (!addrs.empty()) {
            impl_->markBootstrapSuccessLocked(target.first, target.second, now);
        } else {
            impl_->markBootstrapFailureLocked(target.first, target.second, now);
        }
    }
}

void Discovery::refreshFromPeers() {
    if (!impl_->sendMessageCallback || !impl_->getConnectedPeersCallback) {
        impl_->peerExchanges++;
        return;
    }
    
    uint64_t now = std::time(nullptr);
    auto connectedPeers = impl_->getConnectedPeersCallback();
    size_t successCount = 0;
    
    for (const auto& peerId : connectedPeers) {
        // Rate limiting: max 1 request per minute per peer
        {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            auto it = impl_->lastGetPeersRequest.find(peerId);
            if (it != impl_->lastGetPeersRequest.end() && now - it->second < 60) {
                continue; // Skip if requested too recently
            }
            impl_->lastGetPeersRequest[peerId] = now;
        }
        
        // Send GETPEERS request
        if (impl_->sendMessageCallback(peerId, "getpeers", {})) {
            successCount++;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->peerExchanges++;
        impl_->peerExchangeSuccesses += successCount;
        impl_->lastPeerRefresh = now;
    }
}

void Discovery::announce() {
    if (!impl_->sendMessageCallback || !impl_->getConnectedPeersCallback) {
        return;
    }
    
    auto connectedPeers = impl_->getConnectedPeersCallback();
    if (connectedPeers.empty()) {
        return;
    }
    
    // Get our external address
    std::string ourAddr = impl_->externalAddress;
    if (ourAddr.empty()) {
        // Fallback: use 0.0.0.0 as indicator that we don't know our external IP
        // Peers will see this and can potentially help us discover it
        ourAddr = "0.0.0.0";
    }
    
    // Create PEERS message with ourselves
    synapse::PeersMessage peersMsg;
    synapse::PeerAddress selfAddr{};
    selfAddr.services = 1;
    selfAddr.port = impl_->localPort;
    selfAddr.timestamp = std::time(nullptr);
    selfAddr.addr.fill(0);
    
    // Convert IP string to binary format (IPv4 mapped to IPv6)
    in_addr ipv4{};
    if (ourAddr != "0.0.0.0" && inet_pton(AF_INET, ourAddr.c_str(), &ipv4) == 1) {
        selfAddr.addr[10] = 0xff;
        selfAddr.addr[11] = 0xff;
        std::memcpy(selfAddr.addr.data() + 12, &ipv4, 4);
        peersMsg.peers.push_back(selfAddr);
    } else {
        // If address is not valid or is 0.0.0.0, skip announce
        // (We don't know our external IP yet)
        return;
    }
    
    auto payload = peersMsg.serialize();
    
    // Send to all connected peers
    for (const auto& peerId : connectedPeers) {
        impl_->sendMessageCallback(peerId, "peers", payload);
    }
    
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->lastAnnounceTime = std::time(nullptr);
    }
}

void Discovery::processIncoming(const std::vector<PeerInfo>& peers) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t now = std::time(nullptr);
    impl_->totalPeersReportedByNeighbors += peers.size();
    impl_->neighborReportCount += 1;
    
    for (const auto& peer : peers) {
        // Validate peer address
        if (peer.address.empty() || peer.port == 0) continue;
        
        // Skip localhost addresses
        if (peer.address == "127.0.0.1" || peer.address == "::1" || 
            peer.address == "localhost" || peer.address.find("127.") == 0) {
            continue;
        }
        
        // Skip banned peers
        std::string key = peer.address + ":" + std::to_string(peer.port);
        if (impl_->bannedPeers.count(peer.address) > 0) {
            continue;
        }
        
        // Skip if we already know this peer
        if (impl_->knownPeers.find(key) != impl_->knownPeers.end()) {
            // Update lastSeen if peer is already known
            impl_->knownPeers[key].lastSeen = now;
            continue;
        }
        
        // Check if we've reached max peers
        if (impl_->knownPeers.size() >= impl_->config.maxKnownPeers) break;
        
        // Add new peer
        PeerInfo newPeer = peer;
        if (newPeer.timestamp == 0) newPeer.timestamp = now;
        if (newPeer.lastSeen == 0) newPeer.lastSeen = now;
        impl_->knownPeers[key] = newPeer;
        impl_->totalDiscovered++;
        if (impl_->discoveredCallback) impl_->discoveredCallback(newPeer);
    }
}

void Discovery::onPeerDiscovered(std::function<void(const PeerInfo&)> callback) {
    impl_->discoveredCallback = callback;
}

void Discovery::onPeerLost(std::function<void(const std::string&)> callback) {
    impl_->lostCallback = callback;
}

size_t Discovery::knownPeerCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->knownPeers.size();
}

size_t Discovery::activePeerCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    size_t count = 0;
    for (const auto& [key, peer] : impl_->knownPeers) {
        if (peer.state == DiscoveryPeerState::CONNECTED) count++;
    }
    return count;
}

DiscoveryStats Discovery::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    DiscoveryStats stats{};
    const uint64_t now = std::time(nullptr);
    impl_->refreshBootstrapHealthLocked(now);
    stats.totalDiscovered = impl_->totalDiscovered;
    stats.totalConnected = impl_->totalConnected;
    stats.totalFailed = impl_->totalFailed;
    stats.dnsQueries = impl_->dnsQueries;
    stats.peerExchanges = impl_->peerExchanges;
    stats.uptime = now - impl_->startTime;
    stats.bannedPeers = impl_->bannedPeers.size();
    stats.goodPeers = 0;
    stats.badPeers = 0;
    stats.knownPeersCount = impl_->knownPeers.size();
    stats.lastRefreshTime = impl_->lastPeerRefresh;
    stats.lastAnnounceTime = impl_->lastAnnounceTime;
    
    // Calculate average latency
    double totalLatency = 0.0;
    size_t latencyCount = 0;
    for (const auto& [key, peer] : impl_->knownPeers) {
        if (peer.attempts < 3) stats.goodPeers++;
        else stats.badPeers++;
        if (peer.latency > 0) {
            totalLatency += peer.latency;
            latencyCount++;
        }
    }
    stats.avgLatency = latencyCount > 0 ? totalLatency / latencyCount : 0.0;
    
    // Estimate network size
    stats.networkSize = impl_->estimateNetworkSize();
    
    // Calculate peer exchange success rate
    if (impl_->peerExchanges > 0) {
        stats.peerExchangeSuccessRate = (static_cast<double>(impl_->peerExchangeSuccesses) / impl_->peerExchanges) * 100.0;
    } else {
        stats.peerExchangeSuccessRate = 0.0;
    }
    
    // Get connected peers count (requires callback)
    if (impl_->getConnectedPeersCallback) {
        auto connected = impl_->getConnectedPeersCallback();
        stats.connectedPeers = connected.size();
    } else {
        stats.connectedPeers = 0;
    }

    stats.activeBootstrapNodes = 0;
    stats.quarantinedBootstrapNodes = 0;
    std::set<std::string> domains;
    for (const auto& node : impl_->bootstrapNodes) {
        if (node.quarantineUntil > now) stats.quarantinedBootstrapNodes += 1;
        if (node.active && node.quarantineUntil <= now) stats.activeBootstrapNodes += 1;
        auto dot = node.address.rfind('.');
        if (dot != std::string::npos && dot > 0) {
            auto prevDot = node.address.rfind('.', dot - 1);
            if (prevDot != std::string::npos)
                domains.insert(node.address.substr(prevDot + 1));
            else
                domains.insert(node.address);
        }
    }
    stats.uniqueSeedDomains = domains.size();
    
    return stats;
}

std::vector<PeerInfo> Discovery::getGoodPeers(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<PeerInfo> result;
    for (const auto& [key, peer] : impl_->knownPeers) {
        if (peer.attempts < 3 && result.size() < count) {
            result.push_back(peer);
        }
    }
    return result;
}

std::vector<PeerInfo> Discovery::getNewPeers(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::pair<uint64_t, PeerInfo>> sorted;
    for (const auto& [key, peer] : impl_->knownPeers) {
        sorted.emplace_back(peer.timestamp, peer);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    std::vector<PeerInfo> result;
    for (size_t i = 0; i < count && i < sorted.size(); i++) {
        result.push_back(sorted[i].second);
    }
    return result;
}

void Discovery::pruneOldPeers(uint64_t maxAge) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t now = std::time(nullptr);
    for (auto it = impl_->knownPeers.begin(); it != impl_->knownPeers.end(); ) {
        if (now - it->second.lastSeen > maxAge) {
            it = impl_->knownPeers.erase(it);
        } else {
            ++it;
        }
    }
}

bool Discovery::exportPeers(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::ofstream f(path);
    if (!f) return false;
    for (const auto& [key, peer] : impl_->knownPeers) {
        f << peer.address << ":" << peer.port << "\n";
    }
    return true;
}

bool Discovery::importPeers(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find(':');
        if (pos != std::string::npos) {
            PeerInfo info{};
            info.address = line.substr(0, pos);
            info.port = std::stoi(line.substr(pos + 1));
            info.timestamp = std::time(nullptr);
            info.lastSeen = info.timestamp;
            info.state = DiscoveryPeerState::UNKNOWN;
            addPeer(info);
        }
    }
    return true;
}

bool Discovery::setupUPnP(uint16_t port) {
    return false;
}

bool Discovery::setupNatPmp(uint16_t port) {
    return false;
}

std::string Discovery::getExternalAddress() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    // If we already have an external address, return it
    if (!impl_->externalAddress.empty() && impl_->externalAddress != "0.0.0.0") {
        return impl_->externalAddress;
    }
    
    // Try to get from connected peers (they might have seen our address)
    // This is a simple heuristic: if we have connected peers, we can ask them
    // For now, return empty string if not set - will be set via announce or external discovery
    return impl_->externalAddress;
}

void Discovery::setExternalAddress(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->externalAddress = address;
}

void Discovery::setDataDir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->dataDir = dir;
}

void Discovery::setSendMessageCallback(std::function<bool(const std::string& peerId, const std::string& command, const std::vector<uint8_t>& payload)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->sendMessageCallback = callback;
}

void Discovery::setGetConnectedPeersCallback(std::function<std::vector<std::string>()> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->getConnectedPeersCallback = callback;
}

}
}
