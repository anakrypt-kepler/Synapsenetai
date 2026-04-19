#include "core/update_bundle.h"
#include "crypto/crypto.h"
#include "infrastructure/messages.h"
#include <cassert>
#include <string>
#include <vector>

static synapse::core::UpdateChunk makeChunk(const std::string& seed, uint64_t size) {
    synapse::core::UpdateChunk chunk;
    chunk.hash = synapse::crypto::sha256(seed);
    chunk.size = size;
    return chunk;
}

static void testManifestRoundTripAndValidation() {
    auto keyPair = synapse::crypto::generateKeyPair();

    synapse::core::UpdateManifest manifest;
    manifest.chunks = {
        makeChunk("chunk_a", 1024),
        makeChunk("chunk_b", 4096)
    };
    manifest.target = "implant/driver";
    manifest.protocolMin = 1;
    manifest.protocolMax = 2;

    std::string reason;
    assert(synapse::core::signUpdateManifest(manifest, keyPair.privateKey, &reason));
    assert(manifest.validateStrict(&reason));
    assert(manifest.verifySignature(&reason));

    auto bytes = manifest.serialize();
    auto decoded = synapse::core::UpdateManifest::deserialize(bytes);
    assert(decoded.has_value());
    assert(decoded->validateStrict(&reason));
}

static void testRejectUnsupportedVersion() {
    auto keyPair = synapse::crypto::generateKeyPair();

    synapse::core::UpdateManifest manifest;
    manifest.chunks = {
        makeChunk("chunk_c", 2048)
    };
    manifest.target = "implant/app";
    manifest.protocolMin = 1;
    manifest.protocolMax = 1;
    assert(synapse::core::signUpdateManifest(manifest, keyPair.privateKey, nullptr));

    manifest.version = 2;
    std::string reason;
    assert(!manifest.validateStrict(&reason));
    assert(reason == "unsupported_version");
}

static void testRejectBrokenHashAndInvalidSignature() {
    auto keyPair = synapse::crypto::generateKeyPair();

    synapse::core::UpdateManifest manifest;
    manifest.chunks = {
        makeChunk("chunk_d", 3072),
        makeChunk("chunk_e", 5120)
    };
    manifest.target = "implant/driver";
    manifest.protocolMin = 2;
    manifest.protocolMax = 3;
    assert(synapse::core::signUpdateManifest(manifest, keyPair.privateKey, nullptr));

    synapse::core::UpdateManifest badHash = manifest;
    badHash.contentHash[0] ^= 0x01;
    std::string reason;
    assert(!badHash.validateStrict(&reason));
    assert(reason == "content_hash_mismatch");

    synapse::core::UpdateManifest badSig = manifest;
    badSig.signature[0] ^= 0x01;
    assert(!badSig.validateStrict(&reason));
    assert(reason == "invalid_signature");
}

static void testRejectInvalidProtocolRangeAndTarget() {
    auto keyPair = synapse::crypto::generateKeyPair();

    synapse::core::UpdateManifest manifest;
    manifest.chunks = {
        makeChunk("chunk_protocol_a", 1024),
        makeChunk("chunk_protocol_b", 2048)
    };
    manifest.target = "implant/driver";
    manifest.protocolMin = 2;
    manifest.protocolMax = 3;
    assert(synapse::core::signUpdateManifest(manifest, keyPair.privateKey, nullptr));

    synapse::core::UpdateManifest badRange = manifest;
    badRange.protocolMin = 5;
    badRange.protocolMax = 4;
    std::string reason;
    assert(!badRange.validateStrict(&reason));
    assert(reason == "invalid_protocol_range");

    synapse::core::UpdateManifest badTarget = manifest;
    badTarget.target = "implant/driver beta";
    assert(!badTarget.validateStrict(&reason));
    assert(reason == "invalid_target");
}

static void testUpdateBundleInventoryRoundTrip() {
    auto keyPair = synapse::crypto::generateKeyPair();

    synapse::core::UpdateManifest manifest;
    manifest.chunks = {
        makeChunk("chunk_inventory_a", 1024),
        makeChunk("chunk_inventory_b", 2048)
    };
    manifest.target = "implant/driver";
    manifest.protocolMin = 1;
    manifest.protocolMax = 2;
    assert(synapse::core::signUpdateManifest(manifest, keyPair.privateKey, nullptr));

    synapse::InvItem invItem;
    invItem.type = synapse::InvType::UPDATE_BUNDLE;
    invItem.hash = manifest.bundleId;

    synapse::InvMessage inv;
    inv.items.push_back(invItem);
    auto invBytes = inv.serialize();
    auto invDecoded = synapse::InvMessage::deserialize(invBytes);
    assert(invDecoded.items.size() == 1);
    assert(invDecoded.items[0].type == synapse::InvType::UPDATE_BUNDLE);
    assert(invDecoded.items[0].hash == manifest.bundleId);

    synapse::GetDataMessage getData;
    getData.items.push_back(invItem);
    auto getDataBytes = getData.serialize();
    auto getDataDecoded = synapse::GetDataMessage::deserialize(getDataBytes);
    assert(getDataDecoded.items.size() == 1);
    assert(getDataDecoded.items[0].type == synapse::InvType::UPDATE_BUNDLE);
    assert(getDataDecoded.items[0].hash == manifest.bundleId);
}

int main() {
    testManifestRoundTripAndValidation();
    testRejectUnsupportedVersion();
    testRejectBrokenHashAndInvalidSignature();
    testRejectInvalidProtocolRangeAndTarget();
    testUpdateBundleInventoryRoundTrip();
    return 0;
}
