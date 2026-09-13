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

    std::vector<uint8_t> encryptSeed(const std::vector<uint8_t>& seed, const std::vector<uint8_t>& key) const;
    std::vector<uint8_t> decryptSeed(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    std::vector<uint8_t> wrapKey(const std::vector<uint8_t>& privateKey, const std::vector<uint8_t>& wrappingKey) const;
    std::vector<uint8_t> unwrapKey(const std::vector<uint8_t>& data, const std::vector<uint8_t>& wrappingKey) const;

    // Wallet file v4: password PBKDF2 key seeds a deterministic ML-KEM-768
    // keypair; encapsulate a random DEK; AES-GCM the plaintext with that DEK.
    // Returns false when liboqs/Kyber is missing or simulated — callers must
    // write format v3 instead of pretending the wrap is post-quantum.
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
