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

private:
    SecurityLevel level_ = SecurityLevel::STANDARD;
    QuantumManager* qm_ = nullptr;
    HybridKEM* hybridKEM_ = nullptr;
};

}
}
