#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include "pqc_backend_oqs.h"
#include <oqs/oqs.h>
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
    OQS_KEM* kem = detail::newPreferredKyberKem();
    if (!kem) {
        std::fprintf(stderr, "FATAL: Kyber/ML-KEM-768 algorithm unavailable in liboqs\n");
        std::abort();
    }
    std::vector<uint8_t> pub(kem->length_public_key);
    std::vector<uint8_t> priv(kem->length_secret_key);
    if (OQS_KEM_keypair(kem, pub.data(), priv.data()) != OQS_SUCCESS) {
        OQS_KEM_free(kem);
        std::fprintf(stderr, "FATAL: Kyber key generation failed\n");
        std::abort();
    }
    size_t copyPub = std::min(pub.size(), kp.publicKey.size());
    size_t copyPriv = std::min(priv.size(), kp.secretKey.size());
    std::memcpy(kp.publicKey.data(), pub.data(), copyPub);
    std::memcpy(kp.secretKey.data(), priv.data(), copyPriv);
    OQS_KEM_free(kem);
    return kp;
}

EncapsulationResult Kyber::encapsulate(const KyberPublicKey& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    EncapsulationResult result;
    OQS_KEM* kem = detail::newPreferredKyberKem();
    if (!kem) {
        std::fprintf(stderr, "FATAL: Kyber/ML-KEM-768 algorithm unavailable in liboqs\n");
        std::abort();
    }
    if (publicKey.size() < kem->length_public_key) {
        OQS_KEM_free(kem);
        result.success = false;
        return result;
    }
    std::vector<uint8_t> ct(kem->length_ciphertext);
    std::vector<uint8_t> ss(kem->length_shared_secret);
    if (OQS_KEM_encaps(kem, ct.data(), ss.data(), publicKey.data()) != OQS_SUCCESS) {
        OQS_KEM_free(kem);
        std::fprintf(stderr, "FATAL: Kyber encapsulation failed\n");
        std::abort();
    }
    result.ciphertext = std::move(ct);
    result.sharedSecret = std::move(ss);
    result.success = true;
    OQS_KEM_free(kem);
    return result;
}

std::vector<uint8_t> Kyber::decapsulate(const KyberCiphertext& ciphertext,
                                         const KyberSecretKey& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    OQS_KEM* kem = detail::newPreferredKyberKem();
    if (!kem) {
        std::fprintf(stderr, "FATAL: Kyber/ML-KEM-768 algorithm unavailable in liboqs\n");
        std::abort();
    }
    if (ciphertext.size() < kem->length_ciphertext || secretKey.size() < kem->length_secret_key) {
        OQS_KEM_free(kem);
        return {};
    }
    std::vector<uint8_t> ss(kem->length_shared_secret);
    if (OQS_KEM_decaps(kem, ss.data(), ciphertext.data(), secretKey.data()) != OQS_SUCCESS) {
        OQS_KEM_free(kem);
        return {};
    }
    OQS_KEM_free(kem);
    return ss;
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
