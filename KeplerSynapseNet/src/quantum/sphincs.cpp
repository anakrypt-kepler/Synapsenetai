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
    std::vector<uint8_t> pub(sig->length_public_key);
    std::vector<uint8_t> priv(sig->length_secret_key);
    if (OQS_SIG_keypair(sig, pub.data(), priv.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        std::fprintf(stderr, "FATAL: SPHINCS+ key generation failed\n");
        std::abort();
    }
    const size_t copyPub = std::min(pub.size(), kp.publicKey.size());
    const size_t copyPriv = std::min(priv.size(), kp.secretKey.size());
    std::memcpy(kp.publicKey.data(), pub.data(), copyPub);
    std::memcpy(kp.secretKey.data(), priv.data(), copyPriv);
    OQS_SIG_free(sig);
#else
    std::fprintf(stderr, "FATAL: SPHINCS+ requires liboqs (build with -DSYNAPSE_FETCH_LIBOQS=ON)\n");
    std::abort();
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
    OQS_SIG_free(sig);
    std::fprintf(stderr, "FATAL: SPHINCS+ signing failed\n");
    std::abort();
#else
    (void)message; (void)secretKey;
    std::fprintf(stderr, "FATAL: SPHINCS+ requires liboqs\n");
    std::abort();
#endif
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
    const size_t verifyLen = std::min(signature.size(), sig->length_signature);
    bool ok = OQS_SIG_verify(sig, message.data(), message.size(),
                              signature.data(), verifyLen, publicKey.data()) == OQS_SUCCESS;
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
