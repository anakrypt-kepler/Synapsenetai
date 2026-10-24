#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <cstring>
#include <sodium.h>

namespace synapse {
namespace quantum {

namespace {

bool kyberHalfRequested(CryptoAlgorithm configuredAlgo,
                        CryptoAlgorithm keyAlgo,
                        size_t pqcKeySize) {
    return configuredAlgo == CryptoAlgorithm::LATTICE_KYBER768
        || keyAlgo == CryptoAlgorithm::LATTICE_KYBER768
        || pqcKeySize == KYBER_PUBLIC_KEY_SIZE
        || pqcKeySize == KYBER_SECRET_KEY_SIZE;
}

bool mlKemSharePresent(const EncapsulationResult& kyberResult) {
    return kyberResult.success
        && kyberResult.sharedSecret.size() == KYBER_SHARED_SECRET_SIZE
        && kyberResult.ciphertext.size() == KYBER_CIPHERTEXT_SIZE;
}

void wipeBytes(std::vector<uint8_t>& bytes) {
    if (!bytes.empty()) {
        sodium_memzero(bytes.data(), bytes.size());
    }
}

}  // namespace

struct HybridKEM::Impl {
    mutable std::mutex mtx;
    CryptoAlgorithm classicAlgo = CryptoAlgorithm::CLASSIC_X25519;
    CryptoAlgorithm pqcAlgo = CryptoAlgorithm::LATTICE_KYBER768;
    Kyber kyber;
    bool sodiumReady = false;

    Impl() {
        crypto::containForeignCryptoModules();
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
    if (crypto_box_keypair(kp.classicPublicKey.data(), kp.classicSecretKey.data()) != 0) {
        sodium_memzero(kp.classicPublicKey.data(), kp.classicPublicKey.size());
        sodium_memzero(kp.classicSecretKey.data(), kp.classicSecretKey.size());
        kp.classicPublicKey.clear();
        kp.classicSecretKey.clear();
        return kp;
    }

    auto kyberKp = impl_->kyber.generateKeyPair();
    kp.pqcPublicKey.assign(kyberKp.publicKey.begin(), kyberKp.publicKey.end());
    kp.pqcSecretKey.assign(kyberKp.secretKey.begin(), kyberKp.secretKey.end());

    return kp;
}

EncapsulationResult HybridKEM::encapsulate(const HybridKeyPair& recipientPublicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    EncapsulationResult result;
    result.success = false;

    uint8_t ephemeralSk[crypto_scalarmult_curve25519_SCALARBYTES];
    uint8_t ephemeralPk[crypto_scalarmult_curve25519_BYTES];
    if (crypto_box_keypair(ephemeralPk, ephemeralSk) != 0) {
        sodium_memzero(ephemeralSk, sizeof(ephemeralSk));
        sodium_memzero(ephemeralPk, sizeof(ephemeralPk));
        return result;
    }

    uint8_t classicShared[crypto_scalarmult_curve25519_BYTES];
    if (crypto_scalarmult(classicShared, ephemeralSk, recipientPublicKey.classicPublicKey.data()) != 0) {
        sodium_memzero(ephemeralSk, sizeof(ephemeralSk));
        sodium_memzero(classicShared, sizeof(classicShared));
        return result;
    }

    std::vector<uint8_t> classicSecret(classicShared, classicShared + crypto_scalarmult_curve25519_BYTES);
    sodium_memzero(ephemeralSk, sizeof(ephemeralSk));
    sodium_memzero(classicShared, sizeof(classicShared));

    // The caller asked for the Kyber half. A short public key must not be
    // zero-padded into an ML-KEM-768 encapsulate that still returns 32 and 1088.
    const bool kyberRequested = kyberHalfRequested(
        impl_->pqcAlgo, recipientPublicKey.pqcAlgo, recipientPublicKey.pqcPublicKey.size());
    if (kyberRequested &&
        recipientPublicKey.pqcPublicKey.size() != KYBER_PUBLIC_KEY_SIZE) {
        wipeBytes(classicSecret);
        return result;
    }

    EncapsulationResult kyberResult;
    kyberResult.success = false;
    if (recipientPublicKey.pqcPublicKey.size() == KYBER_PUBLIC_KEY_SIZE) {
        KyberPublicKey kyberPk{};
        std::memcpy(kyberPk.data(), recipientPublicKey.pqcPublicKey.data(), kyberPk.size());
        kyberResult = impl_->kyber.encapsulate(kyberPk);
    }

    // liboqs produced no ML-KEM share. Fail closed. Do not publish a hash of
    // the X25519 share alone.
    if (kyberRequested && !mlKemSharePresent(kyberResult)) {
        wipeBytes(classicSecret);
        wipeBytes(kyberResult.sharedSecret);
        return result;
    }

    result.ciphertext.assign(ephemeralPk, ephemeralPk + crypto_scalarmult_curve25519_BYTES);
    result.ciphertext.insert(result.ciphertext.end(),
                             kyberResult.ciphertext.begin(),
                             kyberResult.ciphertext.end());

    result.sharedSecret = combineSharedSecrets(classicSecret, kyberResult.sharedSecret);
    result.success = true;
    wipeBytes(classicSecret);
    wipeBytes(kyberResult.sharedSecret);
    return result;
}

std::vector<uint8_t> HybridKEM::decapsulate(const std::vector<uint8_t>& ciphertext,
                                             const HybridKeyPair& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    // Same rule as encapsulate: a requested Kyber half needs a full secret key.
    // A shorter key must not be zero-padded and then accepted because decaps
    // still returned 32 bytes.
    const bool kyberRequested = kyberHalfRequested(
        impl_->pqcAlgo, secretKey.pqcAlgo, secretKey.pqcSecretKey.size());
    if (kyberRequested && secretKey.pqcSecretKey.size() != KYBER_SECRET_KEY_SIZE) {
        return {};
    }

    if (ciphertext.size() < crypto_scalarmult_curve25519_BYTES + KYBER_CIPHERTEXT_SIZE) {
        return {};
    }

    const uint8_t* ephemeralPk = ciphertext.data();
    uint8_t classicShared[crypto_scalarmult_curve25519_BYTES];
    if (crypto_scalarmult(classicShared, secretKey.classicSecretKey.data(), ephemeralPk) != 0) {
        sodium_memzero(classicShared, sizeof(classicShared));
        return {};
    }

    std::vector<uint8_t> classicSecret(classicShared, classicShared + crypto_scalarmult_curve25519_BYTES);
    sodium_memzero(classicShared, sizeof(classicShared));

    std::vector<uint8_t> pqcSecret;
    if (secretKey.pqcSecretKey.size() == KYBER_SECRET_KEY_SIZE) {
        KyberCiphertext kyberCt{};
        std::memcpy(kyberCt.data(), ciphertext.data() + crypto_scalarmult_curve25519_BYTES, KYBER_CIPHERTEXT_SIZE);
        KyberSecretKey kyberSk{};
        std::memcpy(kyberSk.data(), secretKey.pqcSecretKey.data(), kyberSk.size());
        pqcSecret = impl_->kyber.decapsulate(kyberCt, kyberSk);
        sodium_memzero(kyberSk.data(), kyberSk.size());
    }

    // A requested Kyber half with no liboqs share must not collapse to a
    // hash of the X25519 share.
    if (kyberRequested && pqcSecret.size() != KYBER_SHARED_SECRET_SIZE) {
        wipeBytes(classicSecret);
        wipeBytes(pqcSecret);
        return {};
    }

    auto shared = combineSharedSecrets(classicSecret, pqcSecret);
    wipeBytes(classicSecret);
    wipeBytes(pqcSecret);
    return shared;
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
