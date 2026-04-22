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
    std::vector<uint8_t> pub(sig->length_public_key);
    std::vector<uint8_t> priv(sig->length_secret_key);
    if (OQS_SIG_keypair(sig, pub.data(), priv.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        std::fprintf(stderr, "FATAL: Dilithium key generation failed\n");
        std::abort();
    }
    size_t copyPub = std::min(pub.size(), kp.publicKey.size());
    size_t copyPriv = std::min(priv.size(), kp.secretKey.size());
    std::memcpy(kp.publicKey.data(), pub.data(), copyPub);
    std::memcpy(kp.secretKey.data(), priv.data(), copyPriv);
    OQS_SIG_free(sig);
#else
    std::fprintf(stderr, "FATAL: Dilithium requires liboqs (build with -DSYNAPSE_FETCH_LIBOQS=ON)\n");
    std::abort();
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
    std::vector<uint8_t> signed_msg(DILITHIUM_SIGNATURE_SIZE);
    size_t sig_len = 0;
    if (sig->length_signature <= signed_msg.size() &&
        OQS_SIG_sign(sig, signed_msg.data(), &sig_len, message.data(), message.size(), secretKey.data()) == OQS_SUCCESS) {
        if (sig_len < signed_msg.size()) {
            std::fill(signed_msg.begin() + static_cast<std::ptrdiff_t>(sig_len), signed_msg.end(), 0);
        }
        result.signature = std::move(signed_msg);
        result.success = true;
        OQS_SIG_free(sig);
        return result;
    }
    OQS_SIG_free(sig);
    std::fprintf(stderr, "FATAL: Dilithium signing failed\n");
    std::abort();
#else
    (void)message; (void)secretKey;
    std::fprintf(stderr, "FATAL: Dilithium requires liboqs\n");
    std::abort();
#endif
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
