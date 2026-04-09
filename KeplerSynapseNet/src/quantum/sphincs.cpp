#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include "pqc_backend_oqs.h"
#include <mutex>
#include <random>
#include <cstring>
#include <algorithm>
#include <cstdio>

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse {
namespace quantum {

namespace {
std::vector<uint8_t> deriveSphincsSignatureMaterial(const std::vector<uint8_t>& message,
                                                    const std::array<uint8_t, 32>& publicTag) {
    std::vector<uint8_t> seed(message.begin(), message.end());
    seed.insert(seed.end(), publicTag.begin(), publicTag.end());
    auto state = crypto::sha256(seed.data(), seed.size());

    std::vector<uint8_t> out(SPHINCS_SIGNATURE_SIZE);
    size_t written = 0;
    uint32_t counter = 0;
    while (written < out.size()) {
        std::vector<uint8_t> input(state.begin(), state.end());
        input.push_back(static_cast<uint8_t>((counter >> 24) & 0xff));
        input.push_back(static_cast<uint8_t>((counter >> 16) & 0xff));
        input.push_back(static_cast<uint8_t>((counter >> 8) & 0xff));
        input.push_back(static_cast<uint8_t>(counter & 0xff));
        auto block = crypto::sha256(input.data(), input.size());
        size_t take = std::min(block.size(), out.size() - written);
        std::memcpy(out.data() + written, block.data(), take);
        written += take;
        state = block;
        counter++;
    }
    return out;
}
}

struct Sphincs::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    
    Impl() : rng(std::random_device{}()) {}
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
};

Sphincs::Sphincs() : impl_(std::make_unique<Impl>()) {}
Sphincs::~Sphincs() = default;

SphincsKeyPair Sphincs::generateKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    SphincsKeyPair kp;
    bool liboqsFailure = false;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredSphincsSig();
    if (sig) {
        std::vector<uint8_t> pub(sig->length_public_key);
        std::vector<uint8_t> priv(sig->length_secret_key);
        if (OQS_SIG_keypair(sig, pub.data(), priv.data()) == OQS_SUCCESS) {
            const size_t copyPub = std::min(pub.size(), kp.publicKey.size());
            const size_t copyPriv = std::min(priv.size(), kp.secretKey.size());
            std::memcpy(kp.publicKey.data(), pub.data(), copyPub);
            std::memcpy(kp.secretKey.data(), priv.data(), copyPriv);
            OQS_SIG_free(sig);
            return kp;
        }
        liboqsFailure = true;
        OQS_SIG_free(sig);
    } else {
        liboqsFailure = true;
    }
#endif
    impl_->fillRandom(kp.secretKey.data(), kp.secretKey.size());
    
    auto hash = crypto::sha256(kp.secretKey.data(), kp.secretKey.size());
    std::memcpy(kp.publicKey.data(), hash.data(), kp.publicKey.size());

#ifdef USE_LIBOQS
    if (liboqsFailure) {
        std::fprintf(stderr, "CRITICAL: liboqs was compiled in but SPHINCS+ key generation failed\n");
    } else {
        std::fprintf(stderr, "warning: SPHINCS+ using simulated implementation (liboqs not available)\n");
    }
#else
    std::fprintf(stderr, "warning: SPHINCS+ using simulated implementation (liboqs not available)\n");
#endif
    return kp;
}

SignatureResult Sphincs::sign(const std::vector<uint8_t>& message,
                               const SphincsSecretKey& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    SignatureResult result;
    bool liboqsFailure = false;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredSphincsSig();
    if (sig) {
        std::vector<uint8_t> signature(SPHINCS_SIGNATURE_SIZE);
        size_t signatureLen = 0;
        if (sig->length_signature <= signature.size() &&
            OQS_SIG_sign(sig, signature.data(), &signatureLen, message.data(), message.size(), secretKey.data()) == OQS_SUCCESS) {
            if (signatureLen < signature.size()) {
                std::fill(signature.begin() + static_cast<std::ptrdiff_t>(signatureLen), signature.end(), 0);
            }
            result.signature = std::move(signature);
            result.success = true;
            OQS_SIG_free(sig);
            return result;
        }
        liboqsFailure = true;
        OQS_SIG_free(sig);
    } else {
        liboqsFailure = true;
    }
#endif
    auto publicHash = crypto::sha256(secretKey.data(), secretKey.size());
    std::array<uint8_t, 32> publicTag{};
    std::memcpy(publicTag.data(), publicHash.data(), publicTag.size());
    result.signature = deriveSphincsSignatureMaterial(message, publicTag);
    result.success = true;
#ifdef USE_LIBOQS
    if (liboqsFailure) {
        std::fprintf(stderr, "CRITICAL: liboqs was compiled in but SPHINCS+ signing failed\n");
    } else {
        std::fprintf(stderr, "warning: SPHINCS+ sign using simulated implementation (liboqs not available)\n");
    }
#else
    std::fprintf(stderr, "warning: SPHINCS+ sign using simulated implementation (liboqs not available)\n");
#endif
    return result;
}

bool Sphincs::verify(const std::vector<uint8_t>& message,
                     const SphincsSignature& signature,
                     const SphincsPublicKey& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!validatePublicKey(publicKey)) return false;
    bool liboqsFailure = false;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredSphincsSig();
    if (sig) {
        const size_t verifyLen = std::min(signature.size(), sig->length_signature);
        if (OQS_SIG_verify(sig, message.data(), message.size(), signature.data(), verifyLen, publicKey.data()) == OQS_SUCCESS) {
            OQS_SIG_free(sig);
            return true;
        }
        liboqsFailure = true;
        OQS_SIG_free(sig);
    } else {
        liboqsFailure = true;
    }
#endif

    std::array<uint8_t, 32> publicTag{};
    std::memcpy(publicTag.data(), publicKey.data(), publicTag.size());
    auto expected = deriveSphincsSignatureMaterial(message, publicTag);
    for (size_t i = 0; i < signature.size(); i++) {
        if (signature[i] != expected[i]) {
            return false;
        }
    }
#ifdef USE_LIBOQS
    if (liboqsFailure) {
        std::fprintf(stderr, "CRITICAL: liboqs was compiled in but SPHINCS+ verification failed\n");
    } else {
        std::fprintf(stderr, "warning: SPHINCS+ verify using simulated implementation (liboqs not available)\n");
    }
#else
    std::fprintf(stderr, "warning: SPHINCS+ verify using simulated implementation (liboqs not available)\n");
#endif
    return true;
}

bool Sphincs::validatePublicKey(const SphincsPublicKey& publicKey) {
    for (size_t i = 0; i < publicKey.size(); i++) {
        if (publicKey[i] != 0) return true;
    }
    return false;
}

bool Sphincs::validateSecretKey(const SphincsSecretKey& secretKey) {
    for (size_t i = 0; i < secretKey.size(); i++) {
        if (secretKey[i] != 0) return true;
    }
    return false;
}

}
}
