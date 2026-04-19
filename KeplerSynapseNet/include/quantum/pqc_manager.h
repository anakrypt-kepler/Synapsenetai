#pragma once

#include "quantum_security.h"
#include <memory>
#include <vector>

namespace synapse {
namespace quantum {

class PQCManager {
public:
    PQCManager();
    ~PQCManager();
    
    bool init(SecurityLevel level);
    void shutdown();
    SecurityLevel getSecurityLevel() const;
    
    KyberKeyPair generateKyberKeyPair();
    DilithiumKeyPair generateDilithiumKeyPair();
    SphincsKeyPair generateSphincsKeyPair();
    HybridKeyPair generateHybridKeyPair();
    
    EncapsulationResult encapsulateHybrid(const HybridKeyPair& recipientKey);
    std::vector<uint8_t> decapsulateHybrid(const std::vector<uint8_t>& ciphertext, const HybridKeyPair& keyPair);
    
    SignatureResult signDilithium(const std::vector<uint8_t>& message,
                                  const DilithiumSecretKey& secretKey);
    bool verifyDilithium(const std::vector<uint8_t>& message,
                         const DilithiumSignature& signature,
                         const DilithiumPublicKey& publicKey);
    
    SignatureResult signSphincs(const std::vector<uint8_t>& message,
                                const SphincsSecretKey& secretKey);
    bool verifySphincs(const std::vector<uint8_t>& message,
                       const SphincsSignature& signature,
                       const SphincsPublicKey& publicKey);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
