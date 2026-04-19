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

enum class ConsensusState : uint8_t {
    PENDING = 0,
    VALIDATING = 1,
    ACCEPTED = 2,
    REJECTED = 3,
    EXPIRED = 4,
    APPROVED = 5
};

enum class VoteType : uint8_t {
    APPROVE = 0,
    REJECT = 1,
    ABSTAIN = 2
};

struct Validator {
    crypto::PublicKey pubKey;
    std::string address;
    uint64_t stake;
    double reputation;
    uint32_t validationsCompleted;
    uint64_t lastActive;
    bool eligible;
    bool active = true;
    uint64_t totalRewards = 0;
};

struct Vote {
    uint64_t eventId;
    crypto::PublicKey validator;
    VoteType type;
    double scoreGiven;
    uint64_t timestamp;
    crypto::Signature signature;
    
    std::vector<uint8_t> serialize() const;
    static Vote deserialize(const std::vector<uint8_t>& data);
    crypto::Hash256 computeHash() const;
    bool verify() const;
};

struct ValidationResult {
    uint64_t eventId;
    ConsensusState state;
    uint32_t approveVotes;
    uint32_t rejectVotes;
    uint32_t totalVotes;
    uint32_t requiredVotes;
    double averageScore;
    uint64_t startTime;
    uint64_t endTime;
    std::vector<Vote> votes;
    uint64_t reward;
};

struct ConsensusConfig {
    uint32_t minValidators = 5;
    uint32_t maxValidators = 11;
    uint64_t minStake = 100;
    uint64_t validationTimeout = 3600;
    double majorityThreshold = 0.6;
    double minReputation = 0.5;
    uint64_t submissionStake = 10;
    double quorum = 0.67;
    uint32_t blockFinality = 6;
    uint32_t votingTimeout = 300;
    std::vector<uint8_t> seed;
};

class Consensus {
public:
    Consensus();
    ~Consensus();
    
    bool open(const std::string& dbPath);
    void close();
    void setConfig(const ConsensusConfig& config);
    
    uint64_t submitForValidation(uint64_t eventId, const crypto::PublicKey& submitter);
    bool vote(const Vote& vote);
    bool finalizeValidation(uint64_t eventId);
    
    ConsensusState getState(uint64_t eventId) const;
    ValidationResult getResult(uint64_t eventId) const;
    std::vector<uint64_t> getPending() const;
    std::vector<uint64_t> getValidating() const;
    std::vector<Vote> getVotesFor(uint64_t eventId) const;
    
    bool registerValidator(const Validator& validator);
    bool updateValidatorStake(const crypto::PublicKey& pubKey, uint64_t newStake);
    bool updateValidatorReputation(const crypto::PublicKey& pubKey, double delta);
    Validator getValidator(const crypto::PublicKey& pubKey) const;
    std::vector<Validator> getEligibleValidators() const;
    std::vector<Validator> selectValidators(uint64_t eventId, uint32_t count) const;
    bool isEligibleValidator(const crypto::PublicKey& pubKey) const;
    
    uint64_t calculateReward(const ValidationResult& result) const;
    uint64_t calculatePenalty(const ValidationResult& result) const;
    
    void onStateChange(std::function<void(uint64_t, ConsensusState)> callback);
    void onValidationComplete(std::function<void(const ValidationResult&)> callback);
    
    void processTimeouts();
    size_t pendingCount() const;
    size_t validatorCount() const;
    
    struct ConsensusStats {
        uint64_t totalValidations;
        uint64_t pendingValidations;
        uint64_t approvedValidations;
        uint64_t rejectedValidations;
        uint64_t totalValidators;
        uint64_t activeValidators;
        double avgValidationTime;
        double avgScore;
    };
    
    ConsensusStats getStats() const;
    
    std::vector<Validator> getValidators() const;
    std::vector<Validator> getTopValidators(size_t count) const;
    ValidationResult getValidationResult(uint64_t validationId) const;
    std::vector<ValidationResult> getRecentResults(size_t count) const;
    ConsensusConfig getConfig() const;
    uint64_t getTotalStake() const;
    double getApprovalRate() const;
    bool slashValidator(const std::string& address, uint64_t amount);
    bool rewardValidator(const std::string& address, uint64_t amount);
    std::vector<std::string> getActiveValidatorAddresses() const;
    uint64_t getValidatorStake(const std::string& address) const;
    void setMinStake(uint64_t amount);
    void setQuorum(double quorum);
    bool exportValidators(const std::string& path) const;
    void clearExpiredResults();
    bool isValidatorActive(const std::string& address) const;
    void updateValidatorActivity(const std::string& address);
    std::string selectProposer() const;
    void setVotingTimeout(uint32_t seconds);
    bool importValidators(const std::string& path);
    void setOnProposalCallback(std::function<void(const std::string&)> callback);
    void setOnVoteCallback(std::function<void(const std::string&, bool)> callback);
    bool proposeBlock(const Block& block, const std::string& proposer);
    bool voteOnProposal(const std::string& proposalId, const std::string& validator, bool approve);
    void setOnValidatorJoined(std::function<void(const std::string&)> callback);
    void setOnValidatorLeft(std::function<void(const std::string&)> callback);
    void setLedger(Ledger* ledger);
    std::vector<std::string> getActiveValidators() const;
    void setBlockFinality(uint32_t blocks);
    uint32_t getBlockFinality() const;
    bool isBlockFinalized(uint64_t height) const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
