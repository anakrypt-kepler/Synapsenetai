#pragma once

#include "network.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace synapse {
namespace network {

enum class DiscoveryPeerState {
    UNKNOWN,
    CONNECTING,
    CONNECTED,
    DISCONNECTED,
    BANNED
};

struct BootstrapNode {
    std::string address;
    uint16_t port;
    uint64_t lastSeen;
    uint32_t failures;
    bool active;
    uint64_t quarantineUntil;
    uint64_t lastFailureAt;
    double healthScore;
};

struct PeerInfo {
    std::string address;
    uint16_t port;
    uint64_t services;
    uint64_t timestamp;
    uint64_t lastSeen;
    std::string nodeId;
    std::string userAgent;
    uint32_t protocolVersion;
    uint64_t startHeight;
    DiscoveryPeerState state;
    double latency;
    uint64_t bytesSent;
    uint64_t bytesReceived;
    uint32_t attempts;
    uint64_t nextRetryAt = 0; // Unix timestamp (seconds) before which reconnect is suppressed
};

struct DiscoveryConfig {
    uint32_t maxKnownPeers = 1000;
    uint32_t maxPeers = 125;
    uint32_t minPeers = 8;
    uint32_t announceInterval = 300;
    uint32_t refreshInterval = 60;
    uint32_t discoveryInterval = 30;
    uint32_t maxFailures = 3;
    uint32_t peerTimeout = 3600;
    uint32_t bootstrapQuarantineSeconds = 600;
    uint32_t minimumBootstrapNodes = 3;
    bool enableDNS = true;
    bool enablePeerExchange = true;
    bool enableUPnP = true;
    bool enableNatPmp = true;
};

struct DiscoveryStats {
    uint64_t totalDiscovered;
    uint64_t totalConnected;
    uint64_t totalFailed;
    uint64_t dnsQueries;
    uint64_t peerExchanges;
    uint64_t uptime;
    uint64_t goodPeers;
    uint64_t badPeers;
    uint64_t bannedPeers;
    uint64_t networkSize; // Estimated network size
    double avgLatency;
    uint64_t connectedPeers;
    uint64_t knownPeersCount;
    uint64_t lastRefreshTime;
    uint64_t lastAnnounceTime;
    double peerExchangeSuccessRate; // Percentage of successful exchanges
    uint64_t activeBootstrapNodes;
    uint64_t quarantinedBootstrapNodes;
    uint64_t uniqueSeedDomains;
};

class Discovery {
public:
    Discovery();
    ~Discovery();
    
    bool start(uint16_t localPort);
    void stop();
    bool isRunning() const;
    void setConfig(const DiscoveryConfig& config);
    DiscoveryConfig getConfig() const;
    
    void addBootstrap(const std::string& address, uint16_t port);
    void addDnsSeed(const std::string& hostname);
    void addPeer(const PeerInfo& peer);
    void addKnownPeer(const PeerInfo& peer);
    void removePeer(const std::string& address);
    void markPeerFailed(const std::string& address);
    void markPeerSuccess(const std::string& address);
    
    void banPeer(const std::string& address);
    void unbanPeer(const std::string& address);
    bool isBanned(const std::string& address) const;
    std::vector<std::string> getBannedPeers() const;
    
    std::vector<BootstrapNode> getBootstrapNodes() const;
    std::vector<std::string> getDnsSeeds() const;
    std::vector<PeerInfo> getKnownPeers(size_t limit = 100) const;
    std::vector<PeerInfo> getRandomPeers(size_t count) const;
    std::vector<std::string> discoverPeers(size_t count);
    
    PeerInfo getPeer(const std::string& address) const;
    bool hasPeer(const std::string& address) const;
    
    void refreshFromDNS();
    void refreshFromPeers();
    void announce();
    void processIncoming(const std::vector<PeerInfo>& peers);
    
    void onPeerDiscovered(std::function<void(const PeerInfo&)> callback);
    void onPeerLost(std::function<void(const std::string&)> callback);
    
    size_t knownPeerCount() const;
    size_t activePeerCount() const;
    DiscoveryStats getStats() const;
    
    std::vector<PeerInfo> getGoodPeers(size_t count) const;
    std::vector<PeerInfo> getNewPeers(size_t count) const;
    void pruneOldPeers(uint64_t maxAge);
    bool exportPeers(const std::string& path) const;
    bool importPeers(const std::string& path);
    
    bool setupUPnP(uint16_t port);
    bool setupNatPmp(uint16_t port);
    std::string getExternalAddress() const;
    void setExternalAddress(const std::string& address);
    void setDataDir(const std::string& dir);
    
    // Set callback for sending messages through Network
    void setSendMessageCallback(std::function<bool(const std::string& peerId, const std::string& command, const std::vector<uint8_t>& payload)> callback);
    void setGetConnectedPeersCallback(std::function<std::vector<std::string>()> callback);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
