#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <random>
#include <cstring>
#include <algorithm>
#include <sodium.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <cstdlib>

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif
#include "oqs_rng_scope.h"
#include "pqc_backend_oqs.h"

// AND-mode HybridSig: Ed25519 AND ML-DSA-65 (FIPS 204). Both halves must
// verify. Parameter set is ML-DSA-65 only (not ML-DSA-44/87, not Dilithium2/5).
// liboqs may still name the algorithm Dilithium3; wire sizes are 1952/4032/3309.
// Signs the raw message; application envelopes already domain-separate via
// buildApplicationSignatureTranscript ("synapsenet-application-signature-v1",
// suite "ed25519+ml-dsa-65"). This is not IETF Composite ML-DSA (X.509).
// MLSAG/RingCT/stealth spends stay classical. Handshake/NODE_MSG stay
// Kyber+X25519: handshake.cpp has no Dilithium trailer (do not invent one).

namespace synapse {
namespace quantum {

namespace {

bool pqcKeyMaterialPresent(const std::vector<uint8_t>& key) {
    if (key.empty()) return false;
    for (uint8_t b : key) {
        if (b != 0) return true;
    }
    return false;
}

std::vector<uint8_t> hkdfSha256Expand(const std::vector<uint8_t>& ikm,
                                       const std::string& info,
                                       size_t outputLen) {
    const std::vector<uint8_t> salt(32, 0x00);
    auto prk = crypto::hmacSha256(salt, ikm);
    std::vector<uint8_t> out;
    out.reserve(outputLen);
    std::vector<uint8_t> block;
    uint8_t counter = 1;
    while (out.size() < outputLen) {
        std::vector<uint8_t> msg;
        msg.reserve(block.size() + info.size() + 1);
        msg.insert(msg.end(), block.begin(), block.end());
        msg.insert(msg.end(), info.begin(), info.end());
        msg.push_back(counter);
        block = crypto::hmacSha256(prk, msg);
        const size_t take = std::min(block.size(), outputLen - out.size());
        out.insert(out.end(), block.begin(), block.begin() + take);
        if (counter == 0xff) break;
        counter = static_cast<uint8_t>(counter + 1);
    }
    sodium_memzero(prk.data(), prk.size());
    if (!block.empty()) sodium_memzero(block.data(), block.size());
    return out;
}

std::vector<uint8_t> shake256Expand(const std::vector<uint8_t>& seed, size_t outputLen) {
    crypto::containForeignCryptoModules();
    std::vector<uint8_t> out(outputLen);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    bool ok = true;
    if (1 != EVP_DigestInit_ex(ctx, EVP_shake256(), nullptr)) ok = false;
    if (ok && 1 != EVP_DigestUpdate(ctx, seed.data(), seed.size())) ok = false;
    if (ok && 1 != EVP_DigestFinalXOF(ctx, out.data(), outputLen)) ok = false;
    EVP_MD_CTX_free(ctx);
    if (!ok) {
        OPENSSL_cleanse(out.data(), out.size());
        return {};
    }
    return out;
}

}

struct HybridSig::Impl {
    mutable std::mutex mtx;
    CryptoAlgorithm classicAlgo = CryptoAlgorithm::CLASSIC_ED25519;
    CryptoAlgorithm pqcAlgo = CryptoAlgorithm::LATTICE_DILITHIUM65;
    Dilithium dilithium;
    bool sodiumReady = false;

    Impl() {
        crypto::containForeignCryptoModules();
        sodiumReady = (sodium_init() >= 0);
    }
};

HybridSig::HybridSig() : impl_(std::make_unique<Impl>()) {}
HybridSig::~HybridSig() = default;

HybridKeyPair HybridSig::generateKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    HybridKeyPair kp;
    kp.classicAlgo = impl_->classicAlgo;
    kp.pqcAlgo = impl_->pqcAlgo;

    kp.classicPublicKey.resize(crypto_sign_ed25519_PUBLICKEYBYTES);
    kp.classicSecretKey.resize(crypto_sign_ed25519_SECRETKEYBYTES);
    crypto_sign_ed25519_keypair(kp.classicPublicKey.data(), kp.classicSecretKey.data());

    auto dilithiumKp = impl_->dilithium.generateKeyPair();
    kp.pqcPublicKey.assign(dilithiumKp.publicKey.begin(), dilithiumKp.publicKey.end());
    kp.pqcSecretKey.assign(dilithiumKp.secretKey.begin(), dilithiumKp.secretKey.end());
    sodium_memzero(dilithiumKp.secretKey.data(), dilithiumKp.secretKey.size());

    return kp;
}

HybridKeyPair HybridSig::generateKeyPairFromSeed(const std::vector<uint8_t>& masterSeed) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    HybridKeyPair kp;
    kp.classicAlgo = impl_->classicAlgo;
    kp.pqcAlgo = impl_->pqcAlgo;

    if (masterSeed.size() < 16) return kp;

    auto classicSeed = hkdfSha256Expand(masterSeed, "synapsenet-hybrid-classical-v1", crypto_sign_ed25519_SEEDBYTES);
    auto pqcSeed = hkdfSha256Expand(masterSeed, "synapsenet-hybrid-pqc-v1", 32);
    struct WipeSeeds {
        std::vector<uint8_t>& classicSeed;
        std::vector<uint8_t>& pqcSeed;
        ~WipeSeeds() {
            if (!classicSeed.empty()) sodium_memzero(classicSeed.data(), classicSeed.size());
            if (!pqcSeed.empty()) sodium_memzero(pqcSeed.data(), pqcSeed.size());
        }
    } wipeSeeds{classicSeed, pqcSeed};
    if (classicSeed.size() != crypto_sign_ed25519_SEEDBYTES || pqcSeed.size() != 32) {
        return kp;
    }

    kp.classicPublicKey.resize(crypto_sign_ed25519_PUBLICKEYBYTES);
    kp.classicSecretKey.resize(crypto_sign_ed25519_SECRETKEYBYTES);
    if (crypto_sign_ed25519_seed_keypair(kp.classicPublicKey.data(), kp.classicSecretKey.data(),
                                          classicSeed.data()) != 0) {
        if (!kp.classicSecretKey.empty()) sodium_memzero(kp.classicSecretKey.data(), kp.classicSecretKey.size());
        kp.classicPublicKey.clear();
        kp.classicSecretKey.clear();
        return kp;
    }

    {
#ifdef USE_LIBOQS
        const size_t streamLen = 64 * 1024;
        auto stream = shake256Expand(pqcSeed, streamLen);
        if (stream.size() != streamLen) {
            if (!stream.empty()) OPENSSL_cleanse(stream.data(), stream.size());
            return kp;
        }

        {
            detail::OqsDeterministicRngGuard rng(std::move(stream));
            OQS_SIG* sig = detail::newPreferredDilithiumSig();
            const bool mlDsa65 = detail::dilithiumSigPinned(sig);
            if (mlDsa65) {
                kp.pqcPublicKey.assign(DILITHIUM_PUBLIC_KEY_SIZE, 0);
                kp.pqcSecretKey.assign(DILITHIUM_SECRET_KEY_SIZE, 0);
                if (OQS_SIG_keypair(sig, kp.pqcPublicKey.data(), kp.pqcSecretKey.data()) != OQS_SUCCESS
                    || !rng.accepted(1)) {
                    OPENSSL_cleanse(kp.pqcPublicKey.data(), kp.pqcPublicKey.size());
                    OPENSSL_cleanse(kp.pqcSecretKey.data(), kp.pqcSecretKey.size());
                    kp.pqcPublicKey.clear();
                    kp.pqcSecretKey.clear();
                }
            } else {
                kp.pqcPublicKey.clear();
                kp.pqcSecretKey.clear();
            }
            if (sig) OQS_SIG_free(sig);
        }
#else
        (void)pqcSeed;
#endif
    }

    return kp;
}

SignatureResult HybridSig::sign(const std::vector<uint8_t>& message,
                                 const HybridKeyPair& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    SignatureResult result;
    result.success = false;

    // Fail closed: empty/zero PQC SK used to memcpy nothing into a zero
    // Dilithium buffer and still produce a classic-only "success".
    if (secretKey.classicSecretKey.size() != crypto_sign_ed25519_SECRETKEYBYTES) {
        return result;
    }
    if (secretKey.pqcSecretKey.size() != DILITHIUM_SECRET_KEY_SIZE ||
        !pqcKeyMaterialPresent(secretKey.pqcSecretKey)) {
        return result;
    }

    std::vector<uint8_t> classicSig(crypto_sign_ed25519_BYTES);
    unsigned long long classicSigLen = 0;
    if (crypto_sign_ed25519_detached(classicSig.data(), &classicSigLen,
                                      message.data(), message.size(),
                                      secretKey.classicSecretKey.data()) != 0) {
        return result;
    }
    classicSig.resize(classicSigLen);
    if (classicSig.size() != crypto_sign_ed25519_BYTES) {
        return result;
    }

    DilithiumSecretKey dilSk{};
    std::memcpy(dilSk.data(), secretKey.pqcSecretKey.data(), dilSk.size());
    if (!impl_->dilithium.validateSecretKey(dilSk)) {
        sodium_memzero(dilSk.data(), dilSk.size());
        return result;
    }
    auto pqcResult = impl_->dilithium.sign(message, dilSk);
    sodium_memzero(dilSk.data(), dilSk.size());

    if (!pqcResult.success || pqcResult.signature.size() != DILITHIUM_SIGNATURE_SIZE) {
        return result;
    }

    // Wire bytes are classic || pqc, and the PQC half is ML-DSA-65 (3309 bytes).
    result.signature.clear();
    result.signature.insert(result.signature.end(), classicSig.begin(), classicSig.end());
    result.signature.insert(result.signature.end(), pqcResult.signature.begin(), pqcResult.signature.end());
    result.success = true;
    return result;
}

bool HybridSig::verify(const std::vector<uint8_t>& message,
                        const std::vector<uint8_t>& signature,
                        const HybridKeyPair& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    // Fail closed: missing PQC PK used to memcpy nothing into a zero buffer.
    if (publicKey.classicPublicKey.size() != crypto_sign_ed25519_PUBLICKEYBYTES) {
        return false;
    }
    if (publicKey.pqcPublicKey.size() != DILITHIUM_PUBLIC_KEY_SIZE ||
        !pqcKeyMaterialPresent(publicKey.pqcPublicKey)) {
        return false;
    }

    // Exact sizes: 64-byte Ed25519 and 3309-byte ML-DSA-65. A missing, short,
    // or other lattice parameter set fails closed.
    if (signature.size() != crypto_sign_ed25519_BYTES + DILITHIUM_SIGNATURE_SIZE) {
        return false;
    }
    const size_t pqcLen = DILITHIUM_SIGNATURE_SIZE;

    if (crypto_sign_ed25519_verify_detached(signature.data(),
                                             message.data(), message.size(),
                                             publicKey.classicPublicKey.data()) != 0) {
        return false;
    }

    DilithiumPublicKey dilPk{};
    std::memcpy(dilPk.data(), publicKey.pqcPublicKey.data(), dilPk.size());
    if (!impl_->dilithium.validatePublicKey(dilPk)) {
        return false;
    }

    DilithiumSignature dilSig{};
    const uint8_t* pqcSigStart = signature.data() + crypto_sign_ed25519_BYTES;
    std::memcpy(dilSig.data(), pqcSigStart, pqcLen);

    // AND-mode: classic already verified; Dilithium must also verify.
    return impl_->dilithium.verify(message, dilSig, dilPk);
}

void HybridSig::setClassicAlgorithm(CryptoAlgorithm algo) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->classicAlgo = algo;
}

void HybridSig::setPQCAlgorithm(CryptoAlgorithm algo) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->pqcAlgo = algo;
}

}
}
