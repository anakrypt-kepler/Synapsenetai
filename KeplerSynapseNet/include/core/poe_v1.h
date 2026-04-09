#pragma once

#include "crypto/crypto.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace synapse::core::poe_v1 {

struct TextCanonicalization {
    std::string text;
    std::vector<uint32_t> originalLineOffsets;
};

std::string canonicalizeText(const std::string& input);
std::string canonicalizeCode(const std::string& input);
std::string canonicalizeCodeForSimhash(const std::string& input);
uint64_t simhash64(const std::string& canonicalText);
uint32_t hammingDistance64(uint64_t a, uint64_t b);

using MinHash16 = std::array<uint64_t, 16>;

MinHash16 minhash16(const std::string& canonicalText, uint32_t shingleWords = 5);
uint32_t minhashEqualCount(const MinHash16& a, const MinHash16& b);

bool hasLeadingZeroBits(const crypto::Hash256& hash, uint32_t zeroBits);

std::vector<crypto::PublicKey> selectValidators(
    const crypto::Hash256& prevBlockHash,
    const crypto::Hash256& submitId,
    const std::vector<crypto::PublicKey>& validatorSet,
    uint32_t count
);

}
