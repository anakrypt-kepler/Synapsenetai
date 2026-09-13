#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <string>

#include "crypto/crypto.h"

// MLSAG-style ring signature for private NGT spends.
// keyImage prevents double-spend without revealing which ring member signed.
// Ring members are decoys from the UTXO pool (see confidential_tx.cpp).

namespace synapse {
namespace crypto {

struct RingSignature {
    std::vector<uint8_t> keyImage;
    std::vector<uint8_t> c0;
    std::vector<std::vector<uint8_t>> responses;
    std::vector<uint8_t> serialize() const;
    static RingSignature deserialize(const std::vector<uint8_t>& data);
};

// Two-row MLSAG: row 0 is the one-time key P, row 1 is the commitment
// offset C - Ctilde. Key image is only on P (double-spend tag).
struct MlsagSignature {
    std::vector<uint8_t> keyImage;
    std::vector<uint8_t> c0;
    std::vector<std::vector<uint8_t>> respKey;
    std::vector<std::vector<uint8_t>> respCommit;
    std::vector<uint8_t> serialize() const;
    static MlsagSignature deserialize(const std::vector<uint8_t>& data);
};

class RingSign {
public:
    RingSign();
    ~RingSign();
    static RingSignature sign(
        const std::vector<uint8_t>& message,
        const std::vector<std::vector<uint8_t>>& ring,
        const std::vector<uint8_t>& privateKey,
        size_t signerIndex,
        bool recordImage = true
    );
    static MlsagSignature signMlsag(
        const std::vector<uint8_t>& message,
        const std::vector<std::vector<uint8_t>>& ringP,
        const std::vector<std::vector<uint8_t>>& ringC,
        const std::vector<uint8_t>& privP,
        const std::vector<uint8_t>& privC,
        size_t signerIndex,
        bool recordImage = true
    );
    static bool verifyMlsag(
        const std::vector<uint8_t>& message,
        const std::vector<std::vector<uint8_t>>& ringP,
        const std::vector<std::vector<uint8_t>>& ringC,
        const MlsagSignature& sig
    );
    static std::vector<uint8_t> pointSub(
        const std::vector<uint8_t>& a,
        const std::vector<uint8_t>& b
    );
    static std::vector<uint8_t> pointAddPublic(
        const std::vector<uint8_t>& a,
        const std::vector<uint8_t>& b
    );
    static bool verify(
        const std::vector<uint8_t>& message,
        const std::vector<std::vector<uint8_t>>& ring,
        const RingSignature& sig
    );
    static bool isDoubleSpend(
        const std::vector<uint8_t>& keyImage,
        const std::vector<std::vector<uint8_t>>& usedKeyImages
    );
    static bool isDoubleSpend(const std::vector<uint8_t>& keyImage);
    static void recordKeyImage(const std::vector<uint8_t>& keyImage);
    static void loadKeyImages(const std::string& path);
    static void saveKeyImages(const std::string& path);
private:
    static std::vector<uint8_t> hashToPoint(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> hashToScalar(const std::vector<uint8_t>& data);
};

}
}
