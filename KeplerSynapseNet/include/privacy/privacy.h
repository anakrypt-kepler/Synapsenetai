#ifndef SYNAPSE_PRIVACY_H
#define SYNAPSE_PRIVACY_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace synapse {
namespace privacy {

class Socks5Proxy {
public:
    Socks5Proxy();
    ~Socks5Proxy();
    
    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    bool isConnected() const;
    
    bool authenticate();
    bool connectToTarget(const std::string& targetHost, uint16_t targetPort);
    std::vector<uint8_t> sendData(const std::vector<uint8_t>& data);
    
    void setProxy(const std::string& host, uint16_t port);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OnionService {
public:
    OnionService();
    ~OnionService();
    
    bool init(const std::string& serviceDir);
    bool start(uint16_t virtualPort, uint16_t targetPort);
    void stop();
    bool isRunning() const;
    
    std::string getHostname() const;
    bool rotateIdentity();
    void setRotationInterval(uint64_t seconds);
    void enableAutoRotation(bool enable);
    bool shouldRotate() const;
    void setControlConfig(const std::string& host, uint16_t port,
                          const std::string& password,
                          const std::string& cookiePath);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class SessionCrypto {
public:
    SessionCrypto();
    ~SessionCrypto();
    
    bool createSession(const std::string& sessionId, const std::vector<uint8_t>& sharedSecret);
    bool destroySession(const std::string& sessionId);
    void clearAllSessions();
    
    std::vector<uint8_t> encrypt(const std::string& sessionId, const std::vector<uint8_t>& plaintext);
    std::vector<uint8_t> decrypt(const std::string& sessionId, const std::vector<uint8_t>& ciphertext);
    
    bool hasSession(const std::string& sessionId) const;
    bool rotateKey(const std::string& sessionId);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class StealthAddress {
public:
    StealthAddress();
    ~StealthAddress();
    
    bool generateKeys();
    std::vector<uint8_t> getViewPublicKey() const;
    std::vector<uint8_t> getSpendPublicKey() const;
    
    std::vector<uint8_t> generateOneTimeAddress(const std::vector<uint8_t>& recipientViewPub,
                                                 const std::vector<uint8_t>& recipientSpendPub,
                                                 std::vector<uint8_t>& ephemeralPub);
    
    bool checkOwnership(const std::vector<uint8_t>& oneTimeAddress,
                        const std::vector<uint8_t>& ephemeralPub) const;
    
    std::vector<uint8_t> deriveSpendingKey(const std::vector<uint8_t>& ephemeralPub) const;
    
    std::string encodeAddress() const;
    static bool decodeAddress(const std::string& address,
                              std::vector<uint8_t>& viewPub,
                              std::vector<uint8_t>& spendPub);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class DandelionPhase {
    STEM,
    FLUFF
};

class Dandelion {
public:
    Dandelion();
    ~Dandelion();
    
    void setHopRange(uint32_t minHops, uint32_t maxHops);
    void setFluffProbability(double probability);
    void setStemTimeout(uint64_t seconds);
    
    DandelionPhase submitTransaction(const std::string& txId,
                                      const std::vector<uint8_t>& txData,
                                      const std::vector<std::string>& peers);
    
    DandelionPhase relayTransaction(const std::string& txId,
                                     const std::vector<uint8_t>& txData,
                                     const std::vector<std::string>& peers,
                                     uint32_t currentHop);
    
    std::string getStemPeer(const std::string& txId) const;
    bool isFluffed(const std::string& txId) const;
    void markFluffed(const std::string& txId);
    void clearTransaction(const std::string& txId);
    
    void adaptToNetworkSize(size_t networkSize);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct MixMessage {
    std::string id;
    std::vector<uint8_t> data;
    uint64_t timestamp;
    uint64_t delay;
    bool isDummy = false;
};

class MixInference {
public:
    MixInference();
    ~MixInference();
    
    bool init(const std::vector<std::string>& mixNodes);
    void start();
    void stop();
    
    void setBatchSize(uint32_t size);
    void setBatchTimeout(uint64_t milliseconds);
    void setDelayRange(uint64_t minMs, uint64_t maxMs);
    void setDummyInterval(uint64_t milliseconds);
    void setSendCallback(std::function<void(const MixMessage&)> callback);
    
    std::string submitQuery(const std::vector<uint8_t>& query,
                            const std::vector<std::vector<uint8_t>>& nodeKeys);
    
    std::vector<MixMessage> getBatch();
    bool hasPendingMessages() const;
    size_t pendingCount() const;
    
    void addDummyTraffic();
    
    uint64_t getMessagesIn() const;
    uint64_t getMessagesOut() const;
    uint64_t getDummyCount() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Amnesia {
public:
    Amnesia();
    ~Amnesia();
    
    bool enable();
    void disable();
    bool isEnabled() const;
    
    void* allocateSecure(size_t size);
    void freeSecure(void* ptr);
    void wipeMemory(void* ptr, size_t size);
    void wipeAllAllocations();
    
    size_t secureMemoryUsage() const;
    
    bool lockMemory(void* ptr, size_t size);
    bool unlockMemory(void* ptr, size_t size);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class HiddenVolume {
public:
    HiddenVolume();
    ~HiddenVolume();
    
    bool create(const std::string& path, size_t size,
                const std::string& outerPassword,
                const std::string& hiddenPassword);
    
    bool mountOuter(const std::string& path, const std::string& password);
    bool mountHidden(const std::string& path, const std::string& password);
    void unmount();
    
    std::vector<uint8_t> read(size_t offset, size_t size, bool hidden = false);
    bool write(size_t offset, const std::vector<uint8_t>& data, bool hidden = false);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class DecoyTraffic {
public:
    DecoyTraffic();
    ~DecoyTraffic();
    
    void start();
    void stop();
    bool isRunning() const;
    
    void setIntervalRange(uint64_t minMs, uint64_t maxMs);
    void setSizeRange(size_t minBytes, size_t maxBytes);
    void setTrafficRatio(double ratio);
    
    void setSendCallback(std::function<void(const std::vector<uint8_t>&)> callback);
    
    std::vector<uint8_t> generatePacket();
    static bool isDecoyPacket(const std::vector<uint8_t>& packet);
    
    void adaptToTraffic(size_t realPacketsPerSecond);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PrivacyManager {
public:
    PrivacyManager();
    ~PrivacyManager();
    
    bool init();
    void shutdown();
    
    Socks5Proxy& socks5();
    OnionService& onion();
    SessionCrypto& sessionCrypto();
    StealthAddress& stealth();
    Dandelion& dandelion();
    MixInference& mixInference();
    Amnesia& amnesia();
    HiddenVolume& hiddenVolume();
    DecoyTraffic& decoyTraffic();
    
    void enablePrivacyMode(bool enable);
    bool isPrivacyModeEnabled() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class PrivacyMode {
    NONE,
    BASIC,
    FULL,
    PARANOID
};

struct PrivacyConfig {
    bool useTor = false;
    uint32_t circuitCount = 3;
    bool rotateIdentity = true;
    uint64_t rotationInterval = 3600;
    bool useDandelion = true;
    bool useMixnet = false;
    bool useStealthAddresses = true;
    bool enableDecoyTraffic = false;
    std::string torSocksHost = "127.0.0.1";
    uint16_t torSocksPort = 9050;
    std::string torControlHost = "127.0.0.1";
    uint16_t torControlPort = 9051;
    std::string torControlPassword;
    std::string torControlCookiePath;
    std::string onionServiceDir;
    uint16_t onionVirtualPort = 8333;
    uint16_t onionTargetPort = 8333;
};

class Privacy {
public:
    Privacy();
    ~Privacy();
    
    bool init(const PrivacyConfig& config);
    void shutdown();
    
    bool enable(PrivacyMode mode);
    void disable();
    bool isEnabled() const;
    PrivacyMode getMode() const;
    
    std::string getOnionAddress() const;
    bool rotateIdentity();
    
    bool connectThroughTor(const std::string& host, uint16_t port);
    std::vector<uint8_t> sendThroughTor(const std::vector<uint8_t>& data);
    
    void setConfig(const PrivacyConfig& config);
    PrivacyConfig getConfig() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}

#endif
