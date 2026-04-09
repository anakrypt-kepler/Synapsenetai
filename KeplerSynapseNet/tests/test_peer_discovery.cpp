#include <iostream>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <arpa/inet.h>
#include "network/discovery.h"
#include "infrastructure/messages.h"

namespace synapse {
namespace test {

class TestFramework {
public:
    template <typename T, typename U,
              typename = std::enable_if_t<
                  std::is_integral_v<T> && std::is_integral_v<U> &&
                  !std::is_same_v<T, bool> && !std::is_same_v<U, bool>>>
    static void assertEqual(T expected, U actual, const std::string& msg = "") {
        using CT = std::common_type_t<T, U>;
        CT e = static_cast<CT>(expected);
        CT a = static_cast<CT>(actual);
        if (e != a) {
            std::cerr << "FAIL: " << msg << " (expected " << e << ", got " << a << ")" << std::endl;
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
        std::cout << "\n=== Peer Discovery Test Summary ===" << std::endl;
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

class MockNetworkForDiscovery {
public:
    struct SentMessage {
        std::string peerId;
        std::string command;
        std::vector<uint8_t> payload;
        uint64_t timestamp;
    };
    
    std::vector<std::string> connectedPeers;
    std::vector<SentMessage> sentMessages;
    std::mutex mtx;
    
    bool sendMessage(const std::string& peerId, const std::string& command, const std::vector<uint8_t>& payload) {
        std::lock_guard<std::mutex> lock(mtx);
        SentMessage msg;
        msg.peerId = peerId;
        msg.command = command;
        msg.payload = payload;
        msg.timestamp = std::time(nullptr);
        sentMessages.push_back(msg);
        return true;
    }
    
    std::vector<std::string> getConnectedPeers() {
        std::lock_guard<std::mutex> lock(mtx);
        return connectedPeers;
    }
    
    void addConnectedPeer(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(mtx);
        if (std::find(connectedPeers.begin(), connectedPeers.end(), peerId) == connectedPeers.end()) {
            connectedPeers.push_back(peerId);
        }
    }
    
    void removeConnectedPeer(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(mtx);
        connectedPeers.erase(
            std::remove(connectedPeers.begin(), connectedPeers.end(), peerId),
            connectedPeers.end()
        );
    }
    
    size_t getMessageCount(const std::string& command) {
        std::lock_guard<std::mutex> lock(mtx);
        size_t count = 0;
        for (const auto& msg : sentMessages) {
            if (msg.command == command) count++;
        }
        return count;
    }
    
    void clearMessages() {
        std::lock_guard<std::mutex> lock(mtx);
        sentMessages.clear();
    }
};

void testPeerExchangeSendReceive() {
    std::cout << "Testing peer exchange send/receive..." << std::endl;
    
    network::Discovery discovery;
    MockNetworkForDiscovery mockNetwork;
    
    // Setup callbacks
    discovery.setSendMessageCallback([&mockNetwork](const std::string& peerId, const std::string& command, const std::vector<uint8_t>& payload) -> bool {
        return mockNetwork.sendMessage(peerId, command, payload);
    });
    
    discovery.setGetConnectedPeersCallback([&mockNetwork]() -> std::vector<std::string> {
        return mockNetwork.getConnectedPeers();
    });
    
    // Add some connected peers
    mockNetwork.addConnectedPeer("peer1");
    mockNetwork.addConnectedPeer("peer2");
    mockNetwork.addConnectedPeer("peer3");
    
    // Start discovery
    discovery.start(8333);
    
    // Trigger peer exchange
    discovery.refreshFromPeers();
    
    // Wait a bit for messages to be sent
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check that GETPEERS messages were sent
    size_t getpeersCount = mockNetwork.getMessageCount("getpeers");
    TestFramework::assertTrue(getpeersCount >= 3, "Should send GETPEERS to all connected peers");
    
    discovery.stop();
}

void testPeerExchangeRateLimiting() {
    std::cout << "Testing peer exchange rate limiting..." << std::endl;
    
    network::Discovery discovery;
    MockNetworkForDiscovery mockNetwork;
    
    discovery.setSendMessageCallback([&mockNetwork](const std::string& peerId, const std::string& command, const std::vector<uint8_t>& payload) -> bool {
        return mockNetwork.sendMessage(peerId, command, payload);
    });
    
    discovery.setGetConnectedPeersCallback([&mockNetwork]() -> std::vector<std::string> {
        return mockNetwork.getConnectedPeers();
    });
    
    mockNetwork.addConnectedPeer("peer1");
    discovery.start(8333);
    
    // Send first request
    discovery.refreshFromPeers();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    size_t firstCount = mockNetwork.getMessageCount("getpeers");
    TestFramework::assertEqual(static_cast<size_t>(1), firstCount, "First request should be sent");
    
    // Try to send immediately again (should be rate limited)
    mockNetwork.clearMessages();
    discovery.refreshFromPeers();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    size_t secondCount = mockNetwork.getMessageCount("getpeers");
    TestFramework::assertEqual(static_cast<size_t>(0), secondCount, "Second request should be rate limited");
    
    discovery.stop();
}

void testPeerAddressValidation() {
    std::cout << "Testing peer address validation..." << std::endl;
    
    network::Discovery discovery;
    discovery.start(8333);
    
    std::vector<network::PeerInfo> testPeers;
    
    // Valid peer
    network::PeerInfo validPeer;
    validPeer.address = "192.168.1.1";
    validPeer.port = 8333;
    validPeer.timestamp = std::time(nullptr);
    validPeer.lastSeen = validPeer.timestamp;
    testPeers.push_back(validPeer);
    
    // Localhost peer (should be rejected)
    network::PeerInfo localhostPeer;
    localhostPeer.address = "127.0.0.1";
    localhostPeer.port = 8333;
    localhostPeer.timestamp = std::time(nullptr);
    testPeers.push_back(localhostPeer);
    
    // Empty address (should be rejected)
    network::PeerInfo emptyPeer;
    emptyPeer.address = "";
    emptyPeer.port = 8333;
    testPeers.push_back(emptyPeer);
    
    // Zero port (should be rejected)
    network::PeerInfo zeroPortPeer;
    zeroPortPeer.address = "192.168.1.2";
    zeroPortPeer.port = 0;
    testPeers.push_back(zeroPortPeer);
    
    // Process peers
    size_t beforeCount = discovery.knownPeerCount();
    discovery.processIncoming(testPeers);
    size_t afterCount = discovery.knownPeerCount();
    
    // Only valid peer should be added
    TestFramework::assertTrue(afterCount > beforeCount, "At least one valid peer should be added");
    TestFramework::assertTrue(discovery.hasPeer("192.168.1.1"), "Valid peer should be added");
    TestFramework::assertFalse(discovery.hasPeer("127.0.0.1"), "Localhost peer should be rejected");
    
    discovery.stop();
}

void testBannedPeerRejection() {
    std::cout << "Testing banned peer rejection..." << std::endl;
    
    network::Discovery discovery;
    discovery.start(8333);
    
    // Ban a peer
    discovery.banPeer("192.168.1.100");
    TestFramework::assertTrue(discovery.isBanned("192.168.1.100"), "Peer should be banned");
    
    // Try to add banned peer
    network::PeerInfo bannedPeer;
    bannedPeer.address = "192.168.1.100";
    bannedPeer.port = 8333;
    bannedPeer.timestamp = std::time(nullptr);
    
    size_t beforeCount = discovery.knownPeerCount();
    discovery.processIncoming({bannedPeer});
    size_t afterCount = discovery.knownPeerCount();
    
    TestFramework::assertEqual(beforeCount, afterCount, "Banned peer should not be added");
    
    discovery.stop();
}

void testPeersMessageSerialization() {
    std::cout << "Testing PEERS message serialization..." << std::endl;
    
    synapse::PeersMessage original;
    
    // Create test peer addresses
    for (int i = 0; i < 5; i++) {
        synapse::PeerAddress addr;
        addr.services = 1;
        addr.port = 8333 + i;
        addr.timestamp = std::time(nullptr);
        addr.addr.fill(0);
        
        // Set IPv4 address
        std::string ipStr = "192.168.1." + std::to_string(i + 1);
        in_addr ipv4;
        if (inet_pton(AF_INET, ipStr.c_str(), &ipv4) == 1) {
            addr.addr[10] = 0xff;
            addr.addr[11] = 0xff;
            std::memcpy(addr.addr.data() + 12, &ipv4, 4);
            original.peers.push_back(addr);
        }
    }
    
    // Serialize
    auto serialized = original.serialize();
    TestFramework::assertTrue(serialized.size() > 0, "Serialized message should not be empty");
    
    // Deserialize
    auto deserialized = synapse::PeersMessage::deserialize(serialized);
    TestFramework::assertEqual(original.peers.size(), deserialized.peers.size(), "Deserialized should have same number of peers");
    
    if (deserialized.peers.size() > 0) {
        TestFramework::assertEqual(original.peers[0].port, deserialized.peers[0].port, "First peer port should match");
    }
}

void testAnnounceMechanism() {
    std::cout << "Testing announce mechanism..." << std::endl;
    
    network::Discovery discovery;
    MockNetworkForDiscovery mockNetwork;
    
    discovery.setSendMessageCallback([&mockNetwork](const std::string& peerId, const std::string& command, const std::vector<uint8_t>& payload) -> bool {
        return mockNetwork.sendMessage(peerId, command, payload);
    });
    
    discovery.setGetConnectedPeersCallback([&mockNetwork]() -> std::vector<std::string> {
        return mockNetwork.getConnectedPeers();
    });
    
    // Set external address
    discovery.setExternalAddress("203.0.113.1");
    
    mockNetwork.addConnectedPeer("peer1");
    mockNetwork.addConnectedPeer("peer2");
    
    discovery.start(8333);
    
    // Trigger announce
    discovery.announce();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check that PEERS messages were sent
    size_t peersCount = mockNetwork.getMessageCount("peers");
    TestFramework::assertTrue(peersCount >= 2, "Should send PEERS to all connected peers");
    
    discovery.stop();
}

void testNetworkSizeEstimation() {
    std::cout << "Testing network size estimation..." << std::endl;
    
    network::Discovery discovery;
    discovery.start(8333);
    
    // Add some peers
    for (int i = 0; i < 10; i++) {
        network::PeerInfo peer;
        peer.address = "192.168.1." + std::to_string(i + 1);
        peer.port = 8333;
        peer.timestamp = std::time(nullptr);
        peer.lastSeen = peer.timestamp;
        discovery.addPeer(peer);
    }
    
    auto stats = discovery.getStats();
    TestFramework::assertTrue(stats.networkSize > 0, "Network size should be estimated");
    TestFramework::assertTrue(stats.knownPeersCount == 10, "Should have 10 known peers");
    
    discovery.stop();
}

void testPeerListSizeLimit() {
    std::cout << "Testing peer list size limit..." << std::endl;
    
    network::Discovery discovery;
    discovery.start(8333);
    
    // Add many peers
    for (int i = 0; i < 100; i++) {
        network::PeerInfo peer;
        peer.address = "192.168.1." + std::to_string(i % 255 + 1);
        peer.port = 8333 + (i % 10);
        peer.timestamp = std::time(nullptr);
        peer.lastSeen = peer.timestamp;
        discovery.addPeer(peer);
    }
    
    // Get random peers (should be limited)
    auto randomPeers = discovery.getRandomPeers(50);
    TestFramework::assertTrue(randomPeers.size() <= 50, "Random peers should be limited to 50");
    
    discovery.stop();
}

void testMaxKnownPeersEnforced() {
    std::cout << "Testing maxKnownPeers enforcement..." << std::endl;

    network::Discovery discovery;
    network::DiscoveryConfig cfg = discovery.getConfig();
    cfg.maxKnownPeers = 5;
    discovery.setConfig(cfg);
    discovery.start(8333);

    std::vector<network::PeerInfo> peers;
    peers.reserve(20);
    for (int i = 1; i <= 20; ++i) {
        network::PeerInfo p;
        p.address = "203.0.113." + std::to_string(i); // TEST-NET-3
        p.port = 8333;
        p.timestamp = std::time(nullptr);
        p.lastSeen = p.timestamp;
        peers.push_back(p);
    }

    discovery.processIncoming(peers);

    size_t count = discovery.knownPeerCount();
    TestFramework::assertTrue(count <= 5, "knownPeerCount should not exceed maxKnownPeers");
    TestFramework::assertEqual(static_cast<size_t>(5), count, "knownPeerCount should cap at maxKnownPeers");

    discovery.stop();
}

void testDuplicatePeerRejection() {
    std::cout << "Testing duplicate peer rejection..." << std::endl;
    
    network::Discovery discovery;
    discovery.start(8333);
    
    network::PeerInfo peer;
    peer.address = "192.168.1.50";
    peer.port = 8333;
    peer.timestamp = std::time(nullptr);
    peer.lastSeen = peer.timestamp;
    
    // Add peer first time
    discovery.processIncoming({peer});
    size_t firstCount = discovery.knownPeerCount();
    
    // Try to add same peer again
    discovery.processIncoming({peer});
    size_t secondCount = discovery.knownPeerCount();
    
    TestFramework::assertEqual(firstCount, secondCount, "Duplicate peer should not be added again");
    
    discovery.stop();
}

void testExternalAddressSetting() {
    std::cout << "Testing external address setting..." << std::endl;
    
    network::Discovery discovery;
    discovery.start(8333);
    
    std::string testAddr = "203.0.113.42";
    discovery.setExternalAddress(testAddr);
    
    std::string retrieved = discovery.getExternalAddress();
    TestFramework::assertEqual(testAddr, retrieved, "External address should be set correctly");
    
    discovery.stop();
}

void testBootstrapQuarantineRotation() {
    std::cout << "Testing bootstrap quarantine rotation..." << std::endl;

    network::Discovery discovery;
    network::DiscoveryConfig cfg = discovery.getConfig();
    cfg.maxFailures = 2;
    cfg.bootstrapQuarantineSeconds = 600;
    discovery.setConfig(cfg);
    discovery.start(8333);

    const std::string unstable = "198.51.100.10";
    const std::string stable = "198.51.100.11";
    discovery.addBootstrap(unstable, 8333);
    discovery.addBootstrap(stable, 8333);

    discovery.markPeerFailed(unstable);
    discovery.markPeerFailed(unstable);

    auto nodes = discovery.getBootstrapNodes();
    uint64_t now = std::time(nullptr);
    bool unstableQuarantined = false;
    for (const auto& n : nodes) {
        if (n.address == unstable) {
            unstableQuarantined = (n.quarantineUntil > now) && (!n.active);
        }
    }
    TestFramework::assertTrue(unstableQuarantined, "unstable bootstrap should be quarantined");

    auto discovered = discovery.discoverPeers(8);
    bool unstableReturned = false;
    for (const auto& peer : discovered) {
        if (peer.find(unstable + ":") == 0) {
            unstableReturned = true;
            break;
        }
    }
    TestFramework::assertFalse(unstableReturned, "quarantined bootstrap should be excluded from discovery");

    discovery.markPeerSuccess(unstable);
    nodes = discovery.getBootstrapNodes();
    bool unstableRecovered = false;
    for (const auto& n : nodes) {
        if (n.address == unstable) {
            unstableRecovered = n.active && n.quarantineUntil == 0 && n.healthScore > 0.0;
        }
    }
    TestFramework::assertTrue(unstableRecovered, "bootstrap should recover after success");

    discovery.stop();
}

int main() {
    std::cout << "=== Peer Discovery Tests ===" << std::endl << std::endl;
    
    testPeerExchangeSendReceive();
    testPeerExchangeRateLimiting();
    testPeerAddressValidation();
    testBannedPeerRejection();
    testPeersMessageSerialization();
    testAnnounceMechanism();
    testNetworkSizeEstimation();
    testPeerListSizeLimit();
    testMaxKnownPeersEnforced();
    testDuplicatePeerRejection();
    testExternalAddressSetting();
    testBootstrapQuarantineRotation();
    
    TestFramework::printSummary();
    
    return TestFramework::getFailCount() > 0 ? 1 : 0;
}

}
}

int main() {
    return synapse::test::main();
}
