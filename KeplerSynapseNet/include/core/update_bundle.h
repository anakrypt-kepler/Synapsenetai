#pragma once

#include "crypto/crypto.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace synapse::core {

static constexpr uint32_t UPDATE_MANIFEST_VERSION_V1 = 1;

struct UpdateChunk {
    crypto::Hash256 hash{};
    uint64_t size = 0;
};

struct UpdateManifest {
    uint32_t version = UPDATE_MANIFEST_VERSION_V1;
    crypto::Hash256 bundleId{};
    crypto::Hash256 contentHash{};
    std::vector<UpdateChunk> chunks;
    std::string target;
    uint32_t protocolMin = 1;
    uint32_t protocolMax = 1;
    crypto::PublicKey signer{};
    crypto::Signature signature{};
    std::vector<std::pair<crypto::PublicKey, crypto::Signature>> additionalSignatures;

    std::vector<uint8_t> payloadBytes() const;
    crypto::Hash256 payloadHash() const;
    std::vector<uint8_t> serialize() const;
    static std::optional<UpdateManifest> deserialize(const std::vector<uint8_t>& data);

    static crypto::Hash256 computeContentHash(const std::vector<UpdateChunk>& chunks);
    static crypto::Hash256 computeBundleId(
        uint32_t version,
        const crypto::Hash256& contentHash,
        const std::string& target,
        uint32_t protocolMin,
        uint32_t protocolMax,
        const crypto::PublicKey& signer
    );

    bool verifySignature(std::string* reason = nullptr) const;
    bool validateStrict(std::string* reason = nullptr) const;
};

bool signUpdateManifest(UpdateManifest& manifest, const crypto::PrivateKey& signerKey, std::string* reason = nullptr);

}
