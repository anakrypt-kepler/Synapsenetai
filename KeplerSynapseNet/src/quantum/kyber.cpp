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

struct Kyber::Impl {
    mutable std::mutex mtx;
};

Kyber::Kyber() : impl_(std::make_unique<Impl>()) {}
Kyber::~Kyber() = default;

KyberKeyPair Kyber::generateKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    KyberKeyPair kp;
#ifdef USE_LIBOQS
    OQS_KEM* kem = detail::newPreferredKyberKem();
    if (!detail::kyberKemPinned(kem)) {
        if (kem) OQS_KEM_free(kem);
        std::fprintf(stderr, "FATAL: Kyber/ML-KEM-768 algorithm unavailable in liboqs\n");
        std::abort();
    }
    if (OQS_KEM_keypair(kem, kp.publicKey.data(), kp.secretKey.data()) != OQS_SUCCESS) {
        OQS_KEM_free(kem);
        std::fprintf(stderr, "FATAL: Kyber key generation failed\n");
        std::abort();
    }
    OQS_KEM_free(kem);
#endif
    return kp;
}

EncapsulationResult Kyber::encapsulate(const KyberPublicKey& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    EncapsulationResult result;
#ifdef USE_LIBOQS
    OQS_KEM* kem = detail::newPreferredKyberKem();
    if (!detail::kyberKemPinned(kem)) {
        if (kem) OQS_KEM_free(kem);
        std::fprintf(stderr, "FATAL: Kyber/ML-KEM-768 algorithm unavailable in liboqs\n");
        std::abort();
    }
    if (publicKey.size() < KYBER_PUBLIC_KEY_SIZE) {
        OQS_KEM_free(kem);
        result.success = false;
        return result;
    }
    result.ciphertext.assign(KYBER_CIPHERTEXT_SIZE, 0);
    result.sharedSecret.assign(KYBER_SHARED_SECRET_SIZE, 0);
    if (OQS_KEM_encaps(kem, result.ciphertext.data(), result.sharedSecret.data(), publicKey.data()) != OQS_SUCCESS) {
        OQS_KEM_free(kem);
        std::fprintf(stderr, "FATAL: Kyber encapsulation failed\n");
        std::abort();
    }
    result.success = true;
    OQS_KEM_free(kem);
#else
    (void)publicKey;
    result.success = false;
#endif
    return result;
}

std::vector<uint8_t> Kyber::decapsulate(const KyberCiphertext& ciphertext,
                                         const KyberSecretKey& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
#ifdef USE_LIBOQS
    OQS_KEM* kem = detail::newPreferredKyberKem();
    if (!detail::kyberKemPinned(kem)) {
        if (kem) OQS_KEM_free(kem);
        std::fprintf(stderr, "FATAL: Kyber/ML-KEM-768 algorithm unavailable in liboqs\n");
        std::abort();
    }
    if (ciphertext.size() < KYBER_CIPHERTEXT_SIZE || secretKey.size() < KYBER_SECRET_KEY_SIZE) {
        OQS_KEM_free(kem);
        return {};
    }
    std::vector<uint8_t> ss(KYBER_SHARED_SECRET_SIZE);
    if (OQS_KEM_decaps(kem, ss.data(), ciphertext.data(), secretKey.data()) != OQS_SUCCESS) {
        OQS_KEM_free(kem);
        return {};
    }
    OQS_KEM_free(kem);
    return ss;
#else
    (void)ciphertext; (void)secretKey;
    return {};
#endif
}

bool Kyber::validatePublicKey(const KyberPublicKey& publicKey) {
    for (size_t i = 0; i < publicKey.size(); i++) {
        if (publicKey[i] != 0) return true;
    }
    return false;
}

bool Kyber::validateSecretKey(const KyberSecretKey& secretKey) {
    for (size_t i = 0; i < secretKey.size(); i++) {
        if (secretKey[i] != 0) return true;
    }
    return false;
}

std::vector<uint8_t> Kyber::serializePublicKey(const KyberPublicKey& key) {
    return std::vector<uint8_t>(key.begin(), key.end());
}

std::vector<uint8_t> Kyber::serializeSecretKey(const KyberSecretKey& key) {
    return std::vector<uint8_t>(key.begin(), key.end());
}

KyberPublicKey Kyber::deserializePublicKey(const std::vector<uint8_t>& data) {
    KyberPublicKey key{};
    size_t copyLen = std::min(data.size(), key.size());
    std::memcpy(key.data(), data.data(), copyLen);
    return key;
}

KyberSecretKey Kyber::deserializeSecretKey(const std::vector<uint8_t>& data) {
    KyberSecretKey key{};
    size_t copyLen = std::min(data.size(), key.size());
    std::memcpy(key.data(), data.data(), copyLen);
    return key;
}

}
}
