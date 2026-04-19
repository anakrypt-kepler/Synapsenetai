#include "core/update_bundle.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>

namespace synapse::core {

namespace {

static constexpr size_t MAX_CHUNKS = 4096;
static constexpr uint64_t MAX_CHUNK_SIZE = 1024ULL * 1024ULL * 1024ULL;
static constexpr size_t MAX_TARGET_SIZE = 128;

static void writeU32LE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

static void writeU64LE(std::vector<uint8_t>& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

static bool readU32LE(const uint8_t*& p, const uint8_t* end, uint32_t& out) {
    if (static_cast<size_t>(end - p) < 4) return false;
    out = static_cast<uint32_t>(p[0]) |
          (static_cast<uint32_t>(p[1]) << 8) |
          (static_cast<uint32_t>(p[2]) << 16) |
          (static_cast<uint32_t>(p[3]) << 24);
    p += 4;
    return true;
}

static bool readU64LE(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
    if (static_cast<size_t>(end - p) < 8) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) {
        out |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    p += 8;
    return true;
}

static bool readBytes(const uint8_t*& p, const uint8_t* end, uint8_t* out, size_t n) {
    if (static_cast<size_t>(end - p) < n) return false;
    std::memcpy(out, p, n);
    p += n;
    return true;
}

static bool isTargetCharAllowed(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/' || c == ':';
}

static bool isValidTarget(const std::string& target) {
    if (target.empty() || target.size() > MAX_TARGET_SIZE) return false;
    for (unsigned char c : target) {
        if (!isTargetCharAllowed(c)) return false;
    }
    return true;
}

static bool chunkLess(const UpdateChunk& a, const UpdateChunk& b) {
    if (a.hash != b.hash) {
        return std::lexicographical_compare(a.hash.begin(), a.hash.end(), b.hash.begin(), b.hash.end());
    }
    return a.size < b.size;
}

static bool chunksCanonical(const std::vector<UpdateChunk>& chunks) {
    for (size_t i = 1; i < chunks.size(); ++i) {
        if (chunkLess(chunks[i], chunks[i - 1])) return false;
    }
    return true;
}

}

std::vector<uint8_t> UpdateManifest::payloadBytes() const {
    std::vector<uint8_t> out;
    out.reserve(4 + 32 + 32 + 4 + chunks.size() * (32 + 8) + 4 + target.size() + 4 + 4 + signer.size());

    writeU32LE(out, version);
    out.insert(out.end(), bundleId.begin(), bundleId.end());
    out.insert(out.end(), contentHash.begin(), contentHash.end());
    writeU32LE(out, static_cast<uint32_t>(chunks.size()));
    for (const auto& chunk : chunks) {
        out.insert(out.end(), chunk.hash.begin(), chunk.hash.end());
        writeU64LE(out, chunk.size);
    }
    writeU32LE(out, static_cast<uint32_t>(target.size()));
    out.insert(out.end(), target.begin(), target.end());
    writeU32LE(out, protocolMin);
    writeU32LE(out, protocolMax);
    out.insert(out.end(), signer.begin(), signer.end());
    return out;
}

crypto::Hash256 UpdateManifest::payloadHash() const {
    auto payload = payloadBytes();
    return crypto::sha256(payload.data(), payload.size());
}

std::vector<uint8_t> UpdateManifest::serialize() const {
    auto out = payloadBytes();
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

std::optional<UpdateManifest> UpdateManifest::deserialize(const std::vector<uint8_t>& data) {
    UpdateManifest manifest;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();

    if (!readU32LE(p, end, manifest.version)) return std::nullopt;
    if (!readBytes(p, end, manifest.bundleId.data(), manifest.bundleId.size())) return std::nullopt;
    if (!readBytes(p, end, manifest.contentHash.data(), manifest.contentHash.size())) return std::nullopt;

    uint32_t chunkCount = 0;
    if (!readU32LE(p, end, chunkCount)) return std::nullopt;
    if (chunkCount == 0 || chunkCount > MAX_CHUNKS) return std::nullopt;
    manifest.chunks.reserve(chunkCount);
    for (uint32_t i = 0; i < chunkCount; ++i) {
        UpdateChunk chunk;
        if (!readBytes(p, end, chunk.hash.data(), chunk.hash.size())) return std::nullopt;
        if (!readU64LE(p, end, chunk.size)) return std::nullopt;
        manifest.chunks.push_back(chunk);
    }

    uint32_t targetSize = 0;
    if (!readU32LE(p, end, targetSize)) return std::nullopt;
    if (targetSize == 0 || targetSize > MAX_TARGET_SIZE || static_cast<size_t>(end - p) < targetSize) return std::nullopt;
    manifest.target.assign(reinterpret_cast<const char*>(p), targetSize);
    p += targetSize;

    if (!readU32LE(p, end, manifest.protocolMin)) return std::nullopt;
    if (!readU32LE(p, end, manifest.protocolMax)) return std::nullopt;

    if (!readBytes(p, end, manifest.signer.data(), manifest.signer.size())) return std::nullopt;
    if (!readBytes(p, end, manifest.signature.data(), manifest.signature.size())) return std::nullopt;
    if (p != end) return std::nullopt;

    return manifest;
}

crypto::Hash256 UpdateManifest::computeContentHash(const std::vector<UpdateChunk>& chunks) {
    std::vector<UpdateChunk> sorted = chunks;
    std::sort(sorted.begin(), sorted.end(), chunkLess);

    std::vector<uint8_t> out;
    out.reserve(4 + sorted.size() * (32 + 8));
    writeU32LE(out, static_cast<uint32_t>(sorted.size()));
    for (const auto& chunk : sorted) {
        out.insert(out.end(), chunk.hash.begin(), chunk.hash.end());
        writeU64LE(out, chunk.size);
    }
    return crypto::sha256(out.data(), out.size());
}

crypto::Hash256 UpdateManifest::computeBundleId(
    uint32_t version,
    const crypto::Hash256& contentHash,
    const std::string& target,
    uint32_t protocolMin,
    uint32_t protocolMax,
    const crypto::PublicKey& signer
) {
    std::vector<uint8_t> out;
    const std::string tag = "synapsenet_update_bundle_v1";
    out.insert(out.end(), tag.begin(), tag.end());
    writeU32LE(out, version);
    out.insert(out.end(), contentHash.begin(), contentHash.end());
    writeU32LE(out, static_cast<uint32_t>(target.size()));
    out.insert(out.end(), target.begin(), target.end());
    writeU32LE(out, protocolMin);
    writeU32LE(out, protocolMax);
    out.insert(out.end(), signer.begin(), signer.end());
    return crypto::sha256(out.data(), out.size());
}

bool UpdateManifest::verifySignature(std::string* reason) const {
    if (!crypto::verify(payloadHash(), signature, signer)) {
        if (reason) *reason = "invalid_signature";
        return false;
    }
    return true;
}

bool UpdateManifest::validateStrict(std::string* reason) const {
    if (version != UPDATE_MANIFEST_VERSION_V1) {
        if (reason) *reason = "unsupported_version";
        return false;
    }
    if (chunks.empty() || chunks.size() > MAX_CHUNKS) {
        if (reason) *reason = "invalid_chunk_count";
        return false;
    }
    if (!isValidTarget(target)) {
        if (reason) *reason = "invalid_target";
        return false;
    }
    if (protocolMin == 0 || protocolMax < protocolMin) {
        if (reason) *reason = "invalid_protocol_range";
        return false;
    }
    if (!chunksCanonical(chunks)) {
        if (reason) *reason = "chunks_not_canonical";
        return false;
    }

    std::unordered_set<std::string> seenChunkHashes;
    seenChunkHashes.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        if (chunk.size == 0 || chunk.size > MAX_CHUNK_SIZE) {
            if (reason) *reason = "invalid_chunk_size";
            return false;
        }
        if (chunk.hash == crypto::Hash256{}) {
            if (reason) *reason = "invalid_chunk_hash";
            return false;
        }
        std::string key = crypto::toHex(chunk.hash);
        if (!seenChunkHashes.insert(key).second) {
            if (reason) *reason = "duplicate_chunk_hash";
            return false;
        }
    }

    crypto::Hash256 expectedContentHash = computeContentHash(chunks);
    if (expectedContentHash != contentHash) {
        if (reason) *reason = "content_hash_mismatch";
        return false;
    }

    crypto::Hash256 expectedBundleId = computeBundleId(version, contentHash, target, protocolMin, protocolMax, signer);
    if (expectedBundleId != bundleId) {
        if (reason) *reason = "bundle_id_mismatch";
        return false;
    }

    if (!verifySignature(reason)) return false;
    return true;
}

bool signUpdateManifest(UpdateManifest& manifest, const crypto::PrivateKey& signerKey, std::string* reason) {
    manifest.version = UPDATE_MANIFEST_VERSION_V1;
    std::sort(manifest.chunks.begin(), manifest.chunks.end(), chunkLess);
    manifest.chunks.erase(std::unique(manifest.chunks.begin(), manifest.chunks.end(), [](const UpdateChunk& a, const UpdateChunk& b) {
        return a.hash == b.hash && a.size == b.size;
    }), manifest.chunks.end());
    manifest.signer = crypto::derivePublicKey(signerKey);
    manifest.contentHash = UpdateManifest::computeContentHash(manifest.chunks);
    manifest.bundleId = UpdateManifest::computeBundleId(
        manifest.version,
        manifest.contentHash,
        manifest.target,
        manifest.protocolMin,
        manifest.protocolMax,
        manifest.signer
    );
    manifest.signature = crypto::sign(manifest.payloadHash(), signerKey);
    return manifest.validateStrict(reason);
}

}
