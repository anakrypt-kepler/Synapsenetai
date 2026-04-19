#include "quantum/application_signature.h"
#include "crypto/crypto.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <nlohmann/json.hpp>

namespace synapse::quantum {

namespace {

using json = nlohmann::json;

struct TrustedReleaseAuthorityDigest {
    std::string_view primary;
    std::string_view secondary;
};

constexpr uint32_t APPLICATION_SIGNATURE_MAGIC = 0x4B514153;
constexpr const char* RELEASE_AUTHORITY_KEY_ENV = "SYNAPSENET_RELEASE_AUTHORITY_KEY_FILE";
constexpr const char* DEFAULT_RELEASE_AUTHORITY_RELATIVE_PATH = ".synapsenet/release_authority.json";
constexpr std::array<TrustedReleaseAuthorityDigest, 1> kTrustedProductionReleaseAuthorities {{
    {
        "4546425b5c1afa66a54a5c7928cec2ecf42e8ed4fee1f207ba3c15975c909375",
        "158e72d2449b06f0c22215d96b6b4c0f99280f558a9c63076ce3fa0a5ec6f748"
    }
}};
constexpr std::array<TrustedReleaseAuthorityDigest, 1> kRevokedProductionReleaseAuthorities {{
    {
        "e8831fd8fb075e5edb5ebbc1551356c3e501bf42a312a9dc9115c879a7af9461",
        "7da109f3305daebf2715b12ded62daccb0f290370f49a926c959b7adeef552ea"
    }
}};

#if defined(SYNAPSE_BUILD_TESTS) && SYNAPSE_BUILD_TESTS
constexpr std::array<TrustedReleaseAuthorityDigest, 1> kTrustedTestReleaseAuthorities {{
    {
        "3d60cd15938ca594432596f1c8405242a9774db24aafbbca77493ef208de8c08",
        "cc40371d9c16531384d5116b9dd1ab02f60338a081ba5d6f152a6ec1a1e650d0"
    }
}};
#endif

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
    HybridKeyPair keyPair;
    keyPair.primaryAlgo = CryptoAlgorithm::SIG_ML_DSA_87;
    keyPair.secondaryAlgo = CryptoAlgorithm::SIG_SLH_DSA_SHAKE_256S;
    keyPair.primaryPublicKey = envelope.mlDsaPublicKey;
    keyPair.secondaryPublicKey = envelope.slhDsaPublicKey;
    return keyPair;
}

template <size_t N, size_t M>
constexpr bool authoritySetsAreDisjoint(
    const std::array<TrustedReleaseAuthorityDigest, N>& left,
    const std::array<TrustedReleaseAuthorityDigest, M>& right
) {
    for (const auto& leftAuthority : left) {
        for (const auto& rightAuthority : right) {
            if (leftAuthority.primary == rightAuthority.primary ||
                leftAuthority.secondary == rightAuthority.secondary) {
                return false;
            }
        }
    }
    return true;
}

static_assert(
    authoritySetsAreDisjoint(kTrustedProductionReleaseAuthorities, kRevokedProductionReleaseAuthorities),
    "production release authority digests must be distinct from revoked digests");

#if defined(SYNAPSE_BUILD_TESTS) && SYNAPSE_BUILD_TESTS
static_assert(
    authoritySetsAreDisjoint(kTrustedProductionReleaseAuthorities, kTrustedTestReleaseAuthorities),
    "production and test release authority digests must be distinct");
static_assert(
    authoritySetsAreDisjoint(kTrustedTestReleaseAuthorities, kRevokedProductionReleaseAuthorities),
    "test and revoked release authority digests must be distinct");
#endif

bool matchesTrustedDigest(const std::vector<uint8_t>& publicKey, std::string_view expectedDigestHex) {
    if (publicKey.empty()) {
        return false;
    }
    const auto digest = crypto::sha256(publicKey.data(), publicKey.size());
    return crypto::toHex(digest) == expectedDigestHex;
}

bool matchesTrustedAuthorityDigest(
    const HybridKeyPair& publicKey,
    const TrustedReleaseAuthorityDigest& digest
) {
    return matchesTrustedDigest(publicKey.primaryPublicKey, digest.primary) &&
           matchesTrustedDigest(publicKey.secondaryPublicKey, digest.secondary);
}

template <size_t N>
bool matchesTrustedAuthoritySet(
    const HybridKeyPair& publicKey,
    const std::array<TrustedReleaseAuthorityDigest, N>& trustedAuthorities
) {
    return std::any_of(
        trustedAuthorities.begin(),
        trustedAuthorities.end(),
        [&](const TrustedReleaseAuthorityDigest& digest) {
            return matchesTrustedAuthorityDigest(publicKey, digest);
        });
}

bool loadKeyComponent(
    const json& document,
    const char* primaryField,
    const char* aliasField,
    size_t expectedSize,
    std::vector<uint8_t>& out
) {
    std::string hexValue;
    if (document.contains(primaryField) && document.at(primaryField).is_string()) {
        hexValue = document.at(primaryField).get<std::string>();
    } else if (aliasField && document.contains(aliasField) && document.at(aliasField).is_string()) {
        hexValue = document.at(aliasField).get<std::string>();
    } else {
        return false;
    }
    out = crypto::fromHex(hexValue);
    return out.size() == expectedSize;
}

std::string defaultReleaseAuthorityPath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        return {};
    }
    return (std::filesystem::path(home) / DEFAULT_RELEASE_AUTHORITY_RELATIVE_PATH).string();
}

}

std::vector<uint8_t> ApplicationSignatureEnvelope::serialize() const {
    if (domain.size() > UINT32_MAX ||
        mlDsaPublicKey.size() > UINT32_MAX ||
        slhDsaPublicKey.size() > UINT32_MAX ||
        signature.size() > UINT32_MAX) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve(24 + domain.size() + mlDsaPublicKey.size() + slhDsaPublicKey.size() + signature.size());
    writeU32(out, APPLICATION_SIGNATURE_MAGIC);
    writeU32(out, suiteId);
    writeU32(out, static_cast<uint32_t>(domain.size()));
    out.insert(out.end(), domain.begin(), domain.end());
    writeU32(out, static_cast<uint32_t>(mlDsaPublicKey.size()));
    out.insert(out.end(), mlDsaPublicKey.begin(), mlDsaPublicKey.end());
    writeU32(out, static_cast<uint32_t>(slhDsaPublicKey.size()));
    out.insert(out.end(), slhDsaPublicKey.begin(), slhDsaPublicKey.end());
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

    uint32_t mlDsaSize = 0;
    if (!readU32(p, end, mlDsaSize)) return false;
    if (!readBytes(p, end, mlDsaSize, out.mlDsaPublicKey)) return false;

    uint32_t slhDsaSize = 0;
    if (!readU32(p, end, slhDsaSize)) return false;
    if (!readBytes(p, end, slhDsaSize, out.slhDsaPublicKey)) return false;

    uint32_t signatureSize = 0;
    if (!readU32(p, end, signatureSize)) return false;
    if (!readBytes(p, end, signatureSize, out.signature)) return false;

    return p == end &&
           out.mlDsaPublicKey.size() == DILITHIUM_PUBLIC_KEY_SIZE &&
           out.slhDsaPublicKey.size() == SPHINCS_PUBLIC_KEY_SIZE &&
           out.signature.size() == DILITHIUM_SIGNATURE_SIZE + SPHINCS_SIGNATURE_SIZE;
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
    writeField(out, "sig-suite", toBytes("ml-dsa-87+slh-dsa-shake-256s"));
    writeField(out, "ml-dsa-87-public-key", envelope.mlDsaPublicKey);
    writeField(out, "slh-dsa-shake-256s-public-key", envelope.slhDsaPublicKey);
    writeField(out, "binding", binding);
    writeField(out, "payload", payload);
    return out;
}

std::vector<uint8_t> signApplicationPayload(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding
) {
    HybridKeyPair keyPair;
    if (!loadReleaseAuthorityKeyPair(keyPair, nullptr)) {
        return {};
    }
    return signApplicationPayload(domain, payload, binding, keyPair);
}

std::vector<uint8_t> signApplicationPayload(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const HybridKeyPair& keyPair
) {
    ApplicationSignatureEnvelope envelope;
    envelope.domain = domain;
    envelope.mlDsaPublicKey = keyPair.primaryPublicKey;
    envelope.slhDsaPublicKey = keyPair.secondaryPublicKey;

    auto transcript = buildApplicationSignatureTranscript(domain, payload, binding, envelope);
    if (transcript.empty()) return {};

    HybridSig signer;
    auto result = signer.sign(transcript, keyPair);
    if (!result.success) return {};
    envelope.signature = std::move(result.signature);
    return envelope.serialize();
}

bool isRevokedReleaseAuthorityPublicKey(const HybridKeyPair& publicKey) {
    return publicKey.primaryPublicKey.size() == DILITHIUM_PUBLIC_KEY_SIZE &&
           publicKey.secondaryPublicKey.size() == SPHINCS_PUBLIC_KEY_SIZE &&
           matchesTrustedAuthoritySet(publicKey, kRevokedProductionReleaseAuthorities);
}

bool isTrustedReleaseAuthorityPublicKey(const HybridKeyPair& publicKey) {
    if (publicKey.primaryPublicKey.size() != DILITHIUM_PUBLIC_KEY_SIZE ||
        publicKey.secondaryPublicKey.size() != SPHINCS_PUBLIC_KEY_SIZE) {
        return false;
    }
    if (isRevokedReleaseAuthorityPublicKey(publicKey)) {
        return false;
    }
    if (matchesTrustedAuthoritySet(publicKey, kTrustedProductionReleaseAuthorities)) {
        return true;
    }
#if defined(SYNAPSE_BUILD_TESTS) && SYNAPSE_BUILD_TESTS
    if (matchesTrustedAuthoritySet(publicKey, kTrustedTestReleaseAuthorities)) {
        return true;
    }
#endif
    return false;
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

bool verifyApplicationPayloadWithTrustedReleaseAuthorities(
    const std::string& domain,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binding,
    const std::vector<uint8_t>& envelopeBytes
) {
    ApplicationSignatureEnvelope envelope;
    if (!ApplicationSignatureEnvelope::deserialize(envelopeBytes, envelope)) return false;
    const auto envelopePublicKey = publicOnly(envelope);
    if (!isTrustedReleaseAuthorityPublicKey(envelopePublicKey)) return false;
    auto transcript = buildApplicationSignatureTranscript(domain, payload, binding, envelope);
    if (transcript.empty()) return false;
    HybridSig verifier;
    return verifier.verify(transcript, envelope.signature, envelopePublicKey);
}

bool applicationSignatureIdentityIdFromPublicKey(const HybridKeyPair& publicKey, std::string& out) {
    if (publicKey.primaryPublicKey.size() != DILITHIUM_PUBLIC_KEY_SIZE ||
        publicKey.secondaryPublicKey.size() != SPHINCS_PUBLIC_KEY_SIZE) {
        return false;
    }
    const auto primaryDigest = crypto::sha256(publicKey.primaryPublicKey.data(), publicKey.primaryPublicKey.size());
    const auto secondaryDigest = crypto::sha256(publicKey.secondaryPublicKey.data(), publicKey.secondaryPublicKey.size());
    out = crypto::toHex(primaryDigest);
    out.push_back(':');
    out += crypto::toHex(secondaryDigest);
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

bool loadHybridKeyPairFromFile(const std::string& path, HybridKeyPair& out) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    const std::string text(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    json document = json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return false;
    }

    HybridKeyPair keyPair;
    keyPair.primaryAlgo = CryptoAlgorithm::SIG_ML_DSA_87;
    keyPair.secondaryAlgo = CryptoAlgorithm::SIG_SLH_DSA_SHAKE_256S;
    if (!loadKeyComponent(document, "primaryPublicKey", "mlDsaPublicKey", DILITHIUM_PUBLIC_KEY_SIZE, keyPair.primaryPublicKey)) {
        return false;
    }
    if (!loadKeyComponent(document, "primarySecretKey", "mlDsaSecretKey", DILITHIUM_SECRET_KEY_SIZE, keyPair.primarySecretKey)) {
        return false;
    }
    if (!loadKeyComponent(document, "secondaryPublicKey", "slhDsaPublicKey", SPHINCS_PUBLIC_KEY_SIZE, keyPair.secondaryPublicKey)) {
        return false;
    }
    if (!loadKeyComponent(document, "secondarySecretKey", "slhDsaSecretKey", SPHINCS_SECRET_KEY_SIZE, keyPair.secondarySecretKey)) {
        return false;
    }

    out = std::move(keyPair);
    return true;
}

bool loadReleaseAuthorityKeyPair(HybridKeyPair& out, std::string* resolvedPath) {
    const char* envPath = std::getenv(RELEASE_AUTHORITY_KEY_ENV);
    std::string path = (envPath && *envPath) ? std::string(envPath) : defaultReleaseAuthorityPath();
    if (path.empty()) {
        return false;
    }
    if (!loadHybridKeyPairFromFile(path, out)) {
        return false;
    }
    if (resolvedPath) {
        *resolvedPath = path;
    }
    return true;
}

}
