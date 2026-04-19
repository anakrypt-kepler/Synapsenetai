#pragma once

#include "quantum_security.h"
#include <vector>

namespace synapse {
namespace quantum {

class ModelInferenceCrypto {
public:
    ModelInferenceCrypto();
    
    void setSecurityLevel(SecurityLevel level);
    SecurityLevel getSecurityLevel() const;
    
    std::vector<uint8_t> encryptRequest(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    std::vector<uint8_t> decryptRequest(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    std::vector<uint8_t> encryptResponse(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    std::vector<uint8_t> decryptResponse(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    
private:
    SecurityLevel level_ = SecurityLevel::STANDARD;
};

}
}
