#pragma once

#include "crypto/crypto.h"
#include <sodium.h>
#include <vector>
#include <cstdint>

// Pedersen commitments hide NGT amounts on-chain. Blinding stays with the
// wallet; the chain only sees C = amount*H + blinding*G. Seed for H is
// "SynapseNet_pedersen_H_v1" — do not change it or old CTs will not verify.

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
    static std::vector<uint8_t> getH();
    static bool rangeCheck(const PedersenCommitment& commitment);
    static std::vector<uint8_t> proveRange(uint64_t amount, const std::vector<uint8_t>& blinding);
    static bool verifyRange(const std::vector<uint8_t>& commitment, const std::vector<uint8_t>& proof);
    // Schnorr: C - amount*H = blinding*G. Used when shielding a public amount.
    static std::vector<uint8_t> proveKnownAmount(
        const std::vector<uint8_t>& commitment,
        uint64_t amount,
        const std::vector<uint8_t>& blinding
    );
    static bool verifyKnownAmount(
        const std::vector<uint8_t>& commitment,
        uint64_t amount,
        const std::vector<uint8_t>& proof
    );
};

}
}
