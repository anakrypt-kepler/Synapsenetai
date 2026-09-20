#pragma once

#include "quantum/quantum_security.h"
#include <cstdint>
#include <string>
#include <vector>

// On-wire PQ envelope: magic KQAS, domain-separated so a transfer sig cannot
// be replayed as a consensus vote. Bytes are classic_sig || pqc_sig (AND-mode
// Ed25519 + ML-DSA-65). Handshake/NODE_MSG do not use this trailer.

namespace synapse::quantum {

constexpr uint32_t APPLICATION_SIGNATURE_SUITE_ID = 0x53505131;

struct ApplicationSignatureEnvelope {
    uint32_t suiteId = APPLICATION_SIGNATURE_SUITE_ID;
    std::string domain;
    std::vector<uint8_t> classicPublicKey;
    std::vector<uint8_t> pqcPublicKey;
    std::vector<uint8_t> signature;

    std::vector<uint8_t> serialize() const;
    static bool deserialize(const std::vector<uint8_t>& data, ApplicationSignatureEnvelope& out);
};

std::vector<uint8_t> buildApplicationSignatureTranscript(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const ApplicationSignatureEnvelope& envelope
);

std::vector<uint8_t> signApplicationPayload(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const HybridKeyPair& keyPair
);

bool verifyApplicationPayload(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const std::vector<uint8_t>& envelopeBytes
);

bool applicationSignatureIdentityId(const std::vector<uint8_t>& envelopeBytes, std::string& out);
bool applicationSignatureIdentityIdFromPublicKey(const HybridKeyPair& publicKey, std::string& out);

bool isApplicationSignatureEnvelope(const std::vector<uint8_t>& data);

}
