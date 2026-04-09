#pragma once

#include "core/poe_v1.h"
#include "crypto/crypto.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace synapse::core::poe_v1 {

enum class ContentType : uint8_t {
    TEXT = 0,
    QA = 1,
    CODE = 2,
    LINKLIST = 3,
    OTHER = 4
};

struct LimitsV1 {
    uint32_t maxTitleBytes = 256;
    uint32_t maxBodyBytes = 8192;
    uint32_t maxCitations = 16;
    uint32_t minPowBits = 16;
    uint32_t maxPowBits = 28;
};

struct KnowledgeEntryV1 {
    uint8_t version = 1;
    uint64_t timestamp = 0;
    crypto::PublicKey authorPubKey{};
    ContentType contentType = ContentType::TEXT;
    std::string title;
    std::string body;
    std::vector<crypto::Hash256> citations;
    uint64_t powNonce = 0;
    uint32_t powBits = 0;
    crypto::Signature authorSig{};

    std::vector<uint8_t> canonicalBodyBytes() const;
    crypto::Hash256 contentId() const;
    crypto::Hash256 submitId() const;
    crypto::Hash256 signatureHash() const;
    uint64_t contentSimhash64() const;
    crypto::Hash256 bodyFingerprint() const;

    bool checkLimits(const LimitsV1& limits, std::string* reason = nullptr) const;
    bool verifyPoW(std::string* reason = nullptr) const;
    bool verifySignature(std::string* reason = nullptr) const;
    bool verifyAll(const LimitsV1& limits, std::string* reason = nullptr) const;

    std::vector<uint8_t> serialize() const;
    static std::optional<KnowledgeEntryV1> deserialize(const std::vector<uint8_t>& data);
};

struct ValidationVoteV1 {
    uint8_t version = 1;
    crypto::Hash256 submitId{};
    crypto::Hash256 prevBlockHash{};
    crypto::PublicKey validatorPubKey{};
    uint32_t flags = 0;
    std::array<uint16_t, 3> scores{0, 0, 0};
    crypto::Signature signature{};

    std::vector<uint8_t> payloadBytes() const;
    crypto::Hash256 payloadHash() const;
    bool verifySignature(std::string* reason = nullptr) const;

    std::vector<uint8_t> serialize() const;
    static std::optional<ValidationVoteV1> deserialize(const std::vector<uint8_t>& data);
};

struct FinalizationRecordV1 {
    crypto::Hash256 submitId{};
    crypto::Hash256 prevBlockHash{};
    crypto::Hash256 validatorSetHash{};
    std::vector<ValidationVoteV1> votes;
    uint64_t finalizedAt = 0;

    std::vector<uint8_t> serialize() const;
    static std::optional<FinalizationRecordV1> deserialize(const std::vector<uint8_t>& data);
};

crypto::Hash256 validatorSetHashV1(const std::vector<crypto::PublicKey>& validators);
bool signKnowledgeEntryV1(KnowledgeEntryV1& entry, const crypto::PrivateKey& authorKey);
bool signValidationVoteV1(ValidationVoteV1& vote, const crypto::PrivateKey& validatorKey);

}

