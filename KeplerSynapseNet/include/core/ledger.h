#pragma once

#include "crypto/crypto.h"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace synapse {
namespace core {

enum class EventType : uint8_t {
    GENESIS = 0,
    KNOWLEDGE = 1,
    TRANSFER = 2,
    VALIDATION = 3,
    MODEL_REGISTER = 4,
    MODEL_ACCESS = 5,
    PENALTY = 6,
    POE_ENTRY = 7,
    POE_VOTE = 8
};

struct Event {
    uint64_t id;
    uint64_t timestamp;
    EventType type;
    std::vector<uint8_t> data;
    crypto::Hash256 prevHash;
    crypto::Hash256 hash;
    crypto::PublicKey author;
    crypto::Signature signature;
    
    std::vector<uint8_t> serialize() const;
    static Event deserialize(const std::vector<uint8_t>& data);
    crypto::Hash256 computeHash() const;
    bool verify() const;
    std::string toJson() const;
};

struct Block {
    uint64_t height;
    uint64_t timestamp;
    crypto::Hash256 prevHash;
    crypto::Hash256 merkleRoot;
    std::vector<Event> events;
    uint32_t nonce;
    crypto::Hash256 hash;
    uint32_t difficulty = 1;
    uint64_t totalWork = 0;
    
    std::vector<uint8_t> serialize() const;
    static Block deserialize(const std::vector<uint8_t>& data);
    crypto::Hash256 computeHash() const;
    crypto::Hash256 computeMerkleRoot() const;
    std::string toJson() const;
    bool meetsTarget() const;
};

class Ledger {
public:
    Ledger();
    ~Ledger();
    
    bool open(const std::string& dbPath);
    void close();
    
    bool append(const Event& event);
    bool appendBlock(const Block& block);
    
    Event getEvent(uint64_t id) const;
    Block getBlock(uint64_t height) const;
    Block getBlockByHash(const crypto::Hash256& hash) const;
    
    std::vector<Event> getEventsByType(EventType type, size_t limit = 100) const;
    std::vector<Event> getEventsByAuthor(const crypto::PublicKey& author) const;
    std::vector<Event> getPendingEvents() const;
    
    uint64_t height() const;
    uint64_t eventCount() const;
    crypto::Hash256 tipHash() const;
    Block tip() const;
    uint32_t currentDifficulty() const;
    
    bool verify() const;
    bool verifyEvent(const Event& event) const;
    bool verifyBlock(const Block& block) const;
    
    void onNewBlock(std::function<void(const Block&)> callback);
    void onNewEvent(std::function<void(const Event&)> callback);
    void setSigner(std::function<crypto::Signature(const crypto::Hash256&)> signer);
    
    static Block createGenesisBlock();
    
    struct LedgerStats {
        uint64_t totalBlocks;
        uint64_t totalEvents;
        uint64_t totalSize;
        uint64_t avgBlockSize;
        uint64_t avgEventsPerBlock;
        double verificationRate;
    };
    
    LedgerStats getStats() const;
    std::vector<Block> getRecentBlocks(size_t count) const;
    bool verifyChain() const;
    bool exportToFile(const std::string& path) const;
    bool importFromFile(const std::string& path);
    void compact();
    uint64_t getChainWork() const;
    bool appendBlockWithValidation(const Block& block);
    std::vector<Event> getEventsByTimeRange(uint64_t startTime, uint64_t endTime, size_t limit = 100) const;
    size_t getPendingEventCount() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
