#pragma once

#include "core/poe_v1_objects.h"
#include "crypto/crypto.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace synapse::core {

struct PoeV1Config {
    poe_v1::LimitsV1 limits{};
    std::string validatorMode = "static";
    uint64_t validatorMinStakeAtoms = 0;
    uint32_t validatorsN = 1;
    uint32_t validatorsM = 1;
    bool adaptiveQuorum = false;
    uint32_t adaptiveMinVotes = 1;
    bool adaptiveMajority = false;
    bool allowSelfBootstrapValidator = true;
    uint32_t powBits = 16;
    uint64_t powMaxAttempts = 1000000ULL; // max PoW attempts before giving up (0 = unlimited)
    uint64_t acceptanceBaseReward = 10000000ULL;
    uint64_t acceptanceMinReward = 1000000ULL;
    uint64_t acceptanceMaxReward = 100000000ULL;
    uint32_t acceptanceBonusPerPowBit = 1000000U;
    uint32_t acceptanceSizePenaltyBytes = 2048;
    uint32_t acceptancePenaltyPerChunk = 1000000U;
    uint32_t noveltyBands = 16;
    uint32_t noveltyMaxHamming = 8;
    uint32_t maxCitations = 10;
    uint32_t minSubmitIntervalSeconds = 60;
};

struct PoeSubmitResult {
    bool ok = false;
    std::string error;
    crypto::Hash256 submitId{};
    crypto::Hash256 contentId{};
    uint64_t simhash64 = 0;
    bool finalized = false;
    uint64_t acceptanceReward = 0;
};

struct PoeEpochAllocation {
    crypto::Hash256 submitId{};
    crypto::Hash256 contentId{};
    crypto::PublicKey authorPubKey{};
    uint64_t score = 0;
    uint64_t amount = 0;
};

struct PoeEpochResult {
    bool ok = false;
    std::string error;
    uint64_t epochId = 0;
    uint32_t iterations = 0;
    crypto::Hash256 epochSeed{};
    uint64_t totalBudget = 0;
    crypto::Hash256 allocationHash{};
    std::vector<PoeEpochAllocation> allocations;
};

class PoeV1Engine {
public:
    PoeV1Engine();
    ~PoeV1Engine();

    bool open(const std::string& dbPath);
    void close();

    void setConfig(const PoeV1Config& cfg);
    PoeV1Config getConfig() const;

    void setStaticValidators(const std::vector<crypto::PublicKey>& validators);
    std::vector<crypto::PublicKey> getStaticValidators() const;
    void setValidatorIdentity(const crypto::PublicKey& validator, bool enabled);
    bool hasValidatorIdentity(const crypto::PublicKey& validator) const;
    void setValidatorStake(const crypto::PublicKey& validator, uint64_t stakeAtoms);
    uint64_t getValidatorStake(const crypto::PublicKey& validator) const;
    std::vector<crypto::PublicKey> getDeterministicValidators() const;
    uint32_t effectiveSelectedValidators() const;
    uint32_t effectiveRequiredVotes() const;

    PoeSubmitResult submit(
        const poe_v1::ContentType type,
        const std::string& title,
        const std::string& body,
        const std::vector<crypto::Hash256>& citations,
        const crypto::PrivateKey& authorKey,
        bool autoFinalize
    );

    bool precheckEntry(const poe_v1::KnowledgeEntryV1& entry, std::string* reason = nullptr) const;
    bool importEntry(const poe_v1::KnowledgeEntryV1& entry, std::string* reason = nullptr);
    bool addVote(const poe_v1::ValidationVoteV1& vote);
    std::optional<poe_v1::ValidationVoteV1> getVoteById(const crypto::Hash256& voteId) const;
    std::vector<crypto::Hash256> listEntryIds(size_t limit = 0) const;
    std::vector<crypto::Hash256> listVoteIds(size_t limit = 0) const;
    std::vector<poe_v1::ValidationVoteV1> getVotesForSubmit(const crypto::Hash256& submitId) const;
    std::optional<poe_v1::FinalizationRecordV1> finalize(const crypto::Hash256& submitId);

    bool isFinalized(const crypto::Hash256& submitId) const;
    uint64_t totalEntries() const;
    uint64_t totalFinalized() const;

    std::optional<poe_v1::KnowledgeEntryV1> getEntry(const crypto::Hash256& submitId) const;
    std::optional<crypto::Hash256> getSubmitIdByContentId(const crypto::Hash256& contentId) const;
    std::optional<poe_v1::KnowledgeEntryV1> getEntryByContentId(const crypto::Hash256& contentId) const;
    std::optional<poe_v1::FinalizationRecordV1> getFinalization(const crypto::Hash256& submitId) const;

    uint64_t calculateAcceptanceReward(const poe_v1::KnowledgeEntryV1& entry) const;
    crypto::Hash256 chainSeed() const;

    PoeEpochResult runEpoch(uint64_t totalBudget, uint32_t iterations = 20);
    std::vector<uint64_t> listEpochIds(size_t limit = 0) const;
    std::optional<PoeEpochResult> getEpoch(uint64_t epochId) const;
    bool importEpoch(const PoeEpochResult& epoch);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
