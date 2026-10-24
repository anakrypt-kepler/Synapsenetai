#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include "pqc_backend_oqs.h"
#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif
#include <mutex>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace synapse {
namespace quantum {

struct Dilithium::Impl {
    mutable std::mutex mtx;
};

Dilithium::Dilithium() : impl_(std::make_unique<Impl>()) {}
Dilithium::~Dilithium() = default;

DilithiumKeyPair Dilithium::generateKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    DilithiumKeyPair kp;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredDilithiumSig();
    if (!sig) {
        std::fprintf(stderr, "FATAL: Dilithium/ML-DSA-65 algorithm unavailable in liboqs\n");
        std::abort();
    }
    // ML-DSA-65 only. A shorter parameter set must not be copied into the
    // fixed 1952/4032 buffers and later treated as a real key.
    if (sig->length_public_key != kp.publicKey.size() ||
        sig->length_secret_key != kp.secretKey.size() ||
        sig->length_signature != DILITHIUM_SIGNATURE_SIZE) {
        OQS_SIG_free(sig);
        return kp;
    }
    if (OQS_SIG_keypair(sig, kp.publicKey.data(), kp.secretKey.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        std::fprintf(stderr, "FATAL: Dilithium key generation failed\n");
        std::abort();
    }
    OQS_SIG_free(sig);
#endif
    return kp;
}

SignatureResult Dilithium::sign(const std::vector<uint8_t>& message,
                                 const DilithiumSecretKey& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    SignatureResult result;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredDilithiumSig();
    if (!sig) {
        std::fprintf(stderr, "FATAL: Dilithium/ML-DSA-65 algorithm unavailable in liboqs\n");
        std::abort();
    }
    if (sig->length_public_key != DILITHIUM_PUBLIC_KEY_SIZE ||
        sig->length_secret_key != DILITHIUM_SECRET_KEY_SIZE ||
        sig->length_signature != DILITHIUM_SIGNATURE_SIZE) {
        OQS_SIG_free(sig);
        result.success = false;
        return result;
    }
    std::vector<uint8_t> signed_msg(DILITHIUM_SIGNATURE_SIZE);
    size_t sig_len = 0;
    if (OQS_SIG_sign(sig, signed_msg.data(), &sig_len, message.data(), message.size(), secretKey.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        std::fprintf(stderr, "FATAL: Dilithium signing failed\n");
        std::abort();
    }
    OQS_SIG_free(sig);
    // Do not zero-pad a shorter signature up to the wire size. That padding
    // used to be stripped again in verify and accepted as ML-DSA-65.
    if (sig_len != DILITHIUM_SIGNATURE_SIZE) {
        result.success = false;
        return result;
    }
    result.signature = std::move(signed_msg);
    result.success = true;
    return result;
#else
    (void)message; (void)secretKey;
    result.success = false;
#endif
    return result;
}

bool Dilithium::verify(const std::vector<uint8_t>& message,
                       const DilithiumSignature& signature,
                       const DilithiumPublicKey& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!validatePublicKey(publicKey)) return false;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredDilithiumSig();
    if (!sig) {
        std::fprintf(stderr, "FATAL: Dilithium/ML-DSA-65 algorithm unavailable in liboqs\n");
        std::abort();
    }
    // Exact ML-DSA-65 lengths. Passing a shorter liboqs length used to drop
    // the tail of this buffer and accept a different parameter set.
    if (sig->length_public_key != publicKey.size() ||
        sig->length_secret_key != DILITHIUM_SECRET_KEY_SIZE ||
        sig->length_signature != signature.size()) {
        OQS_SIG_free(sig);
        return false;
    }
    bool ok = OQS_SIG_verify(sig, message.data(), message.size(),
                              signature.data(), signature.size(), publicKey.data()) == OQS_SUCCESS;
    OQS_SIG_free(sig);
    return ok;
#else
    (void)message; (void)signature;
    return false;
#endif
}

bool Dilithium::validatePublicKey(const DilithiumPublicKey& publicKey) {
    for (size_t i = 0; i < publicKey.size(); i++) {
        if (publicKey[i] != 0) return true;
    }
    return false;
}

bool Dilithium::validateSecretKey(const DilithiumSecretKey& secretKey) {
    for (size_t i = 0; i < secretKey.size(); i++) {
        if (secretKey[i] != 0) return true;
    }
    return false;
}

std::vector<uint8_t> Dilithium::serializePublicKey(const DilithiumPublicKey& key) {
    return std::vector<uint8_t>(key.begin(), key.end());
}

std::vector<uint8_t> Dilithium::serializeSecretKey(const DilithiumSecretKey& key) {
    return std::vector<uint8_t>(key.begin(), key.end());
}

DilithiumPublicKey Dilithium::deserializePublicKey(const std::vector<uint8_t>& data) {
    DilithiumPublicKey key{};
    size_t copyLen = std::min(data.size(), key.size());
    std::memcpy(key.data(), data.data(), copyLen);
    return key;
}

DilithiumSecretKey Dilithium::deserializeSecretKey(const std::vector<uint8_t>& data) {
    DilithiumSecretKey key{};
    size_t copyLen = std::min(data.size(), key.size());
    std::memcpy(key.data(), data.data(), copyLen);
    return key;
}

}
}
