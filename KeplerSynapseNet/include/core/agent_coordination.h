#pragma once

#include "crypto/crypto.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <optional>

namespace synapse::core {

using AgentId = crypto::PublicKey;

struct AgentIdentity {
    AgentId id{};
    crypto::PrivateKey privateKey{};
    
    static AgentIdentity generate();
    static AgentIdentity fromSeed(const crypto::Hash256& seed);
    bool valid() const { return id != AgentId{}; }
};

enum class RoomMessageType : uint8_t {
    TASK = 0,
    REVIEW = 1,
    DISPUTE = 2,
    ALERT = 3
};

enum class RoomType : uint8_t {
    TASKS = 0,
    REVIEWS = 1,
    DISPUTES = 2,
    ALERTS = 3
};

struct RoomMessage {
    uint64_t sequence = 0;
    uint64_t timestamp = 0;
    RoomMessageType type = RoomMessageType::TASK;
    AgentId author{};
    std::string roomId;
    std::string payload;
    crypto::Signature signature{};
    
    crypto::Hash256 computeHash() const;
    std::vector<uint8_t> canonicalBytes() const;
};

struct RoomConfig {
    uint32_t maxMessages = 1000;
    uint32_t retentionSeconds = 86400 * 7;
    uint32_t maxPayloadBytes = 8192;
};

struct SignedArtifact {
    crypto::Hash256 hash{};
    RoomMessage message;
    uint64_t insertedAt = 0;
};

class AgentRoom {
public:
    explicit AgentRoom(const std::string& roomId, RoomType type, const RoomConfig& cfg = RoomConfig{});
    
    std::string roomId() const { return roomId_; }
    RoomType roomType() const { return type_; }
    
    bool post(const AgentIdentity& author, RoomMessageType type, const std::string& payload, uint64_t timestamp);
    bool validateMessage(const RoomMessage& msg) const;
    bool addVerifiedMessage(const RoomMessage& msg);
    
    std::vector<SignedArtifact> getArtifacts(uint64_t sinceTimestamp = 0, uint32_t limit = 100) const;
    std::optional<SignedArtifact> getArtifact(const crypto::Hash256& hash) const;
    
    uint64_t nextSequence(const AgentId& author) const;
    void prune(uint64_t nowTimestamp);
    
    uint64_t messageCount() const;
    
private:
    std::string roomId_;
    RoomType type_;
    RoomConfig cfg_;
    mutable std::mutex mtx_;
    std::vector<SignedArtifact> artifacts_;
    std::map<AgentId, uint64_t> authorSequences_;
    std::map<crypto::Hash256, size_t> hashIndex_;
    std::set<crypto::Hash256> seenHashes_;
    
    bool checkAntiReplay(const RoomMessage& msg) const;
};

struct CoordinationConfig {
    uint32_t maxRoomsPerType = 10;
    uint32_t pruneIntervalSeconds = 3600;
    uint32_t maxArtifactHistory = 10000;
};

struct ObservatoryEntry {
    crypto::Hash256 hash{};
    std::string roomId;
    RoomMessageType type = RoomMessageType::TASK;
    AgentId author{};
    std::string payloadPreview;
    uint64_t timestamp = 0;
};

struct CoordinationRecoveryStats {
    uint64_t roomCount = 0;
    uint64_t roomArtifacts = 0;
    uint64_t observatoryEntries = 0;
    uint64_t rebuiltEntries = 0;
    uint64_t rebuiltIndexEntries = 0;
};

class CoordinationHub {
public:
    explicit CoordinationHub(const CoordinationConfig& cfg = CoordinationConfig{});

    CoordinationConfig config() const;
    void setConfig(const CoordinationConfig& cfg);
    
    bool createRoom(const std::string& roomId, RoomType type, const RoomConfig& roomCfg = RoomConfig{});
    bool hasRoom(const std::string& roomId) const;
    AgentRoom* getRoom(const std::string& roomId);
    const AgentRoom* getRoom(const std::string& roomId) const;
    
    bool postToRoom(const std::string& roomId, const AgentIdentity& author, 
                    RoomMessageType type, const std::string& payload, uint64_t timestamp);
    
    std::vector<std::string> listRooms(RoomType type) const;
    std::vector<std::string> listAllRooms() const;
    
    std::vector<ObservatoryEntry> getObservatoryFeed(uint64_t sinceTimestamp = 0, uint32_t limit = 100) const;
    std::optional<SignedArtifact> getArtifact(const crypto::Hash256& hash) const;
    
    void pruneAll(uint64_t nowTimestamp);
    void periodicMaintenance(uint64_t nowTimestamp);
    bool verifyAndRepairIndexes(CoordinationRecoveryStats* stats = nullptr,
                                std::string* reason = nullptr);
    
    uint64_t totalMessages() const;
    
private:
    CoordinationConfig cfg_;
    mutable std::mutex mtx_;
    std::map<std::string, std::unique_ptr<AgentRoom>> rooms_;
    std::vector<ObservatoryEntry> observatoryIndex_;
    std::map<crypto::Hash256, size_t> artifactIndex_;
    uint64_t lastPrune_ = 0;
    
    void addToObservatory(const SignedArtifact& artifact);
};

class ObservatoryFeed {
public:
    explicit ObservatoryFeed(const CoordinationHub& hub);
    
    std::vector<ObservatoryEntry> getFeed(uint64_t sinceTimestamp = 0, uint32_t limit = 100) const;
    std::optional<SignedArtifact> getArtifact(const crypto::Hash256& hash) const;
    std::optional<RoomMessage> getMessage(const crypto::Hash256& hash) const;
    
private:
    const CoordinationHub& hub_;
};

std::string roomMessageTypeToString(RoomMessageType type);
RoomMessageType stringToRoomMessageType(const std::string& s);
std::string roomTypeToString(RoomType type);
RoomType stringToRoomType(const std::string& s);

}
