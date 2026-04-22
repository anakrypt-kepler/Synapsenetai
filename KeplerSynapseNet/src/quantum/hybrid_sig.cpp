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

namespace synapse {
namespace quantum {

namespace {

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

    std::vector<uint8_t> classicSig(crypto_sign_ed25519_BYTES);
    unsigned long long classicSigLen = 0;
    if (crypto_sign_ed25519_detached(classicSig.data(), &classicSigLen,
                                      message.data(), message.size(),
                                      secretKey.classicSecretKey.data()) != 0) {
        result.success = false;
        return result;
    }
    classicSig.resize(classicSigLen);

    DilithiumSecretKey dilSk{};
    size_t copyLen = std::min(secretKey.pqcSecretKey.size(), dilSk.size());
    std::memcpy(dilSk.data(), secretKey.pqcSecretKey.data(), copyLen);
    auto pqcResult = impl_->dilithium.sign(message, dilSk);

    if (!pqcResult.success) {
        result.success = false;
        return result;
    }

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

    if (signature.size() < crypto_sign_ed25519_BYTES + DILITHIUM_SIGNATURE_SIZE) {
        return false;
    }

    if (crypto_sign_ed25519_verify_detached(signature.data(),
                                             message.data(), message.size(),
                                             publicKey.classicPublicKey.data()) != 0) {
        return false;
    }

    DilithiumPublicKey dilPk{};
    size_t copyLen = std::min(publicKey.pqcPublicKey.size(), dilPk.size());
    std::memcpy(dilPk.data(), publicKey.pqcPublicKey.data(), copyLen);

    DilithiumSignature dilSig{};
    const uint8_t* pqcSigStart = signature.data() + crypto_sign_ed25519_BYTES;
    size_t pqcSigLen = std::min(signature.size() - crypto_sign_ed25519_BYTES, dilSig.size());
    std::memcpy(dilSig.data(), pqcSigStart, pqcSigLen);

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
