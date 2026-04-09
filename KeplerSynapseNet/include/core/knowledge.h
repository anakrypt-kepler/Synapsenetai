#pragma once

#include "crypto/crypto.h"
#include "ledger.h"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace synapse {
namespace core {

struct KnowledgeEntry {
    uint64_t id;
    std::string question;
    std::string answer;
    std::string source;
    uint64_t timestamp;
    crypto::PublicKey author;
    crypto::Signature signature;
    double score;
    uint32_t validations;
    std::vector<std::string> tags;
    crypto::Hash256 hash;
    
    std::vector<uint8_t> serialize() const;
    static KnowledgeEntry deserialize(const std::vector<uint8_t>& data);
    crypto::Hash256 computeHash() const;
    bool verify() const;
};

struct ValidationVote {
    uint64_t knowledgeId;
    crypto::PublicKey validator;
    bool approve;
    double scoreGiven;
    uint64_t timestamp;
    crypto::Signature signature;
};

struct KnowledgeStats {
    uint64_t totalEntries;
    uint64_t validatedEntries;
    uint64_t totalValidations;
    double avgScore;
};

struct KnowledgeConfig {
    double minScore = 0.0;
    size_t maxEntries = 1000000;
    uint32_t minValidationsRequired = 3;
};

class KnowledgeNetwork {
public:
    KnowledgeNetwork();
    explicit KnowledgeNetwork(const KnowledgeConfig& config);
    ~KnowledgeNetwork();
    
    bool open(const std::string& dbPath);
    void close();
    
    uint64_t submit(const KnowledgeEntry& entry, const crypto::PrivateKey& authorKey);
    bool importEntry(const KnowledgeEntry& entry);
    bool vote(const ValidationVote& vote, const crypto::PrivateKey& validatorKey);
    
    KnowledgeEntry get(uint64_t id) const;
    std::vector<KnowledgeEntry> search(const std::string& query, size_t limit = 10) const;
    std::vector<KnowledgeEntry> getByAuthor(const crypto::PublicKey& author, size_t limit = 100) const;
    std::vector<KnowledgeEntry> getByTag(const std::string& tag, size_t limit = 100) const;
    std::vector<KnowledgeEntry> getRecent(size_t limit = 50) const;
    std::vector<KnowledgeEntry> getPendingValidation(size_t limit = 50) const;
    
    uint64_t totalEntries() const;
    double getScore(uint64_t id) const;
    uint32_t getValidationCount(uint64_t id) const;
    
    double calculateReward(const KnowledgeEntry& entry) const;
    bool isDuplicate(const KnowledgeEntry& entry) const;
    
    void onNewEntry(std::function<void(const KnowledgeEntry&)> callback);
    void onValidation(std::function<void(uint64_t, double)> callback);
    
    KnowledgeStats getStats() const;
    std::vector<KnowledgeEntry> getRecentEntries(size_t count) const;
    std::vector<KnowledgeEntry> getTopRated(size_t count) const;
    std::vector<std::string> getCategories() const;
    size_t getCategoryCount(const std::string& category) const;
    bool exportToJson(const std::string& path) const;
    void setMinScore(double score);
    void setMaxEntries(size_t max);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
