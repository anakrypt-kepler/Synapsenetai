#include "quantum/quantum_security.h"
#include <mutex>
#include <string>

namespace {
constexpr uint8_t kQuantumFrameMagic = 0xA5;
constexpr uint8_t kQuantumFrameModeHybrid = 0x00;
constexpr uint8_t kQuantumFrameModeQkd = 0x01;

bool kemDowngradedToClassic(synapse::quantum::SecurityLevel level, synapse::quantum::CryptoAlgorithm algo) {
    return level != synapse::quantum::SecurityLevel::STANDARD &&
           algo == synapse::quantum::CryptoAlgorithm::CLASSIC_X25519;
}

bool signatureDowngradedToClassic(synapse::quantum::SecurityLevel level, synapse::quantum::CryptoAlgorithm algo) {
    return level != synapse::quantum::SecurityLevel::STANDARD &&
           algo == synapse::quantum::CryptoAlgorithm::CLASSIC_ED25519;
}
}

namespace synapse {
namespace quantum {

struct QuantumSecurityManager::Impl {
    SecurityLevel level = SecurityLevel::HIGH;
    bool initialized = false;
    mutable std::mutex mtx;
    
    Kyber kyber_;
    Dilithium dilithium_;
    Sphincs sphincs_;
    HybridKEM hybridKEM_;
    HybridSig hybridSig_;
    OTPManager otpManager_;
    HWRNG hwrng_;
    QKDIntegration qkd_;
    CryptoSelector selector_;
    KeyDerivation kdf_;
    TimingDefense timing_;
    
    QuantumStats stats{};
};

QuantumSecurityManager::QuantumSecurityManager() : impl_(std::make_unique<Impl>()) {}
QuantumSecurityManager::~QuantumSecurityManager() { shutdown(); }

bool QuantumSecurityManager::init(SecurityLevel level) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->initialized) return true;
    
    impl_->level = level;
    impl_->selector_.setSecurityLevel(level);
    
    if (!impl_->hwrng_.init()) {
        return false;
    }
    
    if (!impl_->otpManager_.init("otp_keys")) {
        return false;
    }

    if (level == SecurityLevel::QUANTUM_READY) {
        impl_->qkd_.init("qkd0");
    }
    
    impl_->initialized = true;
    return true;
}

void QuantumSecurityManager::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->initialized) return;
    
    impl_->otpManager_.shutdown();
    impl_->qkd_.shutdown();
    impl_->initialized = false;
}

Kyber& QuantumSecurityManager::kyber() { return impl_->kyber_; }
Dilithium& QuantumSecurityManager::dilithium() { return impl_->dilithium_; }
Sphincs& QuantumSecurityManager::sphincs() { return impl_->sphincs_; }
HybridKEM& QuantumSecurityManager::hybridKEM() { return impl_->hybridKEM_; }
HybridSig& QuantumSecurityManager::hybridSig() { return impl_->hybridSig_; }
OTPManager& QuantumSecurityManager::otpManager() { return impl_->otpManager_; }
HWRNG& QuantumSecurityManager::hwrng() { return impl_->hwrng_; }
QKDIntegration& QuantumSecurityManager::qkd() { return impl_->qkd_; }
CryptoSelector& QuantumSecurityManager::selector() { return impl_->selector_; }
KeyDerivation& QuantumSecurityManager::kdf() { return impl_->kdf_; }
TimingDefense& QuantumSecurityManager::timing() { return impl_->timing_; }

void QuantumSecurityManager::setSecurityLevel(SecurityLevel level) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->level = level;
    impl_->selector_.setSecurityLevel(level);
    if (level == SecurityLevel::QUANTUM_READY) {
        if (!impl_->qkd_.isConnected()) {
            impl_->qkd_.init("qkd0");
        }
    } else if (impl_->qkd_.isConnected()) {
        impl_->qkd_.shutdown();
    }
}

SecurityLevel QuantumSecurityManager::getSecurityLevel() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->level;
}

QuantumStats QuantumSecurityManager::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->stats;
}

struct QuantumManager::Impl {
    SecurityLevel level = SecurityLevel::STANDARD;
    bool initialized = false;
    mutable std::mutex mtx;
    
    Kyber kyber_;
    Dilithium dilithium_;
    HybridKEM hybridKEM_;
    HybridSig hybridSig_;
    OTPManager otpManager_;
    HWRNG hwrng_;
    QKDIntegration qkd_;
    CryptoSelector selector_;
    
    QuantumStats stats{};
    HybridKeyPair currentKemKeyPair;
    HybridKeyPair currentSigKeyPair;
    std::string qkdSessionId;
    uint64_t qkdEncryptOperations = 0;
    uint64_t hybridEncryptOperations = 0;
    uint64_t qkdDecryptOperations = 0;
    uint64_t hybridDecryptOperations = 0;
    uint64_t qkdFallbackDecryptOperations = 0;
};

QuantumManager::QuantumManager() : impl_(std::make_unique<Impl>()) {}
QuantumManager::~QuantumManager() { shutdown(); }

bool QuantumManager::init(SecurityLevel level) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->initialized) return true;
    
    impl_->level = level;
    impl_->selector_.setSecurityLevel(level);
    
    if (!impl_->hwrng_.init()) {
        return false;
    }
    
    if (!impl_->otpManager_.init("quantum_keys")) {
        return false;
    }
    
    impl_->currentKemKeyPair = impl_->hybridKEM_.generateKeyPair();
    impl_->currentSigKeyPair = impl_->hybridSig_.generateKeyPair();
    if (level == SecurityLevel::QUANTUM_READY) {
        if (impl_->qkd_.init("qkd0")) {
            auto session = impl_->qkd_.establishSession("local-node");
            if (session.active) {
                impl_->qkdSessionId = session.sessionId;
                impl_->stats.qkdSessionsEstablished++;
            }
        }
    }
    impl_->initialized = true;
    return true;
}

void QuantumManager::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->initialized) return;
    
    if (!impl_->qkdSessionId.empty()) {
        impl_->qkd_.closeSession(impl_->qkdSessionId);
        impl_->qkdSessionId.clear();
    }
    impl_->qkd_.shutdown();
    impl_->otpManager_.shutdown();
    impl_->initialized = false;
}

void QuantumManager::performMaintenance() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->initialized) return;
    
    pruneExpiredKeys();
    if (impl_->level == SecurityLevel::QUANTUM_READY && impl_->qkdSessionId.empty() && impl_->qkd_.isConnected()) {
        auto session = impl_->qkd_.establishSession("local-node");
        if (session.active) {
            impl_->qkdSessionId = session.sessionId;
            impl_->stats.qkdSessionsEstablished++;
        }
    }
}

void QuantumManager::rotateKeys() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->initialized) return;
    
    impl_->currentKemKeyPair = impl_->hybridKEM_.generateKeyPair();
    impl_->currentSigKeyPair = impl_->hybridSig_.generateKeyPair();
    if (impl_->level == SecurityLevel::QUANTUM_READY && impl_->qkd_.isConnected()) {
        if (!impl_->qkdSessionId.empty()) {
            impl_->qkd_.closeSession(impl_->qkdSessionId);
        }
        auto session = impl_->qkd_.establishSession("local-node");
        if (session.active) {
            impl_->qkdSessionId = session.sessionId;
            impl_->stats.qkdSessionsEstablished++;
        } else {
            impl_->qkdSessionId.clear();
        }
    }
}

void QuantumManager::pruneExpiredKeys() {
    impl_->otpManager_.pruneExpiredKeys();
}

std::vector<uint8_t> QuantumManager::generateQuantumSafeKey(size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->initialized && impl_->level == SecurityLevel::QUANTUM_READY && impl_->qkd_.isConnected() && !impl_->qkdSessionId.empty()) {
        auto qkdKey = impl_->qkd_.getKeyMaterial(impl_->qkdSessionId, length);
        if (!qkdKey.empty()) {
            return qkdKey;
        }
    }
    return impl_->hwrng_.generate(length);
}

std::vector<uint8_t> QuantumManager::encryptQuantumSafe(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->initialized || data.empty()) return {};
    if (kemDowngradedToClassic(impl_->level, impl_->selector_.selectKEM())) return {};

    uint8_t mode = kQuantumFrameModeHybrid;
    std::vector<uint8_t> sharedSecret;
    std::vector<uint8_t> kemCiphertext;
    bool usedQkd = false;
    if (impl_->level == SecurityLevel::QUANTUM_READY && impl_->qkd_.isConnected() && !impl_->qkdSessionId.empty()) {
        sharedSecret = impl_->qkd_.getKeyMaterial(impl_->qkdSessionId, KYBER_SHARED_SECRET_SIZE);
        if (!sharedSecret.empty()) {
            mode = kQuantumFrameModeQkd;
            usedQkd = true;
        }
    }
    if (sharedSecret.empty()) {
        auto kem = impl_->hybridKEM_.encapsulate(impl_->currentKemKeyPair);
        if (!kem.success || kem.sharedSecret.empty()) {
            return {};
        }
        kemCiphertext = kem.ciphertext;
        sharedSecret = kem.sharedSecret;
        mode = kQuantumFrameModeHybrid;
    }

    std::vector<uint8_t> output;
    output.reserve(6 + kemCiphertext.size() + data.size());
    output.push_back(kQuantumFrameMagic);
    output.push_back(mode);

    uint32_t ctSize = static_cast<uint32_t>(kemCiphertext.size());
    output.push_back((ctSize >> 24) & 0xFF);
    output.push_back((ctSize >> 16) & 0xFF);
    output.push_back((ctSize >> 8) & 0xFF);
    output.push_back(ctSize & 0xFF);
    
    output.insert(output.end(), kemCiphertext.begin(), kemCiphertext.end());
    
    for (size_t i = 0; i < data.size(); i++) {
        output.push_back(data[i] ^ sharedSecret[i % sharedSecret.size()]);
    }
    
    impl_->stats.hybridOperations++;
    if (usedQkd) {
        impl_->qkdEncryptOperations++;
    } else {
        impl_->hybridEncryptOperations++;
    }
    return output;
}

std::vector<uint8_t> QuantumManager::decryptQuantumSafe(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->initialized || data.size() < 4) return {};
    if (kemDowngradedToClassic(impl_->level, impl_->selector_.selectKEM())) return {};

    uint8_t mode = kQuantumFrameModeHybrid;
    size_t headerOffset = 0;
    uint32_t ctSize = 0;
    if (data.size() >= 6 && data[0] == kQuantumFrameMagic) {
        mode = data[1];
        headerOffset = 2;
    }

    ctSize = (static_cast<uint32_t>(data[headerOffset]) << 24) |
             (static_cast<uint32_t>(data[headerOffset + 1]) << 16) |
             (static_cast<uint32_t>(data[headerOffset + 2]) << 8) |
             static_cast<uint32_t>(data[headerOffset + 3]);

    if (data.size() < headerOffset + 4 + ctSize) return {};

    std::vector<uint8_t> ciphertext(data.begin() + headerOffset + 4, data.begin() + headerOffset + 4 + ctSize);
    std::vector<uint8_t> encrypted(data.begin() + headerOffset + 4 + ctSize, data.end());

    std::vector<uint8_t> sharedSecret;
    if (mode == kQuantumFrameModeQkd && impl_->level == SecurityLevel::QUANTUM_READY &&
        impl_->qkd_.isConnected() && !impl_->qkdSessionId.empty()) {
        sharedSecret = impl_->qkd_.getKeyMaterial(impl_->qkdSessionId, KYBER_SHARED_SECRET_SIZE);
        if (!sharedSecret.empty()) {
            impl_->qkdDecryptOperations++;
        }
    }
    if (sharedSecret.empty()) {
        if (mode == kQuantumFrameModeQkd) {
            impl_->qkdFallbackDecryptOperations++;
        }
        sharedSecret = impl_->hybridKEM_.decapsulate(ciphertext, impl_->currentKemKeyPair);
        if (!sharedSecret.empty()) {
            impl_->hybridDecryptOperations++;
        }
    }
    if (sharedSecret.empty()) {
        return {};
    }
    
    std::vector<uint8_t> result(encrypted.size());
    for (size_t i = 0; i < encrypted.size(); i++) {
        result[i] = encrypted[i] ^ sharedSecret[i % sharedSecret.size()];
    }
    
    impl_->stats.hybridOperations++;
    return result;
}

std::vector<uint8_t> QuantumManager::signQuantumSafe(const std::vector<uint8_t>& message) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->initialized) return {};
    if (signatureDowngradedToClassic(impl_->level, impl_->selector_.selectSignature())) return {};

    auto result = impl_->hybridSig_.sign(message, impl_->currentSigKeyPair);
    if (!result.success) {
        return {};
    }
    impl_->stats.hybridOperations++;
    return result.signature;
}

bool QuantumManager::verifyQuantumSafe(const std::vector<uint8_t>& message,
                                        const std::vector<uint8_t>& signature) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->initialized) return false;
    if (signatureDowngradedToClassic(impl_->level, impl_->selector_.selectSignature())) return false;

    impl_->stats.hybridOperations++;
    return impl_->hybridSig_.verify(message, signature, impl_->currentSigKeyPair);
}

void QuantumManager::setSecurityLevel(SecurityLevel level) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->level = level;
    impl_->selector_.setSecurityLevel(level);
    if (!impl_->initialized) {
        return;
    }
    if (level == SecurityLevel::QUANTUM_READY) {
        if (!impl_->qkd_.isConnected()) {
            impl_->qkd_.init("qkd0");
        }
        if (impl_->qkdSessionId.empty() && impl_->qkd_.isConnected()) {
            auto session = impl_->qkd_.establishSession("local-node");
            if (session.active) {
                impl_->qkdSessionId = session.sessionId;
                impl_->stats.qkdSessionsEstablished++;
            }
        }
    } else {
        if (!impl_->qkdSessionId.empty()) {
            impl_->qkd_.closeSession(impl_->qkdSessionId);
            impl_->qkdSessionId.clear();
        }
        if (impl_->qkd_.isConnected()) {
            impl_->qkd_.shutdown();
        }
    }
}

SecurityLevel QuantumManager::getSecurityLevel() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->level;
}

bool QuantumManager::isQuantumSafe() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->initialized) return false;
    if (impl_->level == SecurityLevel::STANDARD) return true;
    return !kemDowngradedToClassic(impl_->level, impl_->selector_.selectKEM()) &&
           !signatureDowngradedToClassic(impl_->level, impl_->selector_.selectSignature());
}

QuantumStats QuantumManager::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->stats;
}

QuantumRuntimeStatus QuantumManager::getRuntimeStatus() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    QuantumRuntimeStatus status;
    status.initialized = impl_->initialized;
    status.level = impl_->level;
    status.qkdConnected = impl_->qkd_.isConnected();
    status.qkdSessionActive = !impl_->qkdSessionId.empty();
    status.selectedKEM = impl_->selector_.selectKEM();
    status.selectedSignature = impl_->selector_.selectSignature();
    status.selectedEncryption = impl_->selector_.selectEncryption();
    status.qkdEncryptOperations = impl_->qkdEncryptOperations;
    status.hybridEncryptOperations = impl_->hybridEncryptOperations;
    status.qkdDecryptOperations = impl_->qkdDecryptOperations;
    status.hybridDecryptOperations = impl_->hybridDecryptOperations;
    status.qkdFallbackDecryptOperations = impl_->qkdFallbackDecryptOperations;
    status.pqc = getPQCBackendStatus();
    return status;
}

}
}
