#include "network/network.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <random>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <functional>
#include <atomic>
#include <thread>
#include <openssl/rand.h>

namespace synapse {
namespace network {

enum class PeerType {
    Unknown,
    FullNode,
    LightClient,
    Validator,
    Miner,
    Relay,
    Seed
};

struct PeerAddress {
    std::string ip;
    uint16_t port;
    bool isIPv6;
    bool isOnion;
    bool isI2P;
    uint64_t services;
    time_t lastSeen;
    time_t lastTried;
    int attempts;
    int successfulConnections;
    std::string source;
};

struct PeerStats {
    uint64_t bytesSent;
    uint64_t bytesReceived;
    uint64_t messagesSent;
    uint64_t messagesReceived;
    uint64_t pingCount;
    double avgPingTime;
    double minPingTime;
    double maxPingTime;
    time_t connectedSince;
    time_t lastActivity;
    int disconnectCount;
    int banCount;
    double reliability;
};

struct PeerInfo {
    std::string id;
    PeerAddress address;
    PeerState state;
    PeerType type;
    PeerStats stats;
    std::string userAgent;
    uint32_t protocolVersion;
    uint64_t startHeight;
    uint64_t currentHeight;
    bool inbound;
    bool whitelisted;
    bool manuallyAdded;
    int score;
    std::vector<std::string> capabilities;
    std::map<std::string, std::string> metadata;
};

class PeerScoring {
private:
    std::map<std::string, int> scores;
    std::map<std::string, std::vector<std::pair<time_t, int>>> history;
    static const int MAX_SCORE = 100;
    static const int MIN_SCORE = -100;
    static const int BAN_THRESHOLD = -50;

public:
    void initialize(const std::string& peerId) {
        scores[peerId] = 0;
        history[peerId] = {};
    }

    void addPoints(const std::string& peerId, int points, const std::string& reason) {
        if (scores.find(peerId) == scores.end()) {
            initialize(peerId);
        }

        scores[peerId] = std::max(MIN_SCORE, std::min(MAX_SCORE, scores[peerId] + points));
        history[peerId].push_back({time(nullptr), points});

        if (history[peerId].size() > 1000) {
            history[peerId].erase(history[peerId].begin());
        }
    }

    void goodBehavior(const std::string& peerId) {
        addPoints(peerId, 1, "good_behavior");
    }

    void validBlock(const std::string& peerId) {
        addPoints(peerId, 5, "valid_block");
    }

    void validTransaction(const std::string& peerId) {
        addPoints(peerId, 1, "valid_transaction");
    }

    void invalidBlock(const std::string& peerId) {
        addPoints(peerId, -20, "invalid_block");
    }

    void invalidTransaction(const std::string& peerId) {
        addPoints(peerId, -5, "invalid_transaction");
    }

    void timeout(const std::string& peerId) {
        addPoints(peerId, -2, "timeout");
    }

    void protocolViolation(const std::string& peerId) {
        addPoints(peerId, -30, "protocol_violation");
    }

    void spamming(const std::string& peerId) {
        addPoints(peerId, -10, "spamming");
    }

    int getScore(const std::string& peerId) const {
        auto it = scores.find(peerId);
        return (it != scores.end()) ? it->second : 0;
    }

    bool shouldBan(const std::string& peerId) const {
        return getScore(peerId) <= BAN_THRESHOLD;
    }

    void decay() {
        for (auto& pair : scores) {
            if (pair.second > 0) {
                pair.second = std::max(0, pair.second - 1);
            } else if (pair.second < 0) {
                pair.second = std::min(0, pair.second + 1);
            }
        }
    }

    void reset(const std::string& peerId) {
        scores[peerId] = 0;
        history[peerId].clear();
    }

    std::vector<std::pair<std::string, int>> getTopPeers(int count) const {
        std::vector<std::pair<std::string, int>> sorted(scores.begin(), scores.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        if (sorted.size() > static_cast<size_t>(count)) {
            sorted.resize(count);
        }
        return sorted;
    }
};

class BanManager {
private:
    struct BanEntry {
        std::string peerId;
        std::string ip;
        time_t banTime;
        time_t banUntil;
        std::string reason;
        int banCount;
    };

    std::map<std::string, BanEntry> bannedPeers;
    std::map<std::string, BanEntry> bannedIPs;
    std::set<std::string> whitelist;
    mutable std::mutex mutex;
    static const int DEFAULT_BAN_DURATION = 86400;

public:
    void banPeer(const std::string& peerId, const std::string& ip, 
                 const std::string& reason, int duration = DEFAULT_BAN_DURATION) {
        std::lock_guard<std::mutex> lock(mutex);

        if (whitelist.find(peerId) != whitelist.end()) return;
        if (whitelist.find(ip) != whitelist.end()) return;

        BanEntry entry;
        entry.peerId = peerId;
        entry.ip = ip;
        entry.banTime = time(nullptr);
        entry.banUntil = entry.banTime + duration;
        entry.reason = reason;

        auto it = bannedPeers.find(peerId);
        if (it != bannedPeers.end()) {
            entry.banCount = it->second.banCount + 1;
            entry.banUntil = entry.banTime + duration * entry.banCount;
        } else {
            entry.banCount = 1;
        }

        bannedPeers[peerId] = entry;
        bannedIPs[ip] = entry;
    }

    void unbanPeer(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = bannedPeers.find(peerId);
        if (it != bannedPeers.end()) {
            bannedIPs.erase(it->second.ip);
            bannedPeers.erase(it);
        }
    }

    void unbanIP(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = bannedIPs.find(ip);
        if (it != bannedIPs.end()) {
            bannedPeers.erase(it->second.peerId);
            bannedIPs.erase(it);
        }
    }

    bool isBanned(const std::string& peerId) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = bannedPeers.find(peerId);
        if (it == bannedPeers.end()) return false;
        return time(nullptr) < it->second.banUntil;
    }

    bool isIPBanned(const std::string& ip) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = bannedIPs.find(ip);
        if (it == bannedIPs.end()) return false;
        return time(nullptr) < it->second.banUntil;
    }

    void addToWhitelist(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex);
        whitelist.insert(id);
    }

    void removeFromWhitelist(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex);
        whitelist.erase(id);
    }

    bool isWhitelisted(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex);
        return whitelist.find(id) != whitelist.end();
    }

    void cleanupExpired() {
        std::lock_guard<std::mutex> lock(mutex);
        time_t now = time(nullptr);

        for (auto it = bannedPeers.begin(); it != bannedPeers.end();) {
            if (now >= it->second.banUntil) {
                bannedIPs.erase(it->second.ip);
                it = bannedPeers.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<BanEntry> getBannedList() const {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<BanEntry> result;
        for (const auto& pair : bannedPeers) {
            result.push_back(pair.second);
        }
        return result;
    }

    int getBanCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return bannedPeers.size();
    }
};

class AddressManager {
private:
    std::vector<PeerAddress> knownAddresses;
    std::vector<PeerAddress> triedAddresses;
    std::vector<PeerAddress> newAddresses;
    std::set<std::string> seenAddresses;
    mutable std::mutex mutex;
    std::mt19937 rng;
    static const size_t MAX_ADDRESSES = 10000;
    static const size_t MAX_NEW = 1000;
    static const size_t MAX_TRIED = 5000;

public:
    AddressManager() : rng(std::random_device{}()) {}

    void addAddress(const PeerAddress& addr) {
        std::lock_guard<std::mutex> lock(mutex);

        std::string key = addr.ip + ":" + std::to_string(addr.port);
        if (seenAddresses.find(key) != seenAddresses.end()) {
            updateAddress(addr);
            return;
        }

        seenAddresses.insert(key);
        newAddresses.push_back(addr);

        if (newAddresses.size() > MAX_NEW) {
            newAddresses.erase(newAddresses.begin());
        }
    }

    void updateAddress(const PeerAddress& addr) {
        std::string key = addr.ip + ":" + std::to_string(addr.port);

        for (auto& a : newAddresses) {
            if (a.ip == addr.ip && a.port == addr.port) {
                a.lastSeen = addr.lastSeen;
                return;
            }
        }

        for (auto& a : triedAddresses) {
            if (a.ip == addr.ip && a.port == addr.port) {
                a.lastSeen = addr.lastSeen;
                return;
            }
        }
    }

    void markTried(const std::string& ip, uint16_t port, bool success) {
        std::lock_guard<std::mutex> lock(mutex);

        for (auto it = newAddresses.begin(); it != newAddresses.end(); ++it) {
            if (it->ip == ip && it->port == port) {
                it->lastTried = time(nullptr);
                it->attempts++;
                if (success) {
                    it->successfulConnections++;
                    triedAddresses.push_back(*it);
                    newAddresses.erase(it);

                    if (triedAddresses.size() > MAX_TRIED) {
                        triedAddresses.erase(triedAddresses.begin());
                    }
                }
                return;
            }
        }
    }

    PeerAddress getRandomAddress() {
        std::lock_guard<std::mutex> lock(mutex);

        if (newAddresses.empty() && triedAddresses.empty()) {
            return PeerAddress{};
        }

        std::vector<PeerAddress>* pool;
        if (newAddresses.empty()) {
            pool = &triedAddresses;
        } else if (triedAddresses.empty()) {
            pool = &newAddresses;
        } else {
            pool = (rng() % 2 == 0) ? &newAddresses : &triedAddresses;
        }

        std::uniform_int_distribution<size_t> dist(0, pool->size() - 1);
        return (*pool)[dist(rng)];
    }

    std::vector<PeerAddress> getAddresses(int count) {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<PeerAddress> result;

        std::vector<PeerAddress> combined;
        combined.insert(combined.end(), triedAddresses.begin(), triedAddresses.end());
        combined.insert(combined.end(), newAddresses.begin(), newAddresses.end());

        std::shuffle(combined.begin(), combined.end(), rng);

        for (int i = 0; i < count && i < static_cast<int>(combined.size()); i++) {
            result.push_back(combined[i]);
        }

        return result;
    }

    void removeAddress(const std::string& ip, uint16_t port) {
        std::lock_guard<std::mutex> lock(mutex);

        std::string key = ip + ":" + std::to_string(port);
        seenAddresses.erase(key);

        newAddresses.erase(
            std::remove_if(newAddresses.begin(), newAddresses.end(),
                [&](const PeerAddress& a) { return a.ip == ip && a.port == port; }),
            newAddresses.end());

        triedAddresses.erase(
            std::remove_if(triedAddresses.begin(), triedAddresses.end(),
                [&](const PeerAddress& a) { return a.ip == ip && a.port == port; }),
            triedAddresses.end());
    }

    size_t getNewCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return newAddresses.size();
    }

    size_t getTriedCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return triedAddresses.size();
    }

    size_t getTotalCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return seenAddresses.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        knownAddresses.clear();
        triedAddresses.clear();
        newAddresses.clear();
        seenAddresses.clear();
    }
};

class ConnectionManager {
private:
    std::map<std::string, PeerInfo> connectedPeers;
    std::queue<std::string> connectionQueue;
    std::set<std::string> pendingConnections;
    mutable std::mutex mutex;
    int maxOutbound;
    int maxInbound;
    int maxTotal;
    std::atomic<int> outboundCount;
    std::atomic<int> inboundCount;

public:
    ConnectionManager() : maxOutbound(8), maxInbound(117), maxTotal(125),
                          outboundCount(0), inboundCount(0) {}

    void setLimits(int outbound, int inbound, int total) {
        maxOutbound = outbound;
        maxInbound = inbound;
        maxTotal = total;
    }

    bool canAcceptInbound() const {
        return inboundCount < maxInbound && (inboundCount + outboundCount) < maxTotal;
    }

    bool canMakeOutbound() const {
        return outboundCount < maxOutbound && (inboundCount + outboundCount) < maxTotal;
    }

    bool addConnection(const PeerInfo& peer) {
        std::lock_guard<std::mutex> lock(mutex);

        if (connectedPeers.find(peer.id) != connectedPeers.end()) {
            return false;
        }

        if (peer.inbound) {
            if (!canAcceptInbound()) return false;
            inboundCount++;
        } else {
            if (!canMakeOutbound()) return false;
            outboundCount++;
        }

        connectedPeers[peer.id] = peer;
        pendingConnections.erase(peer.id);
        return true;
    }

    void removeConnection(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = connectedPeers.find(peerId);
        if (it != connectedPeers.end()) {
            if (it->second.inbound) {
                inboundCount--;
            } else {
                outboundCount--;
            }
            connectedPeers.erase(it);
        }
    }

    void queueConnection(const std::string& address) {
        std::lock_guard<std::mutex> lock(mutex);
        if (pendingConnections.find(address) == pendingConnections.end()) {
            connectionQueue.push(address);
            pendingConnections.insert(address);
        }
    }

    std::string getNextConnection() {
        std::lock_guard<std::mutex> lock(mutex);
        if (connectionQueue.empty()) return "";

        std::string addr = connectionQueue.front();
        connectionQueue.pop();
        return addr;
    }

    PeerInfo* getPeer(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = connectedPeers.find(peerId);
        return (it != connectedPeers.end()) ? &it->second : nullptr;
    }

    std::vector<PeerInfo> getAllPeers() const {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<PeerInfo> result;
        for (const auto& pair : connectedPeers) {
            result.push_back(pair.second);
        }
        return result;
    }

    std::vector<PeerInfo> getActivePeers() const {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<PeerInfo> result;
        for (const auto& pair : connectedPeers) {
            if (pair.second.state == PeerState::CONNECTED) {
                result.push_back(pair.second);
            }
        }
        return result;
    }

    int getConnectionCount() const {
        return inboundCount + outboundCount;
    }

    int getInboundCount() const {
        return inboundCount;
    }

    int getOutboundCount() const {
        return outboundCount;
    }

    void updatePeerState(const std::string& peerId, PeerState state) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = connectedPeers.find(peerId);
        if (it != connectedPeers.end()) {
            it->second.state = state;
        }
    }

    void updatePeerHeight(const std::string& peerId, uint64_t height) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = connectedPeers.find(peerId);
        if (it != connectedPeers.end()) {
            it->second.currentHeight = height;
        }
    }
};

class PeerManager {
private:
    AddressManager addressManager;
    ConnectionManager connectionManager;
    BanManager banManager;
    PeerScoring scoring;
    mutable std::mutex mutex;
    std::atomic<bool> running;
    std::thread maintenanceThread;
    std::function<void(const std::string&)> onPeerConnected;
    std::function<void(const std::string&)> onPeerDisconnected;
    std::function<void(const std::string&, const std::string&)> onPeerBanned;

public:
    PeerManager() : running(false) {}

    ~PeerManager() {
        stop();
    }

    void start() {
        running = true;
        maintenanceThread = std::thread(&PeerManager::maintenanceLoop, this);
    }

    void stop() {
        running = false;
        if (maintenanceThread.joinable()) {
            maintenanceThread.join();
        }
    }

    void maintenanceLoop() {
        while (running) {
            performMaintenance();
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }

    void performMaintenance() {
        banManager.cleanupExpired();
        scoring.decay();
        evictStalePeers();
        fillConnections();
    }

    void evictStalePeers() {
        auto peers = connectionManager.getAllPeers();
        time_t now = time(nullptr);

        for (const auto& peer : peers) {
            if (now - peer.stats.lastActivity > 120) {
                disconnectPeer(peer.id, "stale");
            }
        }
    }

    void fillConnections() {
        while (connectionManager.canMakeOutbound()) {
            PeerAddress addr = addressManager.getRandomAddress();
            if (addr.ip.empty()) break;

            if (!banManager.isIPBanned(addr.ip)) {
                connectionManager.queueConnection(addr.ip + ":" + std::to_string(addr.port));
            }
        }
    }

    bool connectToPeer(const std::string& address) {
        size_t colonPos = address.find(':');
        if (colonPos == std::string::npos) return false;

        std::string ip = address.substr(0, colonPos);
        uint16_t port = std::stoi(address.substr(colonPos + 1));

        if (banManager.isIPBanned(ip)) return false;
        if (!connectionManager.canMakeOutbound()) return false;

        PeerInfo peer;
        peer.id = generatePeerId();
        peer.address.ip = ip;
        peer.address.port = port;
        peer.state = PeerState::CONNECTING;
        peer.inbound = false;
        peer.stats.connectedSince = time(nullptr);
        peer.stats.lastActivity = time(nullptr);

        return connectionManager.addConnection(peer);
    }

    bool acceptConnection(const std::string& ip, uint16_t port) {
        if (banManager.isIPBanned(ip)) return false;
        if (!connectionManager.canAcceptInbound()) return false;

        PeerInfo peer;
        peer.id = generatePeerId();
        peer.address.ip = ip;
        peer.address.port = port;
        peer.state = PeerState::HANDSHAKING;
        peer.inbound = true;
        peer.stats.connectedSince = time(nullptr);
        peer.stats.lastActivity = time(nullptr);

        return connectionManager.addConnection(peer);
    }

    void disconnectPeer(const std::string& peerId, const std::string& reason) {
        connectionManager.removeConnection(peerId);
        if (onPeerDisconnected) {
            onPeerDisconnected(peerId);
        }
    }

    void banPeer(const std::string& peerId, const std::string& reason) {
        auto peer = connectionManager.getPeer(peerId);
        if (peer) {
            banManager.banPeer(peerId, peer->address.ip, reason);
            disconnectPeer(peerId, "banned: " + reason);
            if (onPeerBanned) {
                onPeerBanned(peerId, reason);
            }
        }
    }

    std::string generatePeerId() {
        unsigned char buf[16];
        if (RAND_bytes(buf, sizeof(buf)) != 1) {
            std::random_device rd;
            for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = static_cast<unsigned char>(rd() & 0xFF);
        }
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < sizeof(buf); ++i) {
            ss << std::setw(2) << static_cast<int>(buf[i]);
        }
        return ss.str();
    }

    void addKnownAddress(const PeerAddress& addr) {
        addressManager.addAddress(addr);
    }

    void addKnownAddresses(const std::vector<PeerAddress>& addrs) {
        for (const auto& addr : addrs) {
            addressManager.addAddress(addr);
        }
    }

    std::vector<PeerAddress> getAddressesForShare(int count) {
        return addressManager.getAddresses(count);
    }

    void recordGoodBehavior(const std::string& peerId) {
        scoring.goodBehavior(peerId);
    }

    void recordBadBehavior(const std::string& peerId, const std::string& type) {
        if (type == "invalid_block") {
            scoring.invalidBlock(peerId);
        } else if (type == "invalid_tx") {
            scoring.invalidTransaction(peerId);
        } else if (type == "timeout") {
            scoring.timeout(peerId);
        } else if (type == "protocol") {
            scoring.protocolViolation(peerId);
        } else if (type == "spam") {
            scoring.spamming(peerId);
        }

        if (scoring.shouldBan(peerId)) {
            banPeer(peerId, type);
        }
    }

    void setOnPeerConnected(std::function<void(const std::string&)> callback) {
        onPeerConnected = callback;
    }

    void setOnPeerDisconnected(std::function<void(const std::string&)> callback) {
        onPeerDisconnected = callback;
    }

    void setOnPeerBanned(std::function<void(const std::string&, const std::string&)> callback) {
        onPeerBanned = callback;
    }

    int getPeerCount() const {
        return connectionManager.getConnectionCount();
    }

    int getInboundCount() const {
        return connectionManager.getInboundCount();
    }

    int getOutboundCount() const {
        return connectionManager.getOutboundCount();
    }

    std::vector<PeerInfo> getConnectedPeers() const {
        return connectionManager.getAllPeers();
    }

    std::vector<PeerInfo> getActivePeers() const {
        return connectionManager.getActivePeers();
    }

    bool isPeerBanned(const std::string& peerId) const {
        return banManager.isBanned(peerId);
    }

    void whitelistPeer(const std::string& peerId) {
        banManager.addToWhitelist(peerId);
    }

    void unwhitelistPeer(const std::string& peerId) {
        banManager.removeFromWhitelist(peerId);
    }
};

class MessageRouter {
private:
    std::map<std::string, std::function<void(const std::string&, const std::vector<uint8_t>&)>> handlers;
    std::map<std::string, uint64_t> messageCount;
    std::map<std::string, std::chrono::steady_clock::time_point> lastMessage;
    mutable std::mutex mutex;

public:
    void registerHandler(const std::string& messageType,
                        std::function<void(const std::string&, const std::vector<uint8_t>&)> handler) {
        std::lock_guard<std::mutex> lock(mutex);
        handlers[messageType] = handler;
    }

    void unregisterHandler(const std::string& messageType) {
        std::lock_guard<std::mutex> lock(mutex);
        handlers.erase(messageType);
    }

    bool routeMessage(const std::string& peerId, const std::string& messageType,
                     const std::vector<uint8_t>& payload) {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = handlers.find(messageType);
        if (it == handlers.end()) return false;

        messageCount[messageType]++;
        lastMessage[peerId] = std::chrono::steady_clock::now();

        it->second(peerId, payload);
        return true;
    }

    uint64_t getMessageCount(const std::string& messageType) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = messageCount.find(messageType);
        return (it != messageCount.end()) ? it->second : 0;
    }

    void resetStats() {
        std::lock_guard<std::mutex> lock(mutex);
        messageCount.clear();
    }
};

class RateLimiter {
private:
    struct PeerLimit {
        int messageCount;
        std::chrono::steady_clock::time_point windowStart;
        int violations;
    };

    std::map<std::string, PeerLimit> peerLimits;
    int maxMessagesPerSecond;
    int windowSize;
    mutable std::mutex mutex;

public:
    RateLimiter() : maxMessagesPerSecond(100), windowSize(1) {}

    void setLimit(int messagesPerSecond) {
        maxMessagesPerSecond = messagesPerSecond;
    }

    bool checkLimit(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(mutex);

        auto now = std::chrono::steady_clock::now();
        auto& limit = peerLimits[peerId];

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - limit.windowStart).count();

        if (elapsed >= windowSize) {
            limit.messageCount = 0;
            limit.windowStart = now;
        }

        limit.messageCount++;

        if (limit.messageCount > maxMessagesPerSecond * windowSize) {
            limit.violations++;
            return false;
        }

        return true;
    }

    int getViolations(const std::string& peerId) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = peerLimits.find(peerId);
        return (it != peerLimits.end()) ? it->second.violations : 0;
    }

    void resetPeer(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(mutex);
        peerLimits.erase(peerId);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        peerLimits.clear();
    }
};

class PeerSelector {
private:
    std::mt19937 rng;

public:
    PeerSelector() : rng(std::random_device{}()) {}

    std::vector<std::string> selectRandom(const std::vector<PeerInfo>& peers, int count) {
        std::vector<std::string> result;
        std::vector<PeerInfo> shuffled = peers;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        for (int i = 0; i < count && i < static_cast<int>(shuffled.size()); i++) {
            result.push_back(shuffled[i].id);
        }
        return result;
    }

    std::vector<std::string> selectByScore(const std::vector<PeerInfo>& peers, int count) {
        std::vector<PeerInfo> sorted = peers;
        std::sort(sorted.begin(), sorted.end(),
            [](const PeerInfo& a, const PeerInfo& b) { return a.score > b.score; });

        std::vector<std::string> result;
        for (int i = 0; i < count && i < static_cast<int>(sorted.size()); i++) {
            result.push_back(sorted[i].id);
        }
        return result;
    }

    std::vector<std::string> selectByHeight(const std::vector<PeerInfo>& peers, int count) {
        std::vector<PeerInfo> sorted = peers;
        std::sort(sorted.begin(), sorted.end(),
            [](const PeerInfo& a, const PeerInfo& b) { return a.currentHeight > b.currentHeight; });

        std::vector<std::string> result;
        for (int i = 0; i < count && i < static_cast<int>(sorted.size()); i++) {
            result.push_back(sorted[i].id);
        }
        return result;
    }

    std::string selectBestForSync(const std::vector<PeerInfo>& peers) {
        if (peers.empty()) return "";

        auto best = std::max_element(peers.begin(), peers.end(),
            [](const PeerInfo& a, const PeerInfo& b) {
                if (a.currentHeight != b.currentHeight) {
                    return a.currentHeight < b.currentHeight;
                }
                return a.score < b.score;
            });

        return best->id;
    }
};

}
}
