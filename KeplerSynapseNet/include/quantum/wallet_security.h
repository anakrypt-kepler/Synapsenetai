#pragma once

#include "quantum_security.h"
#include <vector>

namespace synapse {
namespace quantum {

class WalletSecurity {
public:
    WalletSecurity();

    void setSecurityLevel(SecurityLevel level);
    SecurityLevel getSecurityLevel() const;

    void setQuantumManager(QuantumManager* qm);
    void setHybridKEM(HybridKEM* kem);

    // AES-256-GCM under `key`. A key of AES-256 length contributes its leading
    // 32 bytes, and the blob is the crypto::encryptAES layout. A shorter key is
    // stretched with crypto::deriveKey (PBKDF2-HMAC-SHA256) and a random salt
    // stored in front of that blob. Decrypt applies the same rule. The supplied
    // key is the secret both sides hold.
    std::vector<uint8_t> encryptSeed(const std::vector<uint8_t>& seed, const std::vector<uint8_t>& key) const;
    std::vector<uint8_t> decryptSeed(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    std::vector<uint8_t> wrapKey(const std::vector<uint8_t>& privateKey, const std::vector<uint8_t>& wrappingKey) const;
    std::vector<uint8_t> unwrapKey(const std::vector<uint8_t>& data, const std::vector<uint8_t>& wrappingKey) const;

    // Wallet file v4: password PBKDF2 key seeds a deterministic ML-KEM-768
    // keypair; encapsulate a random DEK; AES-GCM the plaintext with that DEK.
    // Returns false when liboqs/Kyber is missing or simulated — callers must
    // write format v3 instead of pretending the wrap is post-quantum.
    // Deterministic coins stay on the calling thread. Other threads that enter
    // the process-wide RNG hook draw from the operating-system generator.
    static bool fileKyberWrapAvailable();
    static bool deriveFileKyberKeyPair(const std::vector<uint8_t>& pbkdf2Key32, KyberKeyPair& out);
    static bool encapsulateFileDek(const KyberPublicKey& pk,
                                   std::vector<uint8_t>& kemCtOut,
                                   std::vector<uint8_t>& dekOut);
    static bool decapsulateFileDek(const std::vector<uint8_t>& kemCt,
                                   const KyberSecretKey& sk,
                                   std::vector<uint8_t>& dekOut);

private:
    SecurityLevel level_ = SecurityLevel::STANDARD;
    QuantumManager* qm_ = nullptr;
    HybridKEM* hybridKEM_ = nullptr;
};

}
}
