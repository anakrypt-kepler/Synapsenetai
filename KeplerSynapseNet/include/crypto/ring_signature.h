#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

#include "crypto/crypto.h"

namespace synapse {
namespace crypto {

struct RingSignature {
    std::vector<uint8_t> keyImage;
    std::vector<uint8_t> c0;
    std::vector<std::vector<uint8_t>> responses;
    std::vector<uint8_t> serialize() const;
    static RingSignature deserialize(const std::vector<uint8_t>& data);
};

class RingSign {
public:
    RingSign();
    ~RingSign();
    static RingSignature sign(
        const std::vector<uint8_t>& message,
        const std::vector<std::vector<uint8_t>>& ring,
        const std::vector<uint8_t>& privateKey,
        size_t signerIndex
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
private:
    static std::vector<uint8_t> hashToPoint(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> hashToScalar(const std::vector<uint8_t>& data);
};

}
}
