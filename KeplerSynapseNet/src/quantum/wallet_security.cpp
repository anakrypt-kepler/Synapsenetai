#include "quantum/wallet_security.h"
#include "crypto/crypto.h"
#include <array>
#include <algorithm>
#include <cstring>

namespace synapse {
namespace quantum {

static std::array<uint8_t, crypto::AES_KEY_SIZE> toAesKey(const std::vector<uint8_t>& key) {
    std::array<uint8_t, crypto::AES_KEY_SIZE> out{};
    if (key.size() >= out.size()) {
        std::copy_n(key.begin(), out.size(), out.begin());
        return out;
    }
    auto hash = crypto::sha256(key.data(), key.size());
    std::copy_n(hash.begin(), out.size(), out.begin());
    return out;
}

WalletSecurity::WalletSecurity() = default;

void WalletSecurity::setSecurityLevel(SecurityLevel level) {
    level_ = level;
}

SecurityLevel WalletSecurity::getSecurityLevel() const {
    return level_;
}

void WalletSecurity::setQuantumManager(QuantumManager* qm) {
    qm_ = qm;
}

void WalletSecurity::setHybridKEM(HybridKEM* kem) {
    hybridKEM_ = kem;
}

std::vector<uint8_t> WalletSecurity::encryptSeed(const std::vector<uint8_t>& seed,
                                                 const std::vector<uint8_t>& key) const {
    if (seed.empty()) return {};

    if (level_ == SecurityLevel::QUANTUM_READY && qm_) {
        return qm_->encryptQuantumSafe(seed);
    }

    if (level_ >= SecurityLevel::HIGH && hybridKEM_) {
        HybridKeyPair kemKp = hybridKEM_->generateKeyPair();
        auto kemResult = hybridKEM_->encapsulate(kemKp);
        if (kemResult.success && kemResult.sharedSecret.size() >= crypto::AES_KEY_SIZE) {
            std::array<uint8_t, crypto::AES_KEY_SIZE> derivedKey{};
            std::copy_n(kemResult.sharedSecret.begin(), crypto::AES_KEY_SIZE, derivedKey.begin());
            auto encrypted = crypto::encryptAES(seed, derivedKey);

            std::vector<uint8_t> envelope;
            uint32_t ctLen = static_cast<uint32_t>(kemResult.ciphertext.size());
            envelope.push_back(static_cast<uint8_t>((ctLen >> 24) & 0xFF));
            envelope.push_back(static_cast<uint8_t>((ctLen >> 16) & 0xFF));
            envelope.push_back(static_cast<uint8_t>((ctLen >> 8) & 0xFF));
            envelope.push_back(static_cast<uint8_t>(ctLen & 0xFF));
            envelope.insert(envelope.end(), kemResult.ciphertext.begin(), kemResult.ciphertext.end());
            envelope.insert(envelope.end(), encrypted.begin(), encrypted.end());
            return envelope;
        }
    }

    return crypto::encryptAES(seed, toAesKey(key));
}

std::vector<uint8_t> WalletSecurity::decryptSeed(const std::vector<uint8_t>& data,
                                                 const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};

    if (level_ == SecurityLevel::QUANTUM_READY && qm_) {
        return qm_->decryptQuantumSafe(data);
    }

    return crypto::decryptAES(data, toAesKey(key));
}

std::vector<uint8_t> WalletSecurity::wrapKey(const std::vector<uint8_t>& privateKey,
                                             const std::vector<uint8_t>& wrappingKey) const {
    if (privateKey.empty()) return {};

    if (level_ >= SecurityLevel::HIGH && hybridKEM_) {
        HybridKeyPair kemKp = hybridKEM_->generateKeyPair();
        auto kemResult = hybridKEM_->encapsulate(kemKp);
        if (kemResult.success && kemResult.sharedSecret.size() >= crypto::AES_KEY_SIZE) {
            std::array<uint8_t, crypto::AES_KEY_SIZE> derivedKey{};
            std::copy_n(kemResult.sharedSecret.begin(), crypto::AES_KEY_SIZE, derivedKey.begin());
            return crypto::encryptAES(privateKey, derivedKey);
        }
    }

    return crypto::encryptAES(privateKey, toAesKey(wrappingKey));
}

std::vector<uint8_t> WalletSecurity::unwrapKey(const std::vector<uint8_t>& data,
                                               const std::vector<uint8_t>& wrappingKey) const {
    if (data.empty()) return {};
    return crypto::decryptAES(data, toAesKey(wrappingKey));
}

}
}
