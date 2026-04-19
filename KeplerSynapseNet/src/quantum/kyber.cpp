#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include "pqc_backend_oqs.h"
#include <mutex>
#include <random>
#include <cstring>
#include <cstdio>
#include <openssl/rand.h>

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse {
namespace quantum {

namespace {

constexpr size_t KYBER_SIM_PUBLIC_IMPRINT_SIZE = 32;
constexpr char KYBER_SIM_KDF_LABEL[] = "kyber-sim-shared-secret-v1";

std::array<uint8_t, KYBER_SIM_PUBLIC_IMPRINT_SIZE> simulatedPublicImprintFromPublicKey(
    const KyberPublicKey& publicKey) {
    std::array<uint8_t, KYBER_SIM_PUBLIC_IMPRINT_SIZE> imprint{};
    std::memcpy(imprint.data(), publicKey.data(), imprint.size());
    return imprint;
}

std::array<uint8_t, KYBER_SIM_PUBLIC_IMPRINT_SIZE> simulatedPublicImprintFromSecretKey(
    const KyberSecretKey& secretKey) {
    std::array<uint8_t, KYBER_SIM_PUBLIC_IMPRINT_SIZE> imprint{};
    auto hash = crypto::sha256(secretKey.data(), secretKey.size());
    std::memcpy(imprint.data(), hash.data(), imprint.size());
    return imprint;
}

std::vector<uint8_t> deriveSimulatedSharedSecret(
    const std::array<uint8_t, KYBER_SIM_PUBLIC_IMPRINT_SIZE>& publicImprint,
    const uint8_t* ciphertext,
    size_t ciphertextSize) {
    std::vector<uint8_t> kdfInput;
    kdfInput.reserve((sizeof(KYBER_SIM_KDF_LABEL) - 1) + publicImprint.size() + ciphertextSize);
    kdfInput.insert(kdfInput.end(),
                    KYBER_SIM_KDF_LABEL,
                    KYBER_SIM_KDF_LABEL + (sizeof(KYBER_SIM_KDF_LABEL) - 1));
    kdfInput.insert(kdfInput.end(), publicImprint.begin(), publicImprint.end());
    kdfInput.insert(kdfInput.end(), ciphertext, ciphertext + ciphertextSize);

    auto hash = crypto::sha256(kdfInput.data(), kdfInput.size());
    return std::vector<uint8_t>(hash.begin(), hash.begin() + KYBER_SHARED_SECRET_SIZE);
}

} // namespace

struct Kyber::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    
        Impl() : rng(0) {
    #ifdef USE_LIBOQS
        std::random_device rd;
        rng.seed(rd());
    #else
        // nondeterministic seeding even when liboqs is not available
        uint64_t seedVal = 0;
        if (RAND_bytes(reinterpret_cast<unsigned char*>(&seedVal), sizeof(seedVal)) == 1) {
            rng.seed(seedVal);
        } else {
            std::random_device rd;
            rng.seed(rd());
        }
    #endif
        }
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
};

Kyber::Kyber() : impl_(std::make_unique<Impl>()) {}
Kyber::~Kyber() = default;

KyberKeyPair Kyber::generateKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    KyberKeyPair kp;
    bool liboqsFailure = false;
#ifdef USE_LIBOQS
    OQS_KEM* kem = detail::newPreferredKyberKem();
    if (kem) {
        std::vector<uint8_t> pub(kem->length_public_key);
        std::vector<uint8_t> priv(kem->length_secret_key);
        if (OQS_KEM_keypair(kem, pub.data(), priv.data()) == OQS_SUCCESS) {
            size_t copyPub = std::min(pub.size(), kp.publicKey.size());
            size_t copyPriv = std::min(priv.size(), kp.secretKey.size());
            std::memcpy(kp.publicKey.data(), pub.data(), copyPub);
            std::memcpy(kp.secretKey.data(), priv.data(), copyPriv);
            OQS_KEM_free(kem);
            return kp;
        }
        liboqsFailure = true;
        OQS_KEM_free(kem);
    } else {
        liboqsFailure = true;
    }
#endif

    impl_->fillRandom(kp.secretKey.data(), kp.secretKey.size());

    // derive the public imprint deterministically from the full secret key
    auto hash = crypto::sha256(kp.secretKey.data(), kp.secretKey.size());
    std::memcpy(kp.publicKey.data(), hash.data(), 32);
    impl_->fillRandom(kp.publicKey.data() + 32, kp.publicKey.size() - 32);

#ifdef USE_LIBOQS
    if (liboqsFailure) {
        std::fprintf(stderr, "CRITICAL: liboqs was compiled in but Kyber key generation failed\n");
    } else {
        std::fprintf(stderr, "warning: Kyber using simulated implementation (liboqs not available)\n");
    }
#else
    std::fprintf(stderr, "warning: Kyber using simulated implementation (liboqs not available)\n");
#endif
    return kp;
}

EncapsulationResult Kyber::encapsulate(const KyberPublicKey& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    EncapsulationResult result;
    result.success = true;
    result.ciphertext.resize(KYBER_CIPHERTEXT_SIZE);
    result.sharedSecret.resize(KYBER_SHARED_SECRET_SIZE);
    bool liboqsFailure = false;
#ifdef USE_LIBOQS
    OQS_KEM* kem = detail::newPreferredKyberKem();
    if (kem) {
        if (publicKey.size() >= kem->length_public_key) {
            std::vector<uint8_t> ct(kem->length_ciphertext);
            std::vector<uint8_t> ss(kem->length_shared_secret);
            if (OQS_KEM_encaps(kem, ct.data(), ss.data(), publicKey.data()) == OQS_SUCCESS) {
                result.ciphertext.assign(ct.begin(), ct.end());
                result.sharedSecret.assign(ss.begin(), ss.end());
                OQS_KEM_free(kem);
                return result;
            }
        }
        liboqsFailure = true;
        OQS_KEM_free(kem);
    } else {
        liboqsFailure = true;
    }
#endif

    impl_->fillRandom(result.ciphertext.data(), KYBER_CIPHERTEXT_SIZE);

    auto publicImprint = simulatedPublicImprintFromPublicKey(publicKey);
    result.sharedSecret = deriveSimulatedSharedSecret(publicImprint,
                                                      result.ciphertext.data(),
                                                      result.ciphertext.size());
#ifdef USE_LIBOQS
    if (liboqsFailure) {
        std::fprintf(stderr, "CRITICAL: liboqs was compiled in but Kyber encapsulation failed\n");
    } else {
        std::fprintf(stderr, "warning: Kyber encapsulate using simulated implementation (liboqs not available)\n");
    }
#else
    std::fprintf(stderr, "warning: Kyber encapsulate using simulated implementation (liboqs not available)\n");
#endif
    return result;
}

std::vector<uint8_t> Kyber::decapsulate(const KyberCiphertext& ciphertext,
                                         const KyberSecretKey& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    bool liboqsFailure = false;
#ifdef USE_LIBOQS
    OQS_KEM* kem = detail::newPreferredKyberKem();
    if (kem) {
        if (ciphertext.size() >= kem->length_ciphertext && secretKey.size() >= kem->length_secret_key) {
            std::vector<uint8_t> ss(kem->length_shared_secret);
            if (OQS_KEM_decaps(kem, ss.data(), ciphertext.data(), secretKey.data()) == OQS_SUCCESS) {
                OQS_KEM_free(kem);
                return ss;
            }
        }
        liboqsFailure = true;
        OQS_KEM_free(kem);
    } else {
        liboqsFailure = true;
    }
#endif

    auto publicImprint = simulatedPublicImprintFromSecretKey(secretKey);
    auto hash = deriveSimulatedSharedSecret(publicImprint, ciphertext.data(), ciphertext.size());
#ifdef USE_LIBOQS
    if (liboqsFailure) {
        std::fprintf(stderr, "CRITICAL: liboqs was compiled in but Kyber decapsulation failed\n");
    } else {
        std::fprintf(stderr, "warning: Kyber decapsulate using simulated implementation (liboqs not available)\n");
    }
#else
    std::fprintf(stderr, "warning: Kyber decapsulate using simulated implementation (liboqs not available)\n");
#endif
    return hash;
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
