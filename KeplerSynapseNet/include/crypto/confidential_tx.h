#pragma once

#include "crypto/crypto.h"
#include <sodium.h>
#include <vector>
#include <cstdint>

namespace synapse {
namespace crypto {

struct PedersenCommitment {
    std::vector<uint8_t> commitment;
    std::vector<uint8_t> blinding;
    bool verify(uint64_t amount) const;
    std::vector<uint8_t> serialize() const;
    static PedersenCommitment deserialize(const std::vector<uint8_t>& data);
};

class ConfidentialTx {
public:
    static PedersenCommitment commit(uint64_t amount, const std::vector<uint8_t>& blindingFactor);
    static PedersenCommitment commit(uint64_t amount);
    static bool verifyBalance(
        const std::vector<PedersenCommitment>& inputs,
        const std::vector<PedersenCommitment>& outputs,
        uint64_t fee
    );
    static std::vector<uint8_t> generateBlindingFactor();
    static std::vector<uint8_t> blindingSum(
        const std::vector<std::vector<uint8_t>>& blindingFactors,
        bool negate
    );
    static bool rangeCheck(const PedersenCommitment& commitment);
private:
    static std::vector<uint8_t> getH();
};

}
}
