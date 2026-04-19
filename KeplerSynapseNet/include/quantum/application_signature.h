#pragma once

#include "quantum/quantum_security.h"
#include <cstdint>
#include <string>
#include <vector>

namespace synapse::quantum {

constexpr uint32_t APPLICATION_SIGNATURE_SUITE_ID = 0x53505132;

struct ApplicationSignatureEnvelope {
    uint32_t suiteId = APPLICATION_SIGNATURE_SUITE_ID;
    std::string domain;
    std::vector<uint8_t> mlDsaPublicKey;
    std::vector<uint8_t> slhDsaPublicKey;
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
    const std::vector<uint8_t>& binding
);

std::vector<uint8_t> signApplicationPayload(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const HybridKeyPair& keyPair
);

bool isRevokedReleaseAuthorityPublicKey(const HybridKeyPair& publicKey);
bool isTrustedReleaseAuthorityPublicKey(const HybridKeyPair& publicKey);

bool verifyApplicationPayload(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const std::vector<uint8_t>& envelopeBytes
);

bool verifyApplicationPayloadWithTrustedReleaseAuthorities(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const std::vector<uint8_t>& envelopeBytes
);

bool applicationSignatureIdentityId(const std::vector<uint8_t>& envelopeBytes, std::string& out);
bool applicationSignatureIdentityIdFromPublicKey(const HybridKeyPair& publicKey, std::string& out);

bool isApplicationSignatureEnvelope(const std::vector<uint8_t>& data);

bool loadHybridKeyPairFromFile(const std::string& path, HybridKeyPair& out);
bool loadReleaseAuthorityKeyPair(HybridKeyPair& out, std::string* resolvedPath = nullptr);

}
