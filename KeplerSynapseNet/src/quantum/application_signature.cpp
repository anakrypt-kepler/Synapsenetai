#include "quantum/application_signature.h"
#include "crypto/crypto.h"
#include <cstring>
#include <sodium.h>

namespace synapse::quantum {

namespace {

constexpr uint32_t APPLICATION_SIGNATURE_MAGIC = 0x4B514153;
constexpr size_t CLASSIC_ED25519_PUBLIC_KEY_SIZE = 32;
constexpr size_t CLASSIC_ED25519_SIGNATURE_SIZE = 64;

void writeU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void writeU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

bool readU32(const uint8_t*& p, const uint8_t* end, uint32_t& out) {
    if (static_cast<size_t>(end - p) < 4) return false;
    out = (static_cast<uint32_t>(p[0]) << 24) |
          (static_cast<uint32_t>(p[1]) << 16) |
          (static_cast<uint32_t>(p[2]) << 8) |
          static_cast<uint32_t>(p[3]);
    p += 4;
    return true;
}

void writeField(std::vector<uint8_t>& out, const char* label, const std::vector<uint8_t>& value) {
    const size_t labelLen = std::strlen(label);
    if (labelLen > UINT16_MAX || value.size() > UINT32_MAX) return;
    writeU16(out, static_cast<uint16_t>(labelLen));
    out.insert(out.end(), label, label + labelLen);
    writeU32(out, static_cast<uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<uint8_t> toBytes(const std::string& value) {
    return std::vector<uint8_t>(value.begin(), value.end());
}

bool readBytes(const uint8_t*& p, const uint8_t* end, uint32_t size, std::vector<uint8_t>& out) {
    if (static_cast<size_t>(end - p) < size) return false;
    out.assign(p, p + size);
    p += size;
    return true;
}

HybridKeyPair publicOnly(const ApplicationSignatureEnvelope& envelope) {
    HybridKeyPair kp;
    kp.classicAlgo = CryptoAlgorithm::CLASSIC_ED25519;
    kp.pqcAlgo = CryptoAlgorithm::LATTICE_DILITHIUM65;
    kp.classicPublicKey = envelope.classicPublicKey;
    kp.pqcPublicKey = envelope.pqcPublicKey;
    return kp;
}

}

std::vector<uint8_t> ApplicationSignatureEnvelope::serialize() const {
    if (domain.size() > UINT32_MAX ||
        classicPublicKey.size() > UINT32_MAX ||
        pqcPublicKey.size() > UINT32_MAX ||
        signature.size() > UINT32_MAX) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve(24 + domain.size() + classicPublicKey.size() + pqcPublicKey.size() + signature.size());
    writeU32(out, APPLICATION_SIGNATURE_MAGIC);
    writeU32(out, suiteId);
    writeU32(out, static_cast<uint32_t>(domain.size()));
    out.insert(out.end(), domain.begin(), domain.end());
    writeU32(out, static_cast<uint32_t>(classicPublicKey.size()));
    out.insert(out.end(), classicPublicKey.begin(), classicPublicKey.end());
    writeU32(out, static_cast<uint32_t>(pqcPublicKey.size()));
    out.insert(out.end(), pqcPublicKey.begin(), pqcPublicKey.end());
    writeU32(out, static_cast<uint32_t>(signature.size()));
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

bool ApplicationSignatureEnvelope::deserialize(const std::vector<uint8_t>& data, ApplicationSignatureEnvelope& out) {
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();

    uint32_t magic = 0;
    if (!readU32(p, end, magic) || magic != APPLICATION_SIGNATURE_MAGIC) return false;
    if (!readU32(p, end, out.suiteId) || out.suiteId != APPLICATION_SIGNATURE_SUITE_ID) return false;

    uint32_t domainSize = 0;
    if (!readU32(p, end, domainSize)) return false;
    std::vector<uint8_t> domainBytes;
    if (!readBytes(p, end, domainSize, domainBytes)) return false;
    out.domain.assign(domainBytes.begin(), domainBytes.end());

    uint32_t classicSize = 0;
    if (!readU32(p, end, classicSize)) return false;
    if (!readBytes(p, end, classicSize, out.classicPublicKey)) return false;

    uint32_t pqcSize = 0;
    if (!readU32(p, end, pqcSize)) return false;
    if (!readBytes(p, end, pqcSize, out.pqcPublicKey)) return false;

    uint32_t signatureSize = 0;
    if (!readU32(p, end, signatureSize)) return false;
    if (!readBytes(p, end, signatureSize, out.signature)) return false;

    return p == end &&
           out.classicPublicKey.size() == CLASSIC_ED25519_PUBLIC_KEY_SIZE &&
           out.pqcPublicKey.size() == DILITHIUM_PUBLIC_KEY_SIZE &&
           out.signature.size() == CLASSIC_ED25519_SIGNATURE_SIZE + DILITHIUM_SIGNATURE_SIZE;
}

std::vector<uint8_t> buildApplicationSignatureTranscript(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const ApplicationSignatureEnvelope& envelope
) {
    if (envelope.suiteId != APPLICATION_SIGNATURE_SUITE_ID || envelope.domain != domain) return {};

    std::vector<uint8_t> out;
    const char label[] = "synapsenet-application-signature-v1";
    out.insert(out.end(), label, label + sizeof(label) - 1);
    writeU32(out, APPLICATION_SIGNATURE_SUITE_ID);
    writeField(out, "domain", toBytes(domain));
    writeField(out, "sig-suite", toBytes("ed25519+ml-dsa-65"));
    writeField(out, "classic-public-key", envelope.classicPublicKey);
    writeField(out, "pqc-public-key", envelope.pqcPublicKey);
    writeField(out, "binding", binding);
    writeField(out, "payload", payload);
    return out;
}

std::vector<uint8_t> signApplicationPayload(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const HybridKeyPair& keyPair
) {
    if (keyPair.classicPublicKey.size() != CLASSIC_ED25519_PUBLIC_KEY_SIZE ||
        keyPair.pqcPublicKey.size() != DILITHIUM_PUBLIC_KEY_SIZE) {
        return {};
    }

    ApplicationSignatureEnvelope envelope;
    envelope.domain = domain;
    envelope.classicPublicKey = keyPair.classicPublicKey;
    envelope.pqcPublicKey = keyPair.pqcPublicKey;

    auto transcript = buildApplicationSignatureTranscript(domain, payload, binding, envelope);
    if (transcript.empty()) return {};

    HybridSig signer;
    auto result = signer.sign(transcript, keyPair);
    if (!result.success) return {};
    envelope.signature = std::move(result.signature);
    return envelope.serialize();
}

bool verifyApplicationPayload(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const std::vector<uint8_t>& envelopeBytes
) {
    ApplicationSignatureEnvelope envelope;
    if (!ApplicationSignatureEnvelope::deserialize(envelopeBytes, envelope)) return false;
    auto transcript = buildApplicationSignatureTranscript(domain, payload, binding, envelope);
    if (transcript.empty()) return false;
    HybridSig verifier;
    return verifier.verify(transcript, envelope.signature, publicOnly(envelope));
}

bool applicationSignatureIdentityIdFromPublicKey(const HybridKeyPair& publicKey, std::string& out) {
    if (publicKey.classicPublicKey.size() != CLASSIC_ED25519_PUBLIC_KEY_SIZE ||
        publicKey.pqcPublicKey.size() != DILITHIUM_PUBLIC_KEY_SIZE) {
        return false;
    }
    const auto classicDigest = crypto::sha256(publicKey.classicPublicKey.data(), publicKey.classicPublicKey.size());
    const auto pqcDigest = crypto::sha256(publicKey.pqcPublicKey.data(), publicKey.pqcPublicKey.size());
    out = crypto::toHex(classicDigest);
    out.push_back(':');
    out += crypto::toHex(pqcDigest);
    return true;
}

bool applicationSignatureIdentityId(const std::vector<uint8_t>& envelopeBytes, std::string& out) {
    ApplicationSignatureEnvelope envelope;
    if (!ApplicationSignatureEnvelope::deserialize(envelopeBytes, envelope)) {
        return false;
    }
    return applicationSignatureIdentityIdFromPublicKey(publicOnly(envelope), out);
}

bool isApplicationSignatureEnvelope(const std::vector<uint8_t>& data) {
    ApplicationSignatureEnvelope envelope;
    return ApplicationSignatureEnvelope::deserialize(data, envelope);
}

}
