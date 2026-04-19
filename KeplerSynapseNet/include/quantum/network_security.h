#pragma once

#include "quantum_security.h"
#include <vector>

namespace synapse {
namespace quantum {

class NetworkSecurity {
public:
    NetworkSecurity();
    
    void setSecurityLevel(SecurityLevel level);
    SecurityLevel getSecurityLevel() const;
    
    std::vector<uint8_t> deriveSessionKey(const std::vector<uint8_t>& sharedSecret,
                                          const std::vector<uint8_t>& nonce) const;
    std::vector<uint8_t> encryptPacket(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    std::vector<uint8_t> decryptPacket(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;
    
private:
    SecurityLevel level_ = SecurityLevel::STANDARD;
};

}
}
