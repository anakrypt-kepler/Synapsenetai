#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <random>
#include <cstring>
#include <algorithm>
#include <sodium.h>
#include <openssl/evp.h>

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif
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
    return out;
}

std::vector<uint8_t> shake256Expand(const std::vector<uint8_t>& seed, size_t outputLen) {
    std::vector<uint8_t> out(outputLen);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    bool ok = true;
    if (1 != EVP_DigestInit_ex(ctx, EVP_shake256(), nullptr)) ok = false;
    if (ok && 1 != EVP_DigestUpdate(ctx, seed.data(), seed.size())) ok = false;
    if (ok && 1 != EVP_DigestFinalXOF(ctx, out.data(), outputLen)) ok = false;
    EVP_MD_CTX_free(ctx);
    if (!ok) return {};
    return out;
}

std::mutex& detRngMutex() {
    static std::mutex m;
    return m;
}

thread_local std::vector<uint8_t> tl_det_stream;
thread_local size_t tl_det_pos = 0;

void detRngCallback(uint8_t* out, size_t n) {
    while (n > 0) {
        if (tl_det_pos >= tl_det_stream.size()) {
            std::memset(out, 0, n);
            return;
        }
        const size_t take = std::min(n, tl_det_stream.size() - tl_det_pos);
        std::memcpy(out, tl_det_stream.data() + tl_det_pos, take);
        tl_det_pos += take;
        out += take;
        n -= take;
    }
}

}

struct HybridSig::Impl {
    mutable std::mutex mtx;
    CryptoAlgorithm classicAlgo = CryptoAlgorithm::CLASSIC_ED25519;
    CryptoAlgorithm pqcAlgo = CryptoAlgorithm::LATTICE_DILITHIUM65;
    Dilithium dilithium;
    bool sodiumReady = false;

    Impl() {
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

    return kp;
}

HybridKeyPair HybridSig::generateKeyPairFromSeed(const std::vector<uint8_t>& masterSeed) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    HybridKeyPair kp;
    kp.classicAlgo = impl_->classicAlgo;
    kp.pqcAlgo = impl_->pqcAlgo;

    if (masterSeed.size() < 16) return kp;

    const auto classicSeed = hkdfSha256Expand(masterSeed, "synapsenet-hybrid-classical-v1", crypto_sign_ed25519_SEEDBYTES);
    const auto pqcSeed = hkdfSha256Expand(masterSeed, "synapsenet-hybrid-pqc-v1", 32);
    if (classicSeed.size() != crypto_sign_ed25519_SEEDBYTES || pqcSeed.size() != 32) {
        return kp;
    }

    kp.classicPublicKey.resize(crypto_sign_ed25519_PUBLICKEYBYTES);
    kp.classicSecretKey.resize(crypto_sign_ed25519_SECRETKEYBYTES);
    if (crypto_sign_ed25519_seed_keypair(kp.classicPublicKey.data(), kp.classicSecretKey.data(),
                                          classicSeed.data()) != 0) {
        kp.classicPublicKey.clear();
        kp.classicSecretKey.clear();
        return kp;
    }

    {
#ifdef USE_LIBOQS
        std::lock_guard<std::mutex> rngLock(detRngMutex());
        const size_t streamLen = 64 * 1024;
        auto prevStream = std::move(tl_det_stream);
        const size_t prevPos = tl_det_pos;
        tl_det_stream = shake256Expand(pqcSeed, streamLen);
        tl_det_pos = 0;
        if (tl_det_stream.size() != streamLen) {
            tl_det_stream = std::move(prevStream);
            tl_det_pos = prevPos;
            return kp;
        }

        OQS_randombytes_custom_algorithm(detRngCallback);
        OQS_SIG* sig = detail::newPreferredDilithiumSig();
        if (sig) {
            kp.pqcPublicKey.assign(sig->length_public_key, 0);
            kp.pqcSecretKey.assign(sig->length_secret_key, 0);
            if (OQS_SIG_keypair(sig, kp.pqcPublicKey.data(), kp.pqcSecretKey.data()) != OQS_SUCCESS) {
                kp.pqcPublicKey.clear();
                kp.pqcSecretKey.clear();
            }
            OQS_SIG_free(sig);
        } else {
            kp.pqcPublicKey.clear();
            kp.pqcSecretKey.clear();
        }

        OQS_randombytes_switch_algorithm(OQS_RAND_alg_system);
        std::fill(tl_det_stream.begin(), tl_det_stream.end(), 0);
        tl_det_stream = std::move(prevStream);
        tl_det_pos = prevPos;
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
    if (!pqcKeyMaterialPresent(secretKey.pqcSecretKey)) {
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
    const size_t copyLen = std::min(secretKey.pqcSecretKey.size(), dilSk.size());
    std::memcpy(dilSk.data(), secretKey.pqcSecretKey.data(), copyLen);
    if (!impl_->dilithium.validateSecretKey(dilSk)) {
        return result;
    }
    auto pqcResult = impl_->dilithium.sign(message, dilSk);

    if (!pqcResult.success || pqcResult.signature.empty()) {
        return result;
    }

    // Wire bytes are classic || pqc. Use the Dilithium result length (liboqs
    // ML-DSA-65 may not match DILITHIUM_SIGNATURE_SIZE exactly).
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
    if (!pqcKeyMaterialPresent(publicKey.pqcPublicKey)) {
        return false;
    }

    // Need the 64-byte Ed25519 prefix. PQC half is the remaining bytes so a
    // liboqs ML-DSA-65 length that is not DILITHIUM_SIGNATURE_SIZE still AND-verifies.
    if (signature.size() < crypto_sign_ed25519_BYTES) {
        return false;
    }
    const size_t pqcLen = signature.size() - crypto_sign_ed25519_BYTES;
    if (pqcLen == 0) {
        return false;
    }

    if (crypto_sign_ed25519_verify_detached(signature.data(),
                                             message.data(), message.size(),
                                             publicKey.classicPublicKey.data()) != 0) {
        return false;
    }

    DilithiumPublicKey dilPk{};
    const size_t pkCopy = std::min(publicKey.pqcPublicKey.size(), dilPk.size());
    std::memcpy(dilPk.data(), publicKey.pqcPublicKey.data(), pkCopy);
    if (!impl_->dilithium.validatePublicKey(dilPk)) {
        return false;
    }

    DilithiumSignature dilSig{};
    const uint8_t* pqcSigStart = signature.data() + crypto_sign_ed25519_BYTES;
    const size_t sigCopy = std::min(pqcLen, dilSig.size());
    std::memcpy(dilSig.data(), pqcSigStart, sigCopy);

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
