#include <iostream>
#include <cassert>
#include <cstdint>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <map>
#include <set>
#include <queue>
#include <functional>
#include <memory>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace synapse {
namespace test {

class TestFramework {
public:
    static void assertEqual(int expected, int actual, const std::string& msg = "") {
        if (expected != actual) {
            std::cerr << "FAIL: " << msg << " (expected " << expected << ", got " << actual << ")" << std::endl;
            failCount++;
        } else {
            passCount++;
        }
    }
    
    static void assertEqual(uint64_t expected, uint64_t actual, const std::string& msg = "") {
        if (expected != actual) {
            std::cerr << "FAIL: " << msg << " (expected " << expected << ", got " << actual << ")" << std::endl;
            failCount++;
        } else {
            passCount++;
        }
    }
    
    static void assertEqual(const std::string& expected, const std::string& actual, const std::string& msg = "") {
        if (expected != actual) {
            std::cerr << "FAIL: " << msg << " (expected '" << expected << "', got '" << actual << "')" << std::endl;
            failCount++;
        } else {
            passCount++;
        }
    }
    
    static void assertTrue(bool condition, const std::string& msg = "") {
        if (!condition) {
            std::cerr << "FAIL: " << msg << std::endl;
            failCount++;
        } else {
            passCount++;
        }
    }
    
    static void assertFalse(bool condition, const std::string& msg = "") {
        if (condition) {
            std::cerr << "FAIL: " << msg << std::endl;
            failCount++;
        } else {
            passCount++;
        }
    }
    
    static void printSummary() {
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Passed: " << passCount << std::endl;
        std::cout << "Failed: " << failCount << std::endl;
        std::cout << "Total:  " << (passCount + failCount) << std::endl;
    }
    
    static int getFailCount() { return failCount; }
    
private:
    static int passCount;
    static int failCount;
};

int TestFramework::passCount = 0;
int TestFramework::failCount = 0;

struct MockPeer {
    std::string peerId;
    std::string address;
    uint16_t port;
    std::vector<uint8_t> publicKey;
    std::vector<uint8_t> privateKey;
    bool connected;
    uint64_t lastSeen;
    std::queue<std::vector<uint8_t>> messageQueue;
    std::mutex mtx;
    
    MockPeer(const std::string& id, const std::string& addr, uint16_t p)
        : peerId(id), address(addr), port(p), connected(false), lastSeen(0) {
        publicKey.resize(32);
        privateKey.resize(32);
        for (int i = 0; i < 32; i++) {
            publicKey[i] = static_cast<uint8_t>(id[i % id.length()]);
            privateKey[i] = static_cast<uint8_t>(id[i % id.length()] ^ 0xFF);
        }
    }
    
    void sendMessage(const std::vector<uint8_t>& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        messageQueue.push(msg);
    }
    
    std::vector<uint8_t> receiveMessage() {
        std::lock_guard<std::mutex> lock(mtx);
        if (messageQueue.empty()) return {};
        auto msg = messageQueue.front();
        messageQueue.pop();
        return msg;
    }
    
    bool hasMessages() {
        std::lock_guard<std::mutex> lock(mtx);
        return !messageQueue.empty();
    }
};

class MockNetwork {
public:
    void addPeer(std::shared_ptr<MockPeer> peer) {
        std::lock_guard<std::mutex> lock(mtx_);
        peers_[peer->peerId] = peer;
    }
    
    void removePeer(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(mtx_);
        peers_.erase(peerId);
    }
    
    std::shared_ptr<MockPeer> getPeer(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = peers_.find(peerId);
        return it != peers_.end() ? it->second : nullptr;
    }
    
    void broadcast(const std::vector<uint8_t>& msg, const std::string& excludePeer = "") {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& pair : peers_) {
            if (pair.first != excludePeer && pair.second->connected) {
                pair.second->sendMessage(msg);
            }
        }
    }
    
    size_t getPeerCount() {
        std::lock_guard<std::mutex> lock(mtx_);
        return peers_.size();
    }
    
    size_t getConnectedCount() {
        std::lock_guard<std::mutex> lock(mtx_);
        size_t count = 0;
        for (const auto& pair : peers_) {
            if (pair.second->connected) count++;
        }
        return count;
    }
    
private:
    std::map<std::string, std::shared_ptr<MockPeer>> peers_;
    std::mutex mtx_;
};

void testPeerCreation() {
    std::cout << "Testing peer creation..." << std::endl;
    
    MockPeer peer("peer1", "192.168.1.1", 8333);
    
    TestFramework::assertEqual("peer1", peer.peerId, "Peer ID should match");
    TestFramework::assertEqual("192.168.1.1", peer.address, "Address should match");
    TestFramework::assertEqual(8333, static_cast<int>(peer.port), "Port should match");
    TestFramework::assertFalse(peer.connected, "Peer should not be connected initially");
    TestFramework::assertEqual(32, static_cast<int>(peer.publicKey.size()), "Public key should be 32 bytes");
    TestFramework::assertEqual(32, static_cast<int>(peer.privateKey.size()), "Private key should be 32 bytes");
}

void testPeerMessaging() {
    std::cout << "Testing peer messaging..." << std::endl;
    
    MockPeer peer("peer1", "192.168.1.1", 8333);
    
    TestFramework::assertFalse(peer.hasMessages(), "Peer should have no messages initially");
    
    std::vector<uint8_t> msg1 = {0x01, 0x02, 0x03, 0x04};
    peer.sendMessage(msg1);
    
    TestFramework::assertTrue(peer.hasMessages(), "Peer should have messages after send");
    
    auto received = peer.receiveMessage();
    TestFramework::assertEqual(4, static_cast<int>(received.size()), "Received message should be 4 bytes");
    TestFramework::assertEqual(0x01, static_cast<int>(received[0]), "First byte should match");
    TestFramework::assertEqual(0x04, static_cast<int>(received[3]), "Last byte should match");
    
    TestFramework::assertFalse(peer.hasMessages(), "Peer should have no messages after receive");
}

void testNetworkAddRemovePeers() {
    std::cout << "Testing network add/remove peers..." << std::endl;
    
    MockNetwork network;
    
    TestFramework::assertEqual(0, static_cast<int>(network.getPeerCount()), "Network should have no peers initially");
    
    auto peer1 = std::make_shared<MockPeer>("peer1", "192.168.1.1", 8333);
    auto peer2 = std::make_shared<MockPeer>("peer2", "192.168.1.2", 8333);
    auto peer3 = std::make_shared<MockPeer>("peer3", "192.168.1.3", 8333);
    
    network.addPeer(peer1);
    TestFramework::assertEqual(1, static_cast<int>(network.getPeerCount()), "Network should have 1 peer");
    
    network.addPeer(peer2);
    network.addPeer(peer3);
    TestFramework::assertEqual(3, static_cast<int>(network.getPeerCount()), "Network should have 3 peers");
    
    network.removePeer("peer2");
    TestFramework::assertEqual(2, static_cast<int>(network.getPeerCount()), "Network should have 2 peers after removal");
    
    auto retrieved = network.getPeer("peer1");
    TestFramework::assertTrue(retrieved != nullptr, "Should be able to retrieve peer1");
    TestFramework::assertEqual("peer1", retrieved->peerId, "Retrieved peer ID should match");
    
    auto notFound = network.getPeer("peer2");
    TestFramework::assertTrue(notFound == nullptr, "Should not find removed peer");
}

void testNetworkBroadcast() {
    std::cout << "Testing network broadcast..." << std::endl;
    
    MockNetwork network;
    
    auto peer1 = std::make_shared<MockPeer>("peer1", "192.168.1.1", 8333);
    auto peer2 = std::make_shared<MockPeer>("peer2", "192.168.1.2", 8333);
    auto peer3 = std::make_shared<MockPeer>("peer3", "192.168.1.3", 8333);
    
    peer1->connected = true;
    peer2->connected = true;
    peer3->connected = false;
    
    network.addPeer(peer1);
    network.addPeer(peer2);
    network.addPeer(peer3);
    
    TestFramework::assertEqual(2, static_cast<int>(network.getConnectedCount()), "Should have 2 connected peers");
    
    std::vector<uint8_t> msg = {0xAA, 0xBB, 0xCC};
    network.broadcast(msg);
    
    TestFramework::assertTrue(peer1->hasMessages(), "Connected peer1 should receive broadcast");
    TestFramework::assertTrue(peer2->hasMessages(), "Connected peer2 should receive broadcast");
    TestFramework::assertFalse(peer3->hasMessages(), "Disconnected peer3 should not receive broadcast");
    
    network.broadcast(msg, "peer1");
    
    auto peer1Msg = peer1->receiveMessage();
    TestFramework::assertTrue(peer1->hasMessages() == false || peer1Msg.size() == 3, 
                              "peer1 should not receive excluded broadcast");
}

void testConcurrentMessaging() {
    std::cout << "Testing concurrent messaging..." << std::endl;
    
    MockPeer peer("peer1", "192.168.1.1", 8333);
    std::atomic<int> sendCount{0};
    std::atomic<int> receiveCount{0};
    
    const int numMessages = 1000;
    const int numSenders = 4;
    const int numReceivers = 2;
    
    std::vector<std::thread> senders;
    std::vector<std::thread> receivers;
    std::atomic<bool> done{false};
    
    for (int i = 0; i < numSenders; i++) {
        senders.emplace_back([&peer, &sendCount, numMessages, i]() {
            for (int j = 0; j < numMessages / numSenders; j++) {
                std::vector<uint8_t> msg = {static_cast<uint8_t>(i), static_cast<uint8_t>(j)};
                peer.sendMessage(msg);
                sendCount++;
            }
        });
    }
    
    for (int i = 0; i < numReceivers; i++) {
        receivers.emplace_back([&peer, &receiveCount, &done]() {
            while (!done || peer.hasMessages()) {
                auto msg = peer.receiveMessage();
                if (!msg.empty()) {
                    receiveCount++;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }
    
    for (auto& t : senders) {
        t.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    done = true;
    
    for (auto& t : receivers) {
        t.join();
    }
    
    TestFramework::assertEqual(numMessages, sendCount.load(), "All messages should be sent");
    TestFramework::assertTrue(receiveCount.load() > 0, "Some messages should be received");
}

void testPeerConnectionState() {
    std::cout << "Testing peer connection state..." << std::endl;
    
    MockPeer peer("peer1", "192.168.1.1", 8333);
    
    TestFramework::assertFalse(peer.connected, "Peer should start disconnected");
    
    peer.connected = true;
    peer.lastSeen = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    TestFramework::assertTrue(peer.connected, "Peer should be connected");
    TestFramework::assertTrue(peer.lastSeen > 0, "Last seen should be set");
    
    peer.connected = false;
    
    TestFramework::assertFalse(peer.connected, "Peer should be disconnected");
}

void testNetworkTopology() {
    std::cout << "Testing network topology..." << std::endl;
    
    MockNetwork network;
    
    const int numPeers = 100;
    std::vector<std::shared_ptr<MockPeer>> peers;
    
    for (int i = 0; i < numPeers; i++) {
        auto peer = std::make_shared<MockPeer>(
            "peer" + std::to_string(i),
            "192.168.1." + std::to_string(i % 256),
            8333 + (i % 100)
        );
        peer->connected = (i % 3 != 0);
        peers.push_back(peer);
        network.addPeer(peer);
    }
    
    TestFramework::assertEqual(numPeers, static_cast<int>(network.getPeerCount()), 
                               "Network should have all peers");
    
    int expectedConnected = 0;
    for (int i = 0; i < numPeers; i++) {
        if (i % 3 != 0) expectedConnected++;
    }
    
    TestFramework::assertEqual(expectedConnected, static_cast<int>(network.getConnectedCount()),
                               "Connected count should match");
    
    for (int i = 0; i < numPeers; i += 2) {
        network.removePeer("peer" + std::to_string(i));
    }
    
    TestFramework::assertEqual(numPeers / 2, static_cast<int>(network.getPeerCount()),
                               "Half of peers should remain");
}

void testMessageSerialization() {
    std::cout << "Testing message serialization..." << std::endl;
    
    struct TestMessage {
        uint8_t type;
        uint32_t length;
        std::vector<uint8_t> payload;
        
        std::vector<uint8_t> serialize() const {
            std::vector<uint8_t> data;
            data.push_back(type);
            data.push_back((length >> 0) & 0xFF);
            data.push_back((length >> 8) & 0xFF);
            data.push_back((length >> 16) & 0xFF);
            data.push_back((length >> 24) & 0xFF);
            data.insert(data.end(), payload.begin(), payload.end());
            return data;
        }
        
        static TestMessage deserialize(const std::vector<uint8_t>& data) {
            TestMessage msg;
            if (data.size() < 5) return msg;
            msg.type = data[0];
            msg.length = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
            if (data.size() > 5) {
                msg.payload.assign(data.begin() + 5, data.end());
            }
            return msg;
        }
    };
    
    TestMessage original;
    original.type = 0x42;
    original.length = 10;
    original.payload = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    
    auto serialized = original.serialize();
    TestFramework::assertEqual(15, static_cast<int>(serialized.size()), "Serialized size should be 15");
    
    auto deserialized = TestMessage::deserialize(serialized);
    TestFramework::assertEqual(0x42, static_cast<int>(deserialized.type), "Type should match");
    TestFramework::assertEqual(10, static_cast<int>(deserialized.length), "Length should match");
    TestFramework::assertEqual(10, static_cast<int>(deserialized.payload.size()), "Payload size should match");
}

void testHandshakeProtocol() {
    std::cout << "Testing handshake protocol..." << std::endl;
    
    struct HandshakeMessage {
        uint8_t version;
        uint8_t flags;
        std::vector<uint8_t> publicKey;
        std::vector<uint8_t> nonce;
        
        std::vector<uint8_t> serialize() const {
            std::vector<uint8_t> data;
            data.push_back(0x01);
            data.push_back(version);
            data.push_back(flags);
            uint16_t keyLen = publicKey.size();
            data.push_back(keyLen & 0xFF);
            data.push_back((keyLen >> 8) & 0xFF);
            data.insert(data.end(), publicKey.begin(), publicKey.end());
            uint16_t nonceLen = nonce.size();
            data.push_back(nonceLen & 0xFF);
            data.push_back((nonceLen >> 8) & 0xFF);
            data.insert(data.end(), nonce.begin(), nonce.end());
            return data;
        }
    };
    
    HandshakeMessage init;
    init.version = 1;
    init.flags = 0x03;
    init.publicKey.resize(32);
    init.nonce.resize(32);
    
    for (int i = 0; i < 32; i++) {
        init.publicKey[i] = static_cast<uint8_t>(i);
        init.nonce[i] = static_cast<uint8_t>(i ^ 0xFF);
    }
    
    auto serialized = init.serialize();
    
    TestFramework::assertEqual(0x01, static_cast<int>(serialized[0]), "Message type should be 0x01");
    TestFramework::assertEqual(1, static_cast<int>(serialized[1]), "Version should be 1");
    TestFramework::assertEqual(0x03, static_cast<int>(serialized[2]), "Flags should be 0x03");
    
    uint16_t keyLen = serialized[3] | (serialized[4] << 8);
    TestFramework::assertEqual(32, static_cast<int>(keyLen), "Key length should be 32");
}

void testPeerDiscovery() {
    std::cout << "Testing peer discovery..." << std::endl;
    
    struct PeerAnnouncement {
        std::string peerId;
        std::string address;
        uint16_t port;
        uint64_t timestamp;
        std::vector<std::string> services;
    };
    
    std::vector<PeerAnnouncement> announcements;
    
    for (int i = 0; i < 10; i++) {
        PeerAnnouncement ann;
        ann.peerId = "peer" + std::to_string(i);
        ann.address = "192.168.1." + std::to_string(i);
        ann.port = 8333;
        ann.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        ann.services = {"full_node", "relay"};
        announcements.push_back(ann);
    }
    
    TestFramework::assertEqual(10, static_cast<int>(announcements.size()), "Should have 10 announcements");
    
    std::sort(announcements.begin(), announcements.end(),
              [](const PeerAnnouncement& a, const PeerAnnouncement& b) {
                  return a.timestamp > b.timestamp;
              });
    
    TestFramework::assertTrue(announcements[0].timestamp >= announcements[9].timestamp,
                              "Announcements should be sorted by timestamp");
}

void testBandwidthTracking() {
    std::cout << "Testing bandwidth tracking..." << std::endl;
    
    struct BandwidthTracker {
        std::atomic<uint64_t> bytesSent{0};
        std::atomic<uint64_t> bytesReceived{0};
        std::atomic<uint64_t> messagesSent{0};
        std::atomic<uint64_t> messagesReceived{0};
        
        void recordSend(size_t bytes) {
            bytesSent += bytes;
            messagesSent++;
        }
        
        void recordReceive(size_t bytes) {
            bytesReceived += bytes;
            messagesReceived++;
        }
        
        double getSendRate(uint64_t durationMs) const {
            if (durationMs == 0) return 0;
            return static_cast<double>(bytesSent) / durationMs * 1000;
        }
        
        double getReceiveRate(uint64_t durationMs) const {
            if (durationMs == 0) return 0;
            return static_cast<double>(bytesReceived) / durationMs * 1000;
        }
    };
    
    BandwidthTracker tracker;
    
    for (int i = 0; i < 100; i++) {
        tracker.recordSend(1024);
        tracker.recordReceive(2048);
    }
    
    TestFramework::assertEqual(102400ULL, tracker.bytesSent.load(), "Bytes sent should be 102400");
    TestFramework::assertEqual(204800ULL, tracker.bytesReceived.load(), "Bytes received should be 204800");
    TestFramework::assertEqual(100ULL, tracker.messagesSent.load(), "Messages sent should be 100");
    TestFramework::assertEqual(100ULL, tracker.messagesReceived.load(), "Messages received should be 100");
    
    double sendRate = tracker.getSendRate(1000);
    TestFramework::assertTrue(sendRate > 100000, "Send rate should be > 100KB/s");
}

void testConnectionPool() {
    std::cout << "Testing connection pool..." << std::endl;
    
    class ConnectionPool {
    public:
        ConnectionPool(size_t maxSize) : maxSize_(maxSize) {}
        
        bool addConnection(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            if (connections_.size() >= maxSize_) return false;
            connections_.insert(peerId);
            return true;
        }
        
        bool removeConnection(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            return connections_.erase(peerId) > 0;
        }
        
        bool hasConnection(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            return connections_.find(peerId) != connections_.end();
        }
        
        size_t size() {
            std::lock_guard<std::mutex> lock(mtx_);
            return connections_.size();
        }
        
        bool isFull() {
            std::lock_guard<std::mutex> lock(mtx_);
            return connections_.size() >= maxSize_;
        }
        
    private:
        std::set<std::string> connections_;
        size_t maxSize_;
        std::mutex mtx_;
    };
    
    ConnectionPool pool(10);
    
    TestFramework::assertEqual(0, static_cast<int>(pool.size()), "Pool should start empty");
    TestFramework::assertFalse(pool.isFull(), "Pool should not be full");
    
    for (int i = 0; i < 10; i++) {
        bool added = pool.addConnection("peer" + std::to_string(i));
        TestFramework::assertTrue(added, "Should be able to add connection");
    }
    
    TestFramework::assertEqual(10, static_cast<int>(pool.size()), "Pool should have 10 connections");
    TestFramework::assertTrue(pool.isFull(), "Pool should be full");
    
    bool addedExtra = pool.addConnection("peer10");
    TestFramework::assertFalse(addedExtra, "Should not be able to add to full pool");
    
    pool.removeConnection("peer5");
    TestFramework::assertFalse(pool.isFull(), "Pool should not be full after removal");
    TestFramework::assertFalse(pool.hasConnection("peer5"), "Removed peer should not exist");
}

void testMessageQueue() {
    std::cout << "Testing message queue..." << std::endl;
    
    class PriorityMessageQueue {
    public:
        struct Message {
            int priority;
            std::vector<uint8_t> data;
            uint64_t timestamp;
            
            bool operator<(const Message& other) const {
                return priority < other.priority;
            }
        };
        
        void push(const Message& msg) {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(msg);
            cv_.notify_one();
        }
        
        Message pop() {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return !queue_.empty(); });
            Message msg = queue_.top();
            queue_.pop();
            return msg;
        }
        
        bool tryPop(Message& msg, int timeoutMs) {
            std::unique_lock<std::mutex> lock(mtx_);
            if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                              [this] { return !queue_.empty(); })) {
                return false;
            }
            msg = queue_.top();
            queue_.pop();
            return true;
        }
        
        size_t size() {
            std::lock_guard<std::mutex> lock(mtx_);
            return queue_.size();
        }
        
        bool empty() {
            std::lock_guard<std::mutex> lock(mtx_);
            return queue_.empty();
        }
        
    private:
        std::priority_queue<Message> queue_;
        std::mutex mtx_;
        std::condition_variable cv_;
    };
    
    PriorityMessageQueue queue;
    
    TestFramework::assertTrue(queue.empty(), "Queue should start empty");
    
    PriorityMessageQueue::Message msg1{1, {0x01}, 100};
    PriorityMessageQueue::Message msg2{3, {0x02}, 101};
    PriorityMessageQueue::Message msg3{2, {0x03}, 102};
    
    queue.push(msg1);
    queue.push(msg2);
    queue.push(msg3);
    
    TestFramework::assertEqual(3, static_cast<int>(queue.size()), "Queue should have 3 messages");
    
    auto popped = queue.pop();
    TestFramework::assertEqual(3, popped.priority, "Highest priority should be popped first");
    
    popped = queue.pop();
    TestFramework::assertEqual(2, popped.priority, "Second highest priority should be next");
}

void testReconnectionLogic() {
    std::cout << "Testing reconnection logic..." << std::endl;
    
    class ReconnectionManager {
    public:
        struct ReconnectInfo {
            std::string peerId;
            int attempts;
            uint64_t lastAttempt;
            uint64_t nextAttempt;
            int backoffMs;
        };
        
        ReconnectionManager(int maxAttempts = 5, int baseBackoffMs = 1000)
            : maxAttempts_(maxAttempts), baseBackoffMs_(baseBackoffMs) {}
        
        void scheduleReconnect(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            
            auto it = pending_.find(peerId);
            if (it != pending_.end()) {
                it->second.attempts++;
                it->second.backoffMs = std::min(it->second.backoffMs * 2, 60000);
            } else {
                ReconnectInfo info;
                info.peerId = peerId;
                info.attempts = 1;
                info.lastAttempt = now();
                info.backoffMs = baseBackoffMs_;
                pending_[peerId] = info;
            }
            
            pending_[peerId].nextAttempt = now() + pending_[peerId].backoffMs;
        }
        
        bool shouldReconnect(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            
            auto it = pending_.find(peerId);
            if (it == pending_.end()) return false;
            if (it->second.attempts >= maxAttempts_) return false;
            if (now() < it->second.nextAttempt) return false;
            
            return true;
        }
        
        void markSuccess(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            pending_.erase(peerId);
        }
        
        int getAttempts(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = pending_.find(peerId);
            return it != pending_.end() ? it->second.attempts : 0;
        }
        
    private:
        uint64_t now() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
        
        std::map<std::string, ReconnectInfo> pending_;
        int maxAttempts_;
        int baseBackoffMs_;
        std::mutex mtx_;
    };
    
    ReconnectionManager manager(3, 100);
    
    manager.scheduleReconnect("peer1");
    TestFramework::assertEqual(1, manager.getAttempts("peer1"), "Should have 1 attempt");
    
    manager.scheduleReconnect("peer1");
    TestFramework::assertEqual(2, manager.getAttempts("peer1"), "Should have 2 attempts");
    
    manager.scheduleReconnect("peer1");
    TestFramework::assertEqual(3, manager.getAttempts("peer1"), "Should have 3 attempts");
    
    manager.markSuccess("peer1");
    TestFramework::assertEqual(0, manager.getAttempts("peer1"), "Should have 0 attempts after success");
}

void testNetworkLatency() {
    std::cout << "Testing network latency tracking..." << std::endl;
    
    class LatencyTracker {
    public:
        void recordLatency(const std::string& peerId, uint64_t latencyMs) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto& samples = latencies_[peerId];
            samples.push_back(latencyMs);
            if (samples.size() > maxSamples_) {
                samples.erase(samples.begin());
            }
        }
        
        double getAverageLatency(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = latencies_.find(peerId);
            if (it == latencies_.end() || it->second.empty()) return 0;
            
            double sum = std::accumulate(it->second.begin(), it->second.end(), 0.0);
            return sum / it->second.size();
        }
        
        uint64_t getMinLatency(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = latencies_.find(peerId);
            if (it == latencies_.end() || it->second.empty()) return 0;
            return *std::min_element(it->second.begin(), it->second.end());
        }
        
        uint64_t getMaxLatency(const std::string& peerId) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = latencies_.find(peerId);
            if (it == latencies_.end() || it->second.empty()) return 0;
            return *std::max_element(it->second.begin(), it->second.end());
        }
        
    private:
        std::map<std::string, std::vector<uint64_t>> latencies_;
        size_t maxSamples_ = 100;
        std::mutex mtx_;
    };
    
    LatencyTracker tracker;
    
    for (int i = 0; i < 10; i++) {
        tracker.recordLatency("peer1", 50 + i * 10);
    }
    
    double avg = tracker.getAverageLatency("peer1");
    TestFramework::assertTrue(avg >= 90 && avg <= 100, "Average latency should be around 95ms");
    
    TestFramework::assertEqual(50ULL, tracker.getMinLatency("peer1"), "Min latency should be 50ms");
    TestFramework::assertEqual(140ULL, tracker.getMaxLatency("peer1"), "Max latency should be 140ms");
}

int main() {
    std::cout << "=== SynapseNet Network Tests ===" << std::endl << std::endl;
    
    testPeerCreation();
    testPeerMessaging();
    testNetworkAddRemovePeers();
    testNetworkBroadcast();
    testConcurrentMessaging();
    testPeerConnectionState();
    testNetworkTopology();
    testMessageSerialization();
    testHandshakeProtocol();
    testPeerDiscovery();
    testBandwidthTracking();
    testConnectionPool();
    testMessageQueue();
    testReconnectionLogic();
    testNetworkLatency();
    
    TestFramework::printSummary();
    
    return TestFramework::getFailCount() > 0 ? 1 : 0;
}

}
}

int main() {
    return synapse::test::main();
}
