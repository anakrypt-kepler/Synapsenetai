#include "crypto/confidential_tx.h"
#include "crypto/crypto.h"
#include <sodium.h>
#include <cstring>
#include <string>

namespace synapse {
namespace crypto {

static void amountToScalar(uint64_t amount, unsigned char out[crypto_core_ed25519_SCALARBYTES]) {
    std::memset(out, 0, crypto_core_ed25519_SCALARBYTES);
    for (int i = 0; i < 8; i++) {
        out[i] = static_cast<unsigned char>((amount >> (i * 8)) & 0xff);
    }
}

std::vector<uint8_t> ConfidentialTx::getH() {
    const std::string seed = "SynapseNet_pedersen_H_v1";
    unsigned char hash[crypto_hash_sha512_BYTES];
    crypto_hash_sha512(hash, reinterpret_cast<const unsigned char*>(seed.data()), seed.size());
    unsigned char point[crypto_core_ed25519_BYTES];
    crypto_core_ed25519_from_hash(point, hash);
    return std::vector<uint8_t>(point, point + crypto_core_ed25519_BYTES);
}

std::vector<uint8_t> ConfidentialTx::generateBlindingFactor() {
    std::vector<uint8_t> r(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_random(r.data());
    return r;
}

PedersenCommitment ConfidentialTx::commit(uint64_t amount, const std::vector<uint8_t>& blindingFactor) {
    PedersenCommitment result;
    if (blindingFactor.size() != crypto_core_ed25519_SCALARBYTES) {
        return result;
    }

    unsigned char rG[crypto_core_ed25519_BYTES];
    if (crypto_scalarmult_ed25519_base_noclamp(rG, blindingFactor.data()) != 0) {
        return result;
    }

    std::vector<uint8_t> H = getH();

    unsigned char vScalar[crypto_core_ed25519_SCALARBYTES];
    amountToScalar(amount, vScalar);

    unsigned char vH[crypto_core_ed25519_BYTES];
    bool haveVH = false;
    if (amount != 0) {
        if (crypto_scalarmult_ed25519_noclamp(vH, vScalar, H.data()) != 0) {
            return result;
        }
        haveVH = true;
    }

    unsigned char C[crypto_core_ed25519_BYTES];
    if (haveVH) {
        if (crypto_core_ed25519_add(C, rG, vH) != 0) {
            return result;
        }
    } else {
        std::memcpy(C, rG, crypto_core_ed25519_BYTES);
    }

    result.commitment.assign(C, C + crypto_core_ed25519_BYTES);
    result.blinding = blindingFactor;
    return result;
}

PedersenCommitment ConfidentialTx::commit(uint64_t amount) {
    return commit(amount, generateBlindingFactor());
}

bool ConfidentialTx::verifyBalance(
    const std::vector<PedersenCommitment>& inputs,
    const std::vector<PedersenCommitment>& outputs,
    uint64_t fee
) {
    if (inputs.empty()) {
        return false;
    }

    unsigned char inSum[crypto_core_ed25519_BYTES];
    bool inInit = false;
    for (const auto& in : inputs) {
        if (in.commitment.size() != crypto_core_ed25519_BYTES) {
            return false;
        }
        if (!crypto_core_ed25519_is_valid_point(in.commitment.data())) {
            return false;
        }
        if (!inInit) {
            std::memcpy(inSum, in.commitment.data(), crypto_core_ed25519_BYTES);
            inInit = true;
        } else {
            if (crypto_core_ed25519_add(inSum, inSum, in.commitment.data()) != 0) {
                return false;
            }
        }
    }

    unsigned char outSum[crypto_core_ed25519_BYTES];
    bool outInit = false;
    for (const auto& out : outputs) {
        if (out.commitment.size() != crypto_core_ed25519_BYTES) {
            return false;
        }
        if (!crypto_core_ed25519_is_valid_point(out.commitment.data())) {
            return false;
        }
        if (!outInit) {
            std::memcpy(outSum, out.commitment.data(), crypto_core_ed25519_BYTES);
            outInit = true;
        } else {
            if (crypto_core_ed25519_add(outSum, outSum, out.commitment.data()) != 0) {
                return false;
            }
        }
    }

    if (fee != 0) {
        std::vector<uint8_t> H = getH();
        unsigned char feeScalar[crypto_core_ed25519_SCALARBYTES];
        amountToScalar(fee, feeScalar);
        unsigned char feeH[crypto_core_ed25519_BYTES];
        if (crypto_scalarmult_ed25519_noclamp(feeH, feeScalar, H.data()) != 0) {
            return false;
        }
        if (!outInit) {
            std::memcpy(outSum, feeH, crypto_core_ed25519_BYTES);
            outInit = true;
        } else {
            if (crypto_core_ed25519_add(outSum, outSum, feeH) != 0) {
                return false;
            }
        }
    }

    if (!outInit) {
        return false;
    }

    return sodium_memcmp(inSum, outSum, crypto_core_ed25519_BYTES) == 0;
}

std::vector<uint8_t> ConfidentialTx::blindingSum(
    const std::vector<std::vector<uint8_t>>& blindingFactors,
    bool negate
) {
    unsigned char acc[crypto_core_ed25519_SCALARBYTES];
    std::memset(acc, 0, crypto_core_ed25519_SCALARBYTES);

    for (const auto& bf : blindingFactors) {
        if (bf.size() != crypto_core_ed25519_SCALARBYTES) {
            return std::vector<uint8_t>();
        }
        if (negate) {
            unsigned char neg[crypto_core_ed25519_SCALARBYTES];
            crypto_core_ed25519_scalar_negate(neg, bf.data());
            crypto_core_ed25519_scalar_add(acc, acc, neg);
        } else {
            crypto_core_ed25519_scalar_add(acc, acc, bf.data());
        }
    }

    return std::vector<uint8_t>(acc, acc + crypto_core_ed25519_SCALARBYTES);
}

bool ConfidentialTx::rangeCheck(const PedersenCommitment& commitment) {
    if (commitment.commitment.size() != crypto_core_ed25519_BYTES) {
        return false;
    }
    return crypto_core_ed25519_is_valid_point(commitment.commitment.data());
}

bool PedersenCommitment::verify(uint64_t amount) const {
    if (commitment.size() != crypto_core_ed25519_BYTES) {
        return false;
    }
    if (blinding.size() != crypto_core_ed25519_SCALARBYTES) {
        return false;
    }
    PedersenCommitment recomputed = ConfidentialTx::commit(amount, blinding);
    if (recomputed.commitment.size() != crypto_core_ed25519_BYTES) {
        return false;
    }
    return sodium_memcmp(recomputed.commitment.data(), commitment.data(), crypto_core_ed25519_BYTES) == 0;
}

std::vector<uint8_t> PedersenCommitment::serialize() const {
    std::vector<uint8_t> out;
    uint32_t clen = static_cast<uint32_t>(commitment.size());
    uint32_t blen = static_cast<uint32_t>(blinding.size());
    for (int i = 0; i < 4; i++) out.push_back((clen >> (i * 8)) & 0xff);
    out.insert(out.end(), commitment.begin(), commitment.end());
    for (int i = 0; i < 4; i++) out.push_back((blen >> (i * 8)) & 0xff);
    out.insert(out.end(), blinding.begin(), blinding.end());
    return out;
}

PedersenCommitment PedersenCommitment::deserialize(const std::vector<uint8_t>& data) {
    PedersenCommitment result;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    if (static_cast<size_t>(end - p) < 4) return result;
    uint32_t clen = 0;
    for (int i = 0; i < 4; i++) clen |= static_cast<uint32_t>(p[i]) << (i * 8);
    p += 4;
    if (static_cast<size_t>(end - p) < clen) return result;
    result.commitment.assign(p, p + clen);
    p += clen;
    if (static_cast<size_t>(end - p) < 4) return result;
    uint32_t blen = 0;
    for (int i = 0; i < 4; i++) blen |= static_cast<uint32_t>(p[i]) << (i * 8);
    p += 4;
    if (static_cast<size_t>(end - p) < blen) return result;
    result.blinding.assign(p, p + blen);
    p += blen;
    return result;
}

}
}
