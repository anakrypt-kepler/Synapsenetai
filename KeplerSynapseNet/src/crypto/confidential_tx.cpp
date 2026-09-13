// Confidential amounts: Pedersen C = vH + rG. getH() is derived from a fixed seed.

#include "crypto/confidential_tx.h"
#include "crypto/crypto.h"
#include "crypto/ring_signature.h"
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

namespace {

void writeU32le(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

uint32_t readU32le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::vector<uint8_t> hashToScalarLocal(const std::vector<uint8_t>& data) {
    unsigned char wide[crypto_core_ed25519_NONREDUCEDSCALARBYTES];
    crypto_hash_sha512(wide, data.data(), data.size());
    std::vector<uint8_t> s(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_reduce(s.data(), wide);
    return s;
}

void pow2Scalar(int bit, unsigned char out[crypto_core_ed25519_SCALARBYTES]) {
    std::memset(out, 0, crypto_core_ed25519_SCALARBYTES);
    out[bit / 8] = static_cast<unsigned char>(1u << (bit % 8));
}

} // namespace

std::vector<uint8_t> ConfidentialTx::proveRange(uint64_t amount, const std::vector<uint8_t>& blinding) {
    try {
    if (blinding.size() != crypto_core_ed25519_SCALARBYTES) {
        return {};
    }
    if (sodium_init() < 0) return {};

    std::vector<uint8_t> H = getH();
    PedersenCommitment parent = commit(amount, blinding);
    if (parent.commitment.size() != crypto_core_ed25519_BYTES) return {};

    std::vector<std::vector<uint8_t>> bitC(64);
    std::vector<uint8_t> accR(crypto_core_ed25519_SCALARBYTES, 0);
    std::vector<uint8_t> twoPowR(crypto_core_ed25519_SCALARBYTES);

    std::vector<uint8_t> out;
    writeU32le(out, 64);

    for (int i = 0; i < 64; ++i) {
        uint64_t bit = (amount >> i) & 1ULL;
        std::vector<uint8_t> ri = generateBlindingFactor();
        bitC[i] = commit(bit, ri).commitment;
        if (bitC[i].size() != crypto_core_ed25519_BYTES) return {};

        pow2Scalar(i, twoPowR.data());
        std::vector<uint8_t> weighted(crypto_core_ed25519_SCALARBYTES);
        crypto_core_ed25519_scalar_mul(weighted.data(), twoPowR.data(), ri.data());
        crypto_core_ed25519_scalar_add(accR.data(), accR.data(), weighted.data());

        std::vector<uint8_t> cMinusH(crypto_core_ed25519_BYTES);
        if (crypto_core_ed25519_sub(cMinusH.data(), bitC[i].data(), H.data()) != 0) return {};

        std::vector<std::vector<uint8_t>> ring;
        ring.push_back(bitC[i]);
        ring.push_back(cMinusH);
        size_t idx = (bit == 0) ? 0 : 1;
        std::vector<uint8_t> msg = parent.commitment;
        msg.push_back(static_cast<uint8_t>(i));
        RingSignature bp = RingSign::sign(msg, ring, ri, idx, false);
        std::vector<uint8_t> sig = bp.serialize();

        out.insert(out.end(), bitC[i].begin(), bitC[i].end());
        writeU32le(out, static_cast<uint32_t>(sig.size()));
        out.insert(out.end(), sig.begin(), sig.end());
        sodium_memzero(ri.data(), ri.size());
    }

    std::vector<uint8_t> delta(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_sub(delta.data(), blinding.data(), accR.data());

    std::vector<uint8_t> k = generateBlindingFactor();
    std::vector<uint8_t> R(crypto_core_ed25519_BYTES);
    if (crypto_scalarmult_ed25519_base_noclamp(R.data(), k.data()) != 0) return {};

    std::vector<uint8_t> cdata = parent.commitment;
    cdata.insert(cdata.end(), R.begin(), R.end());
    const char domain[] = "SynapseNet_range_delta_v1";
    cdata.insert(cdata.end(), domain, domain + sizeof(domain) - 1);
    std::vector<uint8_t> c = hashToScalarLocal(cdata);
    std::vector<uint8_t> cd(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_mul(cd.data(), c.data(), delta.data());
    std::vector<uint8_t> s(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_sub(s.data(), k.data(), cd.data());

    out.insert(out.end(), R.begin(), R.end());
    out.insert(out.end(), s.begin(), s.end());
    sodium_memzero(delta.data(), delta.size());
    sodium_memzero(k.data(), k.size());
    return out;
    } catch (...) {
        return {};
    }
}

bool ConfidentialTx::verifyRange(const std::vector<uint8_t>& commitment, const std::vector<uint8_t>& proof) {
    try {
    if (commitment.size() != crypto_core_ed25519_BYTES) return false;
    if (proof.size() < 4) return false;
    if (sodium_init() < 0) return false;
    const uint8_t* p = proof.data();
    const uint8_t* end = proof.data() + proof.size();
    if (static_cast<size_t>(end - p) < 4) return false;
    uint32_t nbits = readU32le(p);
    p += 4;
    if (nbits != 64) return false;

    std::vector<uint8_t> H = getH();
    unsigned char sum[crypto_core_ed25519_BYTES];
    bool sumInit = false;

    for (uint32_t i = 0; i < nbits; ++i) {
        if (static_cast<size_t>(end - p) < crypto_core_ed25519_BYTES + 4) return false;
        std::vector<uint8_t> Ci(p, p + crypto_core_ed25519_BYTES);
        p += crypto_core_ed25519_BYTES;
        if (crypto_core_ed25519_is_valid_point(Ci.data()) != 1) return false;
        uint32_t slen = readU32le(p);
        p += 4;
        if (slen > 4096 || static_cast<size_t>(end - p) < slen) return false;
        std::vector<uint8_t> sigBytes(p, p + slen);
        p += slen;

        std::vector<uint8_t> cMinusH(crypto_core_ed25519_BYTES);
        if (crypto_core_ed25519_sub(cMinusH.data(), Ci.data(), H.data()) != 0) return false;
        std::vector<std::vector<uint8_t>> ring;
        ring.push_back(Ci);
        ring.push_back(cMinusH);
        RingSignature bp = RingSignature::deserialize(sigBytes);
        std::vector<uint8_t> msg = commitment;
        msg.push_back(static_cast<uint8_t>(i));
        if (!RingSign::verify(msg, ring, bp)) return false;

        unsigned char twoPow[crypto_core_ed25519_SCALARBYTES];
        pow2Scalar(static_cast<int>(i), twoPow);
        unsigned char weighted[crypto_core_ed25519_BYTES];
        if (crypto_scalarmult_ed25519_noclamp(weighted, twoPow, Ci.data()) != 0) return false;
        if (!sumInit) {
            std::memcpy(sum, weighted, crypto_core_ed25519_BYTES);
            sumInit = true;
        } else {
            if (crypto_core_ed25519_add(sum, sum, weighted) != 0) return false;
        }
    }
    if (!sumInit) return false;
    if (static_cast<size_t>(end - p) < 64) return false;
    std::vector<uint8_t> R(p, p + 32);
    p += 32;
    std::vector<uint8_t> s(p, p + 32);
    p += 32;
    if (p != end) return false;
    if (crypto_core_ed25519_is_valid_point(R.data()) != 1) return false;

    unsigned char D[crypto_core_ed25519_BYTES];
    if (crypto_core_ed25519_sub(D, commitment.data(), sum) != 0) return false;

    std::vector<uint8_t> cdata(commitment.begin(), commitment.end());
    cdata.insert(cdata.end(), R.begin(), R.end());
    const char domain[] = "SynapseNet_range_delta_v1";
    cdata.insert(cdata.end(), domain, domain + sizeof(domain) - 1);
    std::vector<uint8_t> c = hashToScalarLocal(cdata);

    unsigned char sG[crypto_core_ed25519_BYTES];
    unsigned char cD[crypto_core_ed25519_BYTES];
    if (crypto_scalarmult_ed25519_base_noclamp(sG, s.data()) != 0) return false;
    if (crypto_scalarmult_ed25519_noclamp(cD, c.data(), D) != 0) {
        // D may be identity if delta=0; treat as R == sG
        return sodium_memcmp(sG, R.data(), crypto_core_ed25519_BYTES) == 0;
    }
    unsigned char rhs[crypto_core_ed25519_BYTES];
    if (crypto_core_ed25519_add(rhs, sG, cD) != 0) return false;
    return sodium_memcmp(rhs, R.data(), crypto_core_ed25519_BYTES) == 0;
    } catch (...) {
        return false;
    }
}

std::vector<uint8_t> ConfidentialTx::proveKnownAmount(
    const std::vector<uint8_t>& commitment,
    uint64_t amount,
    const std::vector<uint8_t>& blinding
) {
    if (commitment.size() != crypto_core_ed25519_BYTES) return {};
    if (blinding.size() != crypto_core_ed25519_SCALARBYTES) return {};
    if (sodium_init() < 0) return {};

    unsigned char D[crypto_core_ed25519_BYTES];
    if (amount == 0) {
        std::memcpy(D, commitment.data(), crypto_core_ed25519_BYTES);
    } else {
        std::vector<uint8_t> H = getH();
        unsigned char vScalar[crypto_core_ed25519_SCALARBYTES];
        amountToScalar(amount, vScalar);
        unsigned char vH[crypto_core_ed25519_BYTES];
        if (crypto_scalarmult_ed25519_noclamp(vH, vScalar, H.data()) != 0) return {};
        if (crypto_core_ed25519_sub(D, commitment.data(), vH) != 0) return {};
    }

    std::vector<uint8_t> k = generateBlindingFactor();
    std::vector<uint8_t> R(crypto_core_ed25519_BYTES);
    if (crypto_scalarmult_ed25519_base_noclamp(R.data(), k.data()) != 0) return {};

    std::vector<uint8_t> cdata(commitment.begin(), commitment.end());
    cdata.insert(cdata.end(), R.begin(), R.end());
    const char domain[] = "SynapseNet_known_amount_v1";
    cdata.insert(cdata.end(), domain, domain + sizeof(domain) - 1);
    for (int i = 0; i < 8; ++i) {
        cdata.push_back(static_cast<uint8_t>((amount >> (i * 8)) & 0xff));
    }
    std::vector<uint8_t> c = hashToScalarLocal(cdata);
    std::vector<uint8_t> cr(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_mul(cr.data(), c.data(), blinding.data());
    std::vector<uint8_t> s(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_sub(s.data(), k.data(), cr.data());

    std::vector<uint8_t> out;
    out.insert(out.end(), R.begin(), R.end());
    out.insert(out.end(), s.begin(), s.end());
    sodium_memzero(k.data(), k.size());
    return out;
}

bool ConfidentialTx::verifyKnownAmount(
    const std::vector<uint8_t>& commitment,
    uint64_t amount,
    const std::vector<uint8_t>& proof
) {
    if (commitment.size() != crypto_core_ed25519_BYTES) return false;
    if (proof.size() != 64) return false;
    if (sodium_init() < 0) return false;

    unsigned char D[crypto_core_ed25519_BYTES];
    if (amount == 0) {
        std::memcpy(D, commitment.data(), crypto_core_ed25519_BYTES);
    } else {
        std::vector<uint8_t> H = getH();
        unsigned char vScalar[crypto_core_ed25519_SCALARBYTES];
        amountToScalar(amount, vScalar);
        unsigned char vH[crypto_core_ed25519_BYTES];
        if (crypto_scalarmult_ed25519_noclamp(vH, vScalar, H.data()) != 0) return false;
        if (crypto_core_ed25519_sub(D, commitment.data(), vH) != 0) return false;
    }

    std::vector<uint8_t> R(proof.begin(), proof.begin() + 32);
    std::vector<uint8_t> s(proof.begin() + 32, proof.begin() + 64);
    if (crypto_core_ed25519_is_valid_point(R.data()) != 1) return false;

    std::vector<uint8_t> cdata(commitment.begin(), commitment.end());
    cdata.insert(cdata.end(), R.begin(), R.end());
    const char domain[] = "SynapseNet_known_amount_v1";
    cdata.insert(cdata.end(), domain, domain + sizeof(domain) - 1);
    for (int i = 0; i < 8; ++i) {
        cdata.push_back(static_cast<uint8_t>((amount >> (i * 8)) & 0xff));
    }
    std::vector<uint8_t> c = hashToScalarLocal(cdata);

    unsigned char sG[crypto_core_ed25519_BYTES];
    unsigned char cD[crypto_core_ed25519_BYTES];
    if (crypto_scalarmult_ed25519_base_noclamp(sG, s.data()) != 0) return false;
    if (crypto_scalarmult_ed25519_noclamp(cD, c.data(), D) != 0) {
        return sodium_memcmp(sG, R.data(), crypto_core_ed25519_BYTES) == 0;
    }
    unsigned char rhs[crypto_core_ed25519_BYTES];
    if (crypto_core_ed25519_add(rhs, sG, cD) != 0) return false;
    return sodium_memcmp(rhs, R.data(), crypto_core_ed25519_BYTES) == 0;
}

}
}
