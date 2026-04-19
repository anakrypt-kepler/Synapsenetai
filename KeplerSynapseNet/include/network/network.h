#pragma once

#include "crypto/crypto.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

namespace synapse {
namespace network {

constexpr uint32_t PROTOCOL_MAGIC = 0x53594E41;
constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr size_t MAX_MESSAGE_SIZE = 4 * 1024 * 1024;
constexpr size_t MAX_PEERS = 125;

enum class PeerState {
    CONNECTING,
    HANDSHAKING,
    CONNECTED,
    DISCONNECTING,
    DISCONNECTED,
    BANNED
};

struct Peer {
    std::string id;
    std::string address;
    uint16_t port;
    uint64_t connectedAt;
    uint64_t lastSeen;
    uint64_t bytesRecv;
    uint64_t bytesSent;
    uint32_t version;
    uint64_t startHeight;
    bool isOutbound;
    PeerState state;
    int socket;
};

struct MessageHeader {
    uint32_t magic;
    char command[12];
    uint32_t length;
    uint32_t checksum;
};

enum class MessageType {
    VERSION,
    VERACK,
    PING,
    PONG,
    GETADDR,
    ADDR,
    INV,
    GETDATA,
    NOTFOUND,
    BLOCK,
    TX,
    KNOWLEDGE,
    VOTE,
    REJECT
};

struct Message {
    MessageType type;
    std::string command;
    std::vector<uint8_t> payload;
    std::string from;
    uint64_t timestamp;
    uint64_t nonce = 0;
    
    std::vector<uint8_t> serialize() const;
    static Message deserialize(const std::vector<uint8_t>& data);
};

struct NetworkConfig {
    uint32_t maxPeers = 125;
    uint32_t maxInbound = 100;
    uint32_t maxOutbound = 25;
    uint32_t connectTimeout = 5000;
    uint32_t handshakeTimeout = 10000;
    uint32_t pingInterval = 120;
    bool enableRelay = true;
    bool useSocksProxy = false;
    std::string socksProxyHost = "127.0.0.1";
    uint16_t socksProxyPort = 9050;
    std::string socksUsername;
    std::string socksPassword;
    bool torRequired = false;
    bool hybridMode = false;
    bool adaptiveAdmission = true;
    bool deterministicEviction = true;
    uint32_t maxPeersPerIp = 8;
    uint32_t maxPeersPerSubnet = 32;
    uint8_t subnetPrefixBits = 24;
    bool enableTokenBucketDosGuard = true;
    uint32_t tokenBucketBytesPerSecond = static_cast<uint32_t>(MAX_MESSAGE_SIZE * 2);
    uint32_t tokenBucketBytesBurst = static_cast<uint32_t>(MAX_MESSAGE_SIZE * 4);
    uint32_t tokenBucketMessagesPerSecond = 500;
    uint32_t tokenBucketMessagesBurst = 1000;
    uint32_t malformedPenaltyScore = 20;
    uint32_t ratePenaltyScore = 10;
    uint32_t penaltyHalfLifeSeconds = 900;
    uint32_t baseBanSeconds = 120;
    uint32_t maxBanSeconds = 3600;
    bool enableOverloadMode = true;
    uint32_t overloadEnterPeerPercent = 90;
    uint32_t overloadExitPeerPercent = 70;
    uint64_t overloadEnterBufferedRxBytes = MAX_MESSAGE_SIZE * 32;
    uint64_t overloadExitBufferedRxBytes = MAX_MESSAGE_SIZE * 16;
    uint32_t maxOutboundQueueBytes = 10 * 1024 * 1024;
};

struct NetworkStats {
    uint64_t totalPeers;
    uint64_t inboundPeers;
    uint64_t outboundPeers;
    uint64_t bytesSent;
    uint64_t bytesReceived;
    uint64_t messagesSent;
    uint64_t messagesReceived;
    uint64_t uptime;
    bool overloadMode;
    uint64_t bufferedRxBytes;
    uint64_t rejectedConnections;
    uint64_t evictedPeers;
    uint64_t tempBans;
    uint64_t malformedMessages;
    uint64_t rateLimitedEvents;
    uint64_t overloadTransitions;
    uint64_t zombieDisconnects;
};

class Network {
public:
    Network();
    ~Network();
    
    bool start(uint16_t port);
    void stop();
    bool isRunning() const;
    
    bool connect(const std::string& address, uint16_t port);
    void disconnect(const std::string& peerId);
    void ban(const std::string& peerId, uint32_t seconds = 86400);
    
    bool broadcast(const Message& msg);
    bool send(const std::string& peerId, const Message& msg);
    
    std::vector<Peer> getPeers() const;
    Peer getPeer(const std::string& peerId) const;
    size_t peerCount() const;
    size_t outboundCount() const;
    
    void onMessage(std::function<void(const std::string&, const Message&)> handler);
    void onPeerConnected(std::function<void(const Peer&)> handler);
    void onPeerDisconnected(std::function<void(const Peer&)> handler);
    
    uint16_t getPort() const;
    std::string getLocalAddress() const;
    
    NetworkStats getStats() const;
    void setMaxPeers(uint32_t max);
    void setMaxInbound(uint32_t max);
    void setMaxOutbound(uint32_t max);
    
    bool banPeer(const std::string& peerId, uint32_t duration);
    bool unbanPeer(const std::string& peerId);
    bool isBanned(const std::string& peerId) const;
    std::vector<std::string> getBannedPeers() const;
    
    void setConfig(const NetworkConfig& config);
    NetworkConfig getConfig() const;
    bool isConnected() const;
    
    void ping(const std::string& peerId);
    void pingAll();
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct HandshakeResult {
    bool success;
    std::string error;
    std::vector<uint8_t> remoteNodeId;
    std::vector<uint8_t> remotePublicKey;
    uint32_t remoteVersion;
    uint32_t remoteCapabilities;
    std::vector<uint8_t> sessionKey;
    std::string remoteAddress;
};

class Handshake {
public:
    Handshake();
    ~Handshake();
    
    bool init(const std::vector<uint8_t>& nodeId, const std::vector<uint8_t>& publicKey,
              const std::vector<uint8_t>& privateKey);
    void setCapabilities(uint32_t caps);
    
    std::vector<uint8_t> createInitMessage();
    HandshakeResult processInitMessage(const std::vector<uint8_t>& data);
    std::vector<uint8_t> createResponseMessage(const HandshakeResult& initResult);
    HandshakeResult processResponseMessage(const std::vector<uint8_t>& data);
    
    std::vector<uint8_t> getSessionKey() const;
    bool isComplete() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct SyncStats {
    uint64_t localHeight;
    uint64_t networkHeight;
    uint64_t syncedHeight;
    bool syncing;
    int progress;
    uint64_t bytesReceived;
    size_t peerCount;
    double blocksPerSecond;
};

class Sync {
public:
    Sync();
    ~Sync();
    
    void start();
    void stop();
    bool isSyncing() const;
    bool isSynced() const;
    
    void setLocalHeight(uint64_t height);
    void updatePeerHeight(const std::string& peerId, uint64_t height);
    void removePeer(const std::string& peerId);
    
    uint64_t getLocalHeight() const;
    uint64_t getNetworkHeight() const;
    int getProgress() const;
    
    void processBlock(const std::vector<uint8_t>& blockData, uint64_t height);
    void processTimeout();
    
    std::vector<uint64_t> getPendingHeights() const;
    void setMaxConcurrentRequests(uint32_t max);
    void setRequestTimeout(uint32_t seconds);
    
    void onProgress(std::function<void(uint64_t, uint64_t)> callback);
    void onBlockReceived(std::function<void(const std::vector<uint8_t>&)> callback);
    void onSyncComplete(std::function<void()> callback);
    
    SyncStats getStats() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
