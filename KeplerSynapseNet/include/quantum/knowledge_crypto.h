#pragma once

#include "quantum_security.h"
#include <vector>
#include <string>

namespace synapse {
namespace quantum {

class KnowledgeCrypto {
public:
    KnowledgeCrypto();
    
    void setSecurityLevel(SecurityLevel level);
    SecurityLevel getSecurityLevel() const;
    
    std::vector<uint8_t> deriveKey(const std::vector<uint8_t>& seed, const std::string& context) const;
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    
private:
    SecurityLevel level_ = SecurityLevel::STANDARD;
};

}
}
