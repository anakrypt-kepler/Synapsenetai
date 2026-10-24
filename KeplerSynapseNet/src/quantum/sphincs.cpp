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

namespace synapse {
namespace quantum {

struct Sphincs::Impl {
    mutable std::mutex mtx;
};

Sphincs::Sphincs() : impl_(std::make_unique<Impl>()) {}
Sphincs::~Sphincs() = default;

SphincsKeyPair Sphincs::generateKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    SphincsKeyPair kp;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredSphincsSig();
    if (!sig) {
        std::fprintf(stderr, "FATAL: SPHINCS+/SLH-DSA algorithm unavailable in liboqs\n");
        std::abort();
    }
    // SLH-DSA-SHA2-128s wire sizes only. Do not copy a shorter parameter set
    // into the fixed key buffers.
    if (sig->length_public_key != kp.publicKey.size() ||
        sig->length_secret_key != kp.secretKey.size() ||
        sig->length_signature != SPHINCS_SIGNATURE_SIZE) {
        OQS_SIG_free(sig);
        return kp;
    }
    if (OQS_SIG_keypair(sig, kp.publicKey.data(), kp.secretKey.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        std::fprintf(stderr, "FATAL: SPHINCS+ key generation failed\n");
        std::abort();
    }
    OQS_SIG_free(sig);
#endif
    return kp;
}

SignatureResult Sphincs::sign(const std::vector<uint8_t>& message,
                               const SphincsSecretKey& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    SignatureResult result;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredSphincsSig();
    if (!sig) {
        std::fprintf(stderr, "FATAL: SPHINCS+/SLH-DSA algorithm unavailable in liboqs\n");
        std::abort();
    }
    if (sig->length_public_key != SPHINCS_PUBLIC_KEY_SIZE ||
        sig->length_secret_key != SPHINCS_SECRET_KEY_SIZE ||
        sig->length_signature != SPHINCS_SIGNATURE_SIZE) {
        OQS_SIG_free(sig);
        result.success = false;
        return result;
    }
    std::vector<uint8_t> signature(SPHINCS_SIGNATURE_SIZE);
    size_t signatureLen = 0;
    if (OQS_SIG_sign(sig, signature.data(), &signatureLen, message.data(), message.size(), secretKey.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        std::fprintf(stderr, "FATAL: SPHINCS+ signing failed\n");
        std::abort();
    }
    OQS_SIG_free(sig);
    if (signatureLen != SPHINCS_SIGNATURE_SIZE) {
        result.success = false;
        return result;
    }
    result.signature = std::move(signature);
    result.success = true;
    return result;
#else
    (void)message; (void)secretKey;
    result.success = false;
#endif
    return result;
}

bool Sphincs::verify(const std::vector<uint8_t>& message,
                     const SphincsSignature& signature,
                     const SphincsPublicKey& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!validatePublicKey(publicKey)) return false;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredSphincsSig();
    if (!sig) {
        std::fprintf(stderr, "FATAL: SPHINCS+/SLH-DSA algorithm unavailable in liboqs\n");
        std::abort();
    }
    // Exact SLH-DSA-SHA2-128s lengths. A shorter signature must not verify
    // by passing only the algorithm's prefix of this buffer.
    if (sig->length_public_key != publicKey.size() ||
        sig->length_secret_key != SPHINCS_SECRET_KEY_SIZE ||
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
