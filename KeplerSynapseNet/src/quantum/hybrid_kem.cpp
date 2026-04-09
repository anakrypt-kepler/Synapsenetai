#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <cstring>
#include <sodium.h>

namespace synapse {
namespace quantum {

struct HybridKEM::Impl {
    mutable std::mutex mtx;
    CryptoAlgorithm classicAlgo = CryptoAlgorithm::CLASSIC_X25519;
    CryptoAlgorithm pqcAlgo = CryptoAlgorithm::LATTICE_KYBER768;
    Kyber kyber;
    bool sodiumReady = false;

    Impl() {
        sodiumReady = (sodium_init() >= 0);
    }
};

HybridKEM::HybridKEM() : impl_(std::make_unique<Impl>()) {}
HybridKEM::~HybridKEM() = default;

HybridKeyPair HybridKEM::generateKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    HybridKeyPair kp;
    kp.classicAlgo = impl_->classicAlgo;
    kp.pqcAlgo = impl_->pqcAlgo;

    kp.classicSecretKey.resize(crypto_scalarmult_curve25519_SCALARBYTES);
    kp.classicPublicKey.resize(crypto_scalarmult_curve25519_BYTES);
    crypto_box_keypair(kp.classicPublicKey.data(), kp.classicSecretKey.data());

    auto kyberKp = impl_->kyber.generateKeyPair();
    kp.pqcPublicKey.assign(kyberKp.publicKey.begin(), kyberKp.publicKey.end());
    kp.pqcSecretKey.assign(kyberKp.secretKey.begin(), kyberKp.secretKey.end());

    return kp;
}

EncapsulationResult HybridKEM::encapsulate(const HybridKeyPair& recipientPublicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    EncapsulationResult result;
    result.success = true;

    uint8_t ephemeralSk[crypto_scalarmult_curve25519_SCALARBYTES];
    uint8_t ephemeralPk[crypto_scalarmult_curve25519_BYTES];
    crypto_box_keypair(ephemeralPk, ephemeralSk);

    uint8_t classicShared[crypto_scalarmult_curve25519_BYTES];
    if (crypto_scalarmult(classicShared, ephemeralSk, recipientPublicKey.classicPublicKey.data()) != 0) {
        result.success = false;
        sodium_memzero(ephemeralSk, sizeof(ephemeralSk));
        return result;
    }

    std::vector<uint8_t> classicSecret(classicShared, classicShared + crypto_scalarmult_curve25519_BYTES);
    sodium_memzero(ephemeralSk, sizeof(ephemeralSk));
    sodium_memzero(classicShared, sizeof(classicShared));

    KyberPublicKey kyberPk{};
    size_t copyLen = std::min(recipientPublicKey.pqcPublicKey.size(), kyberPk.size());
    std::memcpy(kyberPk.data(), recipientPublicKey.pqcPublicKey.data(), copyLen);

    auto kyberResult = impl_->kyber.encapsulate(kyberPk);

    result.ciphertext.assign(ephemeralPk, ephemeralPk + crypto_scalarmult_curve25519_BYTES);
    result.ciphertext.insert(result.ciphertext.end(),
                             kyberResult.ciphertext.begin(),
                             kyberResult.ciphertext.end());

    result.sharedSecret = combineSharedSecrets(classicSecret, kyberResult.sharedSecret);

    return result;
}

std::vector<uint8_t> HybridKEM::decapsulate(const std::vector<uint8_t>& ciphertext,
                                             const HybridKeyPair& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    if (ciphertext.size() < crypto_scalarmult_curve25519_BYTES + KYBER_CIPHERTEXT_SIZE) {
        return {};
    }

    const uint8_t* ephemeralPk = ciphertext.data();
    uint8_t classicShared[crypto_scalarmult_curve25519_BYTES];
    if (crypto_scalarmult(classicShared, secretKey.classicSecretKey.data(), ephemeralPk) != 0) {
        return {};
    }

    std::vector<uint8_t> classicSecret(classicShared, classicShared + crypto_scalarmult_curve25519_BYTES);
    sodium_memzero(classicShared, sizeof(classicShared));

    KyberCiphertext kyberCt{};
    std::memcpy(kyberCt.data(), ciphertext.data() + crypto_scalarmult_curve25519_BYTES, KYBER_CIPHERTEXT_SIZE);

    KyberSecretKey kyberSk{};
    size_t copyLen = std::min(secretKey.pqcSecretKey.size(), kyberSk.size());
    std::memcpy(kyberSk.data(), secretKey.pqcSecretKey.data(), copyLen);

    auto pqcSecret = impl_->kyber.decapsulate(kyberCt, kyberSk);

    return combineSharedSecrets(classicSecret, pqcSecret);
}

void HybridKEM::setClassicAlgorithm(CryptoAlgorithm algo) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->classicAlgo = algo;
}

void HybridKEM::setPQCAlgorithm(CryptoAlgorithm algo) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->pqcAlgo = algo;
}

std::vector<uint8_t> HybridKEM::combineSharedSecrets(const std::vector<uint8_t>& classicSecret,
                                                      const std::vector<uint8_t>& pqcSecret) {
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), classicSecret.begin(), classicSecret.end());
    combined.insert(combined.end(), pqcSecret.begin(), pqcSecret.end());

    auto hash = crypto::sha256(combined.data(), combined.size());
    sodium_memzero(combined.data(), combined.size());
    return std::vector<uint8_t>(hash.begin(), hash.end());
}

}
}
