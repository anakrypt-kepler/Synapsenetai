#pragma once

// Nine additive PoE layers. Legacy CODE/TEXT entries keep today's submit path.
// 1 Harvest Recipe  2 Mouth Isolation  3 Quorum of Absence
// 4 Half-life of knowledge  5 Retraction  6 Anonymous seniority
// 7 Two clocks  8 Scar of a door  9 Lymph
// Consensus kernel never takes inference-kernel payloads. LLM/GGUF is not consensus.

#include "crypto/crypto.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace synapse::core::poe_v1 {

constexpr uint32_t kMaxLocatorBytes = 2048;
constexpr uint32_t kMaxSelectorBytes = 8192;
constexpr uint32_t kMaxMediaTypeBytes = 128;
constexpr uint32_t kMaxScarFieldBytes = 64;
constexpr uint32_t kMaxVoteNoteBytes = 256;
constexpr uint32_t kMaxLymphPageBytes = 65536;
constexpr uint32_t kSeniorityEd25519Bytes = 32;

enum class KnowStatus : uint8_t {
    PENDING = 0,
    ACTIVE = 1,
    SLEEPING = 2,
    RETRACTED = 3
};

// Absence finalizes only as "not seen". Never as "false".
enum class AbsenceOutcome : uint8_t {
    NOT_SEEN = 0
};

struct HarvestRecipeV1 {
    uint8_t version = 1;
    std::string locator;
    std::string selector;
    std::string mediaType;
    crypto::Hash256 bodyHash{};
    crypto::PublicKey authorPubKey{};
    uint64_t authoredAt = 0;
    std::vector<crypto::Hash256> citations;
    crypto::Signature authorSig{};

    std::vector<uint8_t> identityBytes() const;
    std::vector<uint8_t> canonicalBytes() const;
    crypto::Hash256 recipeId() const;
    crypto::Hash256 signatureHash() const;
    bool checkLimits(std::string* reason = nullptr) const;
    bool verifySignature(std::string* reason = nullptr) const;
    bool verifyAll(std::string* reason = nullptr) const;
    std::vector<uint8_t> serialize() const;
    static std::optional<HarvestRecipeV1> deserialize(const std::vector<uint8_t>& data);
};

struct RecipeReplayV1 {
    uint8_t version = 1;
    crypto::Hash256 recipeId{};
    crypto::PublicKey reporterPubKey{};
    crypto::Hash256 observedBodyHash{};
    uint8_t match = 0;
    crypto::Signature signature{};

    std::vector<uint8_t> payloadBytes() const;
    crypto::Hash256 payloadHash() const;
    bool verifySignature(std::string* reason = nullptr) const;
    std::vector<uint8_t> serialize() const;
    static std::optional<RecipeReplayV1> deserialize(const std::vector<uint8_t>& data);
};

struct AbsenceReportV1 {
    uint8_t version = 1;
    crypto::Hash256 recipeId{};
    crypto::PublicKey reporterPubKey{};
    uint64_t windowStart = 0;
    uint64_t windowEnd = 0;
    uint8_t contradictionFound = 0;
    std::vector<crypto::Hash256> contradictingHashes;
    crypto::Signature signature{};

    std::vector<uint8_t> payloadBytes() const;
    crypto::Hash256 payloadHash() const;
    bool verifySignature(std::string* reason = nullptr) const;
    std::vector<uint8_t> serialize() const;
    static std::optional<AbsenceReportV1> deserialize(const std::vector<uint8_t>& data);
};

struct AbsenceQuorumV1 {
    crypto::Hash256 recipeId{};
    AbsenceOutcome outcome = AbsenceOutcome::NOT_SEEN;
    std::vector<AbsenceReportV1> reports;
    std::vector<uint8_t> serialize() const;
    static std::optional<AbsenceQuorumV1> deserialize(const std::vector<uint8_t>& data);
};

struct RetractV1 {
    uint8_t version = 1;
    crypto::Hash256 submitId{};
    crypto::PublicKey authorPubKey{};
    uint64_t retractedAt = 0;
    crypto::Signature signature{};

    std::vector<uint8_t> payloadBytes() const;
    crypto::Hash256 payloadHash() const;
    bool verifySignature(std::string* reason = nullptr) const;
    std::vector<uint8_t> serialize() const;
    static std::optional<RetractV1> deserialize(const std::vector<uint8_t>& data);
};

struct WitnessV1 {
    uint8_t version = 1;
    crypto::Hash256 submitId{};
    crypto::PublicKey reporterPubKey{};
    uint64_t witnessedAt = 0;
    crypto::Signature signature{};

    std::vector<uint8_t> payloadBytes() const;
    crypto::Hash256 payloadHash() const;
    bool verifySignature(std::string* reason = nullptr) const;
    std::vector<uint8_t> serialize() const;
    static std::optional<WitnessV1> deserialize(const std::vector<uint8_t>& data);
};

struct ScarV1 {
    uint8_t version = 1;
    std::string gateClass;
    std::string dayUtc;
    std::string methodClass;
    std::string cookie;
    std::string session;
    std::string header;
    std::string credential;
    std::string urlQuery;

    crypto::Hash256 scarId() const;
    std::vector<uint8_t> serialize() const;
    static std::optional<ScarV1> deserialize(const std::vector<uint8_t>& data);
};

struct LymphDraftV1 {
    HarvestRecipeV1 recipe;
    std::vector<uint8_t> pageBytes;
};

struct SeniorityTokenV1 {
    crypto::Hash256 submitId{};
    std::array<uint8_t, kSeniorityEd25519Bytes> point{};
    crypto::PublicKey authorPubKey{};
    crypto::Signature bindingSig{};

    std::vector<uint8_t> payloadBytes() const;
    crypto::Hash256 payloadHash() const;
    bool verifyBinding(std::string* reason = nullptr) const;
    std::vector<uint8_t> serialize() const;
    static std::optional<SeniorityTokenV1> deserialize(const std::vector<uint8_t>& data);
};

struct SeniorityProofV1 {
    uint32_t threshold = 0;
    std::vector<std::array<uint8_t, kSeniorityEd25519Bytes>> ring;
    std::vector<std::vector<uint8_t>> ringSigs;

    std::vector<uint8_t> serialize() const;
    static std::optional<SeniorityProofV1> deserialize(const std::vector<uint8_t>& data);
};

bool containsInferenceKernelPayload(const std::string& text);
bool consensusPayloadClean(const std::string& title, const std::string& body, std::string* reason = nullptr);
bool voteNoteClean(const std::string& note, std::string* reason = nullptr);

bool scarAcceptable(const ScarV1& scar, std::string* reason = nullptr);
bool isUtcDateYYYYMMDD(const std::string& day);

crypto::Hash256 hashFetchedBody(const std::vector<uint8_t>& bytes);

std::optional<HarvestRecipeV1> lymphExportIfMatch(
    const LymphDraftV1& draft,
    const std::vector<uint8_t>& replayBytes,
    std::string* reason = nullptr);

bool signHarvestRecipeV1(HarvestRecipeV1& recipe, const crypto::PrivateKey& authorKey);
bool signRecipeReplayV1(RecipeReplayV1& replay, const crypto::PrivateKey& reporterKey);
bool signAbsenceReportV1(AbsenceReportV1& report, const crypto::PrivateKey& reporterKey);
bool signRetractV1(RetractV1& retract, const crypto::PrivateKey& authorKey);
bool signWitnessV1(WitnessV1& witness, const crypto::PrivateKey& reporterKey);

bool deriveSeniorityScalar(
    const crypto::PrivateKey& authorKey,
    const crypto::Hash256& submitId,
    std::vector<uint8_t>* outScalar);
bool deriveSeniorityPoint(
    const crypto::PrivateKey& authorKey,
    const crypto::Hash256& submitId,
    std::array<uint8_t, kSeniorityEd25519Bytes>* outPoint);
bool bindSeniorityToken(SeniorityTokenV1& token, const crypto::PrivateKey& authorKey);

std::optional<SeniorityProofV1> proveAnonymousSeniority(
    const crypto::PrivateKey& authorKey,
    const std::vector<crypto::Hash256>& ownedSubmitIds,
    const std::vector<std::array<uint8_t, kSeniorityEd25519Bytes>>& ringPoints,
    uint32_t threshold,
    std::string* reason = nullptr);
bool verifyAnonymousSeniority(
    const SeniorityProofV1& proof,
    const std::vector<std::array<uint8_t, kSeniorityEd25519Bytes>>& publishedPoints,
    std::string* reason = nullptr);

std::vector<crypto::Hash256> citationDagOrder(
    const std::vector<crypto::Hash256>& submitIds,
    const std::vector<std::vector<crypto::Hash256>>& citedContentIds,
    const std::vector<crypto::Hash256>& nodeContentIds);

}
