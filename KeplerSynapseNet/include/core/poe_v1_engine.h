#pragma once

// PoE v1 engine: submit → PoW gate → validator votes → epoch reward.
// powBits is a small spam filter, not Bitcoin-style mining.
// Novelty uses SimHash Hamming distance (noveltyMaxHamming). Rewards are in
// NGT atoms: base 0.10 until acceptanceSizePenaltyBytes, then -0.01 per
// extra chunk. Length is not a bonus. Clamped to min/max.
// allowSelfBootstrapValidator lets a lone devnet node vote on its own work.
//
// Nine additive layers (legacy CODE/TEXT keep the live mesh path):
// 1 Harvest Recipe — mineable unit is locator+selector+bodyHash; essays are not mint inputs
// 2 Mouth Isolation — CONSENSUS_KERNEL ∩ INFERENCE_KERNEL = empty
// 3 Quorum of Absence — "not seen", never "false"
// 4 Half-life — KNOW sleeps without independent re-witness; chain is not rewritten
// 5 Retraction — author retract marks reward unreclaimable (no stealth burn)
// 6 Anonymous seniority — ed25519 ring proof of "at least N" without which-N
// 7 Two clocks — citation DAG, not Tor arrival time
// 8 Scar of a door — class+UTC day+method; no cookies/sessions
// 9 Lymph — local replay match before recipe gossip (hash, never page bytes)

#include "core/poe_v1_objects.h"
#include "core/poe_v1_layers.h"
#include "crypto/crypto.h"
#include <cstdint>
#include <functional>
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
    // When true: 1–2 validators require all votes; n>=3 requires n/2+1.
    bool adaptiveMajority = false;
    bool allowSelfBootstrapValidator = true;
    uint32_t powBits = 16;
    uint64_t powMaxAttempts = 1000000ULL; // max PoW attempts before giving up (0 = unlimited)
    uint64_t acceptanceBaseReward = 10000000ULL;
    uint64_t acceptanceMinReward = 1000000ULL;
    uint64_t acceptanceMaxReward = 100000000ULL;
    uint32_t acceptanceBonusPerPowBit = 1000000U;
    // First 8 KiB of title+body pays the full base. Dumps pay 0.01 NGT per extra 8 KiB.
    uint32_t acceptanceSizePenaltyBytes = 8192;
    uint32_t acceptancePenaltyPerChunk = 1000000U;
    uint32_t noveltyBands = 16;
    uint32_t noveltyMaxHamming = 8;
    uint32_t maxCitations = 10;
    uint32_t minSubmitIntervalSeconds = 60;
    // Half-life: independently re-witnessed knowledge stays ACTIVE this long.
    uint64_t witnessWindowSeconds = 2592000ULL;
    uint32_t absenceQuorumN = 2;
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

    // Layer clock. Tests inject unix seconds;  unset uses wall clock only for
    // witness windows, never as a finalize-order input.
    void setNowUnix(uint64_t unixSeconds);
    void clearNowUnix();
    uint64_t nowUnix() const;

    using RecipeFetchFn = std::function<std::optional<std::vector<uint8_t>>(const poe_v1::HarvestRecipeV1&)>;
    using AbsenceSearchFn = std::function<std::vector<crypto::Hash256>(
        const poe_v1::HarvestRecipeV1&, uint64_t windowStart, uint64_t windowEnd)>;
    void setRecipeFetcher(RecipeFetchFn fn);
    void setAbsenceSearch(AbsenceSearchFn fn);

    PoeSubmitResult submitRecipe(
        const poe_v1::HarvestRecipeV1& recipe,
        const std::vector<crypto::Hash256>& knowledgeCitations,
        const crypto::PrivateKey& authorKey,
        bool autoFinalize
    );
    bool importRecipe(const poe_v1::HarvestRecipeV1& recipe, std::string* reason = nullptr);
    std::optional<poe_v1::HarvestRecipeV1> getRecipe(const crypto::Hash256& recipeId) const;
    std::optional<crypto::Hash256> getRecipeIdForSubmit(const crypto::Hash256& submitId) const;
    bool linkSubmitToRecipe(const crypto::Hash256& submitId, const crypto::Hash256& recipeId, std::string* reason = nullptr);

    bool addRecipeReplay(const poe_v1::RecipeReplayV1& replay, std::string* reason = nullptr);
    bool hasMatchingReplay(const crypto::Hash256& recipeId) const;
    bool replayRecipe(const crypto::Hash256& recipeId, const crypto::PrivateKey& reporterKey, std::string* reason = nullptr);

    bool addAbsenceReport(const poe_v1::AbsenceReportV1& report, std::string* reason = nullptr);
    bool reportAbsence(
        const crypto::Hash256& recipeId,
        uint64_t windowStart,
        uint64_t windowEnd,
        const crypto::PrivateKey& reporterKey,
        std::string* reason = nullptr);
    std::optional<poe_v1::AbsenceQuorumV1> tryAbsenceQuorum(const crypto::Hash256& recipeId);

    bool addWitness(const poe_v1::WitnessV1& witness, std::string* reason = nullptr);
    poe_v1::KnowStatus knowStatus(const crypto::Hash256& submitId) const;
    poe_v1::KnowStatus refreshKnowStatus(const crypto::Hash256& submitId);

    bool addRetract(const poe_v1::RetractV1& retract, std::string* reason = nullptr);
    bool isRetracted(const crypto::Hash256& submitId) const;
    bool isRewardUnreclaimable(const crypto::Hash256& submitId) const;
    bool shouldMintAcceptanceReward(const crypto::Hash256& submitId) const;
    std::optional<poe_v1::RetractV1> getRetract(const crypto::Hash256& submitId) const;

    bool publishSeniorityToken(const crypto::Hash256& submitId, const crypto::PrivateKey& authorKey, std::string* reason = nullptr);
    std::optional<poe_v1::SeniorityTokenV1> getSeniorityToken(const crypto::Hash256& submitId) const;
    std::vector<poe_v1::SeniorityTokenV1> listSeniorityTokens() const;
    std::optional<poe_v1::SeniorityProofV1> proveSeniority(const crypto::PrivateKey& authorKey, uint32_t n, std::string* reason = nullptr) const;
    bool verifySeniorityProof(const poe_v1::SeniorityProofV1& proof, std::string* reason = nullptr) const;

    std::vector<crypto::Hash256> citationDagFinalizeOrder(const std::vector<crypto::Hash256>& submitIds) const;

    bool importScar(const poe_v1::ScarV1& scar, std::string* reason = nullptr);
    std::optional<poe_v1::ScarV1> getScar(const crypto::Hash256& scarId) const;

    std::optional<poe_v1::HarvestRecipeV1> lymphExport(
        const poe_v1::LymphDraftV1& draft,
        const std::vector<uint8_t>& replayBytes,
        std::string* reason = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
