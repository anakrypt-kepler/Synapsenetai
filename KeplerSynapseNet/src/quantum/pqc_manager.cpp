#include "quantum/pqc_manager.h"
#include <mutex>

namespace synapse {
namespace quantum {

struct PQCManager::Impl {
    SecurityLevel level = SecurityLevel::STANDARD;
    bool initialized = false;
    mutable std::mutex mtx;
    Kyber kyber;
    Dilithium dilithium;
    Sphincs sphincs;
    HybridKEM hybridKEM;
    HybridSig hybridSig;
};

PQCManager::PQCManager() : impl_(std::make_unique<Impl>()) {}
PQCManager::~PQCManager() { shutdown(); }

bool PQCManager::init(SecurityLevel level) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->level = level;
    impl_->initialized = true;
    return true;
}

void PQCManager::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->initialized = false;
}

SecurityLevel PQCManager::getSecurityLevel() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->level;
}

KyberKeyPair PQCManager::generateKyberKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->kyber.generateKeyPair();
}

DilithiumKeyPair PQCManager::generateDilithiumKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->dilithium.generateKeyPair();
}

SphincsKeyPair PQCManager::generateSphincsKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->sphincs.generateKeyPair();
}

HybridKeyPair PQCManager::generateHybridKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->hybridKEM.generateKeyPair();
}

EncapsulationResult PQCManager::encapsulateHybrid(const HybridKeyPair& recipientKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->initialized) {
        EncapsulationResult result;
        result.success = false;
        return result;
    }
    return impl_->hybridKEM.encapsulate(recipientKey);
}

std::vector<uint8_t> PQCManager::decapsulateHybrid(const std::vector<uint8_t>& ciphertext,
                                                   const HybridKeyPair& keyPair) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->initialized) return {};
    return impl_->hybridKEM.decapsulate(ciphertext, keyPair);
}

SignatureResult PQCManager::signDilithium(const std::vector<uint8_t>& message,
                                          const DilithiumSecretKey& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->initialized) {
        SignatureResult result;
        result.success = false;
        return result;
    }
    return impl_->dilithium.sign(message, secretKey);
}

bool PQCManager::verifyDilithium(const std::vector<uint8_t>& message,
                                 const DilithiumSignature& signature,
                                 const DilithiumPublicKey& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->initialized) return false;
    return impl_->dilithium.verify(message, signature, publicKey);
}

SignatureResult PQCManager::signSphincs(const std::vector<uint8_t>& message,
                                        const SphincsSecretKey& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->initialized) {
        SignatureResult result;
        result.success = false;
        return result;
    }
    return impl_->sphincs.sign(message, secretKey);
}

bool PQCManager::verifySphincs(const std::vector<uint8_t>& message,
                               const SphincsSignature& signature,
                               const SphincsPublicKey& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->initialized) return false;
    return impl_->sphincs.verify(message, signature, publicKey);
}

}
}
