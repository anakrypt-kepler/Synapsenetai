#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <random>
#include <cstring>
#include <algorithm>
#include <sodium.h>

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse {
namespace quantum {

struct HybridSig::Impl {
    mutable std::mutex mtx;
    CryptoAlgorithm classicAlgo = CryptoAlgorithm::CLASSIC_ED25519;
    CryptoAlgorithm pqcAlgo = CryptoAlgorithm::LATTICE_DILITHIUM65;
    Dilithium dilithium;
    bool sodiumReady = false;

    Impl() {
        sodiumReady = (sodium_init() >= 0);
    }
};

HybridSig::HybridSig() : impl_(std::make_unique<Impl>()) {}
HybridSig::~HybridSig() = default;

HybridKeyPair HybridSig::generateKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    HybridKeyPair kp;
    kp.classicAlgo = impl_->classicAlgo;
    kp.pqcAlgo = impl_->pqcAlgo;

    kp.classicPublicKey.resize(crypto_sign_ed25519_PUBLICKEYBYTES);
    kp.classicSecretKey.resize(crypto_sign_ed25519_SECRETKEYBYTES);
    crypto_sign_ed25519_keypair(kp.classicPublicKey.data(), kp.classicSecretKey.data());

    auto dilithiumKp = impl_->dilithium.generateKeyPair();
    kp.pqcPublicKey.assign(dilithiumKp.publicKey.begin(), dilithiumKp.publicKey.end());
    kp.pqcSecretKey.assign(dilithiumKp.secretKey.begin(), dilithiumKp.secretKey.end());

    return kp;
}

SignatureResult HybridSig::sign(const std::vector<uint8_t>& message,
                                 const HybridKeyPair& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    SignatureResult result;

    std::vector<uint8_t> classicSig(crypto_sign_ed25519_BYTES);
    unsigned long long classicSigLen = 0;
    if (crypto_sign_ed25519_detached(classicSig.data(), &classicSigLen,
                                      message.data(), message.size(),
                                      secretKey.classicSecretKey.data()) != 0) {
        result.success = false;
        return result;
    }
    classicSig.resize(classicSigLen);

    DilithiumSecretKey dilSk{};
    size_t copyLen = std::min(secretKey.pqcSecretKey.size(), dilSk.size());
    std::memcpy(dilSk.data(), secretKey.pqcSecretKey.data(), copyLen);
    auto pqcResult = impl_->dilithium.sign(message, dilSk);

    if (!pqcResult.success) {
        result.success = false;
        return result;
    }

    result.signature.clear();
    result.signature.insert(result.signature.end(), classicSig.begin(), classicSig.end());
    result.signature.insert(result.signature.end(), pqcResult.signature.begin(), pqcResult.signature.end());
    result.success = true;
    return result;
}

bool HybridSig::verify(const std::vector<uint8_t>& message,
                        const std::vector<uint8_t>& signature,
                        const HybridKeyPair& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    if (signature.size() < crypto_sign_ed25519_BYTES + DILITHIUM_SIGNATURE_SIZE) {
        return false;
    }

    if (crypto_sign_ed25519_verify_detached(signature.data(),
                                             message.data(), message.size(),
                                             publicKey.classicPublicKey.data()) != 0) {
        return false;
    }

    DilithiumPublicKey dilPk{};
    size_t copyLen = std::min(publicKey.pqcPublicKey.size(), dilPk.size());
    std::memcpy(dilPk.data(), publicKey.pqcPublicKey.data(), copyLen);

    DilithiumSignature dilSig{};
    const uint8_t* pqcSigStart = signature.data() + crypto_sign_ed25519_BYTES;
    size_t pqcSigLen = std::min(signature.size() - crypto_sign_ed25519_BYTES, dilSig.size());
    std::memcpy(dilSig.data(), pqcSigStart, pqcSigLen);

    return impl_->dilithium.verify(message, dilSig, dilPk);
}

void HybridSig::setClassicAlgorithm(CryptoAlgorithm algo) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->classicAlgo = algo;
}

void HybridSig::setPQCAlgorithm(CryptoAlgorithm algo) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->pqcAlgo = algo;
}

}
}
