#include "core/poe_v1_objects.h"
#include <algorithm>
#include <cstring>

namespace synapse::core::poe_v1 {

static void writeU8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

static void writeU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

static void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void writeU64LE(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

static uint8_t readU8(const uint8_t*& p, const uint8_t* end, bool& ok) {
    if (p + 1 > end) { ok = false; return 0; }
    return *p++;
}

static uint16_t readU16LE(const uint8_t*& p, const uint8_t* end, bool& ok) {
    if (p + 2 > end) { ok = false; return 0; }
    uint16_t v = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    p += 2;
    return v;
}

static uint32_t readU32LE(const uint8_t*& p, const uint8_t* end, bool& ok) {
    if (p + 4 > end) { ok = false; return 0; }
    uint32_t v = static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
    p += 4;
    return v;
}

static uint64_t readU64LE(const uint8_t*& p, const uint8_t* end, bool& ok) {
    if (p + 8 > end) { ok = false; return 0; }
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    p += 8;
    return v;
}

static void writeVarBytes(std::vector<uint8_t>& out, const std::string& s) {
    writeU32LE(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

static std::optional<std::string> readVarBytesString(const uint8_t*& p, const uint8_t* end, bool& ok) {
    uint32_t len = readU32LE(p, end, ok);
    if (!ok) return std::nullopt;
    if (p + len > end) { ok = false; return std::nullopt; }
    std::string s(reinterpret_cast<const char*>(p), len);
    p += len;
    return s;
}

std::vector<uint8_t> KnowledgeEntryV1::canonicalBodyBytes() const {
    std::vector<uint8_t> out;
    out.reserve(128 + title.size() + body.size() + citations.size() * 32);

    writeU8(out, version);
    writeU64LE(out, timestamp);
    out.insert(out.end(), authorPubKey.begin(), authorPubKey.end());
    writeU8(out, static_cast<uint8_t>(contentType));
    writeVarBytes(out, canonicalizeText(title));
    if (contentType == ContentType::CODE) {
        writeVarBytes(out, canonicalizeCode(body));
    } else {
        writeVarBytes(out, canonicalizeText(body));
    }
    writeU32LE(out, static_cast<uint32_t>(citations.size()));
    for (const auto& c : citations) out.insert(out.end(), c.begin(), c.end());
    writeU32LE(out, powBits);
    return out;
}

crypto::Hash256 KnowledgeEntryV1::contentId() const {
    auto bodyBytes = canonicalBodyBytes();
    return crypto::sha256(bodyBytes.data(), bodyBytes.size());
}

crypto::Hash256 KnowledgeEntryV1::bodyFingerprint() const {
    std::vector<uint8_t> out;
    out.reserve(64 + title.size() + body.size() + citations.size() * 32);
    out.insert(out.end(), authorPubKey.begin(), authorPubKey.end());
    writeU8(out, static_cast<uint8_t>(contentType));
    writeVarBytes(out, canonicalizeText(title));
    if (contentType == ContentType::CODE) {
        writeVarBytes(out, canonicalizeCode(body));
    } else {
        writeVarBytes(out, canonicalizeText(body));
    }
    writeU32LE(out, static_cast<uint32_t>(citations.size()));
    for (const auto& c : citations) out.insert(out.end(), c.begin(), c.end());
    return crypto::sha256(out.data(), out.size());
}

crypto::Hash256 KnowledgeEntryV1::submitId() const {
    crypto::Hash256 cid = contentId();
    std::vector<uint8_t> buf;
    buf.insert(buf.end(), cid.begin(), cid.end());
    writeU64LE(buf, powNonce);
    return crypto::sha256(buf.data(), buf.size());
}

crypto::Hash256 KnowledgeEntryV1::signatureHash() const {
    auto bodyBytes = canonicalBodyBytes();
    writeU64LE(bodyBytes, powNonce);
    return crypto::sha256(bodyBytes.data(), bodyBytes.size());
}

uint64_t KnowledgeEntryV1::contentSimhash64() const {
    std::string canon = canonicalizeText(title);
    if (!canon.empty()) canon.push_back('\n');
    if (contentType == ContentType::CODE) {
        canon += canonicalizeCodeForSimhash(body);
    } else {
        canon += canonicalizeText(body);
    }
    return simhash64(canon);
}

bool KnowledgeEntryV1::checkLimits(const LimitsV1& limits, std::string* reason) const {
    if (version != 1) {
        if (reason) *reason = "unsupported_version";
        return false;
    }
    if (title.empty() || body.empty()) {
        if (reason) *reason = "empty_fields";
        return false;
    }
    if (title.size() > limits.maxTitleBytes || body.size() > limits.maxBodyBytes) {
        if (reason) *reason = "too_large";
        return false;
    }
    if (citations.size() > limits.maxCitations) {
        if (reason) *reason = "too_many_citations";
        return false;
    }
    if (powBits < limits.minPowBits || powBits > limits.maxPowBits) {
        if (reason) *reason = "bad_pow_bits";
        return false;
    }
    return true;
}

bool KnowledgeEntryV1::verifyPoW(std::string* reason) const {
    if (!hasLeadingZeroBits(submitId(), powBits)) {
        if (reason) *reason = "pow_failed";
        return false;
    }
    return true;
}

bool KnowledgeEntryV1::verifySignature(std::string* reason) const {
    crypto::Hash256 h = signatureHash();
    if (!crypto::verify(h, authorSig, authorPubKey)) {
        if (reason) *reason = "sig_failed";
        return false;
    }
    return true;
}

bool KnowledgeEntryV1::verifyAll(const LimitsV1& limits, std::string* reason) const {
    if (!checkLimits(limits, reason)) return false;
    if (!verifyPoW(reason)) return false;
    if (!verifySignature(reason)) return false;
    return true;
}

std::vector<uint8_t> KnowledgeEntryV1::serialize() const {
    std::vector<uint8_t> out;
    out.reserve(256 + title.size() + body.size() + citations.size() * 32);
    writeU8(out, version);
    writeU64LE(out, timestamp);
    out.insert(out.end(), authorPubKey.begin(), authorPubKey.end());
    writeU8(out, static_cast<uint8_t>(contentType));
    writeVarBytes(out, title);
    writeVarBytes(out, body);
    writeU32LE(out, static_cast<uint32_t>(citations.size()));
    for (const auto& c : citations) out.insert(out.end(), c.begin(), c.end());
    writeU64LE(out, powNonce);
    writeU32LE(out, powBits);
    out.insert(out.end(), authorSig.begin(), authorSig.end());
    return out;
}

std::optional<KnowledgeEntryV1> KnowledgeEntryV1::deserialize(const std::vector<uint8_t>& data) {
    KnowledgeEntryV1 e;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();

    e.version = readU8(p, end, ok);
    e.timestamp = readU64LE(p, end, ok);
    if (!ok) return std::nullopt;

    if (p + e.authorPubKey.size() > end) return std::nullopt;
    std::memcpy(e.authorPubKey.data(), p, e.authorPubKey.size());
    p += e.authorPubKey.size();

    e.contentType = static_cast<ContentType>(readU8(p, end, ok));
    if (!ok) return std::nullopt;

    auto title = readVarBytesString(p, end, ok);
    auto body = readVarBytesString(p, end, ok);
    if (!ok || !title || !body) return std::nullopt;
    e.title = *title;
    e.body = *body;

    uint32_t citeCount = readU32LE(p, end, ok);
    if (!ok) return std::nullopt;
    if (citeCount > 100000) return std::nullopt;
    e.citations.clear();
    e.citations.reserve(citeCount);
    for (uint32_t i = 0; i < citeCount; ++i) {
        if (p + crypto::SHA256_SIZE > end) return std::nullopt;
        crypto::Hash256 h{};
        std::memcpy(h.data(), p, h.size());
        p += h.size();
        e.citations.push_back(h);
    }

    e.powNonce = readU64LE(p, end, ok);
    e.powBits = readU32LE(p, end, ok);
    if (!ok) return std::nullopt;

    if (p + e.authorSig.size() > end) return std::nullopt;
    std::memcpy(e.authorSig.data(), p, e.authorSig.size());
    p += e.authorSig.size();

    if (p != end) return std::nullopt;
    return e;
}

std::vector<uint8_t> ValidationVoteV1::payloadBytes() const {
    std::vector<uint8_t> out;
    out.reserve(1 + 32 + 32 + 33 + 4 + 6);
    writeU8(out, version);
    out.insert(out.end(), submitId.begin(), submitId.end());
    out.insert(out.end(), prevBlockHash.begin(), prevBlockHash.end());
    out.insert(out.end(), validatorPubKey.begin(), validatorPubKey.end());
    writeU32LE(out, flags);
    for (size_t i = 0; i < scores.size(); ++i) writeU16LE(out, scores[i]);
    return out;
}

crypto::Hash256 ValidationVoteV1::payloadHash() const {
    auto buf = payloadBytes();
    return crypto::sha256(buf.data(), buf.size());
}

bool ValidationVoteV1::verifySignature(std::string* reason) const {
    crypto::Hash256 h = payloadHash();
    if (!crypto::verify(h, signature, validatorPubKey)) {
        if (reason) *reason = "vote_sig_failed";
        return false;
    }
    return true;
}

std::vector<uint8_t> ValidationVoteV1::serialize() const {
    auto out = payloadBytes();
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

std::optional<ValidationVoteV1> ValidationVoteV1::deserialize(const std::vector<uint8_t>& data) {
    ValidationVoteV1 v;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();

    v.version = readU8(p, end, ok);
    if (!ok) return std::nullopt;

    if (p + v.submitId.size() + v.prevBlockHash.size() + v.validatorPubKey.size() > end) return std::nullopt;
    std::memcpy(v.submitId.data(), p, v.submitId.size());
    p += v.submitId.size();
    std::memcpy(v.prevBlockHash.data(), p, v.prevBlockHash.size());
    p += v.prevBlockHash.size();
    std::memcpy(v.validatorPubKey.data(), p, v.validatorPubKey.size());
    p += v.validatorPubKey.size();

    v.flags = readU32LE(p, end, ok);
    if (!ok) return std::nullopt;
    for (size_t i = 0; i < v.scores.size(); ++i) {
        v.scores[i] = readU16LE(p, end, ok);
        if (!ok) return std::nullopt;
    }

    if (p + v.signature.size() > end) return std::nullopt;
    std::memcpy(v.signature.data(), p, v.signature.size());
    p += v.signature.size();

    if (p != end) return std::nullopt;
    return v;
}

std::vector<uint8_t> FinalizationRecordV1::serialize() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), submitId.begin(), submitId.end());
    out.insert(out.end(), prevBlockHash.begin(), prevBlockHash.end());
    out.insert(out.end(), validatorSetHash.begin(), validatorSetHash.end());
    writeU64LE(out, finalizedAt);
    writeU32LE(out, static_cast<uint32_t>(votes.size()));
    for (const auto& v : votes) {
        auto vd = v.serialize();
        writeU32LE(out, static_cast<uint32_t>(vd.size()));
        out.insert(out.end(), vd.begin(), vd.end());
    }
    return out;
}

std::optional<FinalizationRecordV1> FinalizationRecordV1::deserialize(const std::vector<uint8_t>& data) {
    FinalizationRecordV1 r;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();

    if (p + r.submitId.size() + r.prevBlockHash.size() + r.validatorSetHash.size() > end) return std::nullopt;
    std::memcpy(r.submitId.data(), p, r.submitId.size());
    p += r.submitId.size();
    std::memcpy(r.prevBlockHash.data(), p, r.prevBlockHash.size());
    p += r.prevBlockHash.size();
    std::memcpy(r.validatorSetHash.data(), p, r.validatorSetHash.size());
    p += r.validatorSetHash.size();

    r.finalizedAt = readU64LE(p, end, ok);
    uint32_t voteCount = readU32LE(p, end, ok);
    if (!ok) return std::nullopt;
    if (voteCount > 100000) return std::nullopt;
    r.votes.clear();
    r.votes.reserve(voteCount);
    for (uint32_t i = 0; i < voteCount; ++i) {
        uint32_t len = readU32LE(p, end, ok);
        if (!ok) return std::nullopt;
        if (p + len > end) return std::nullopt;
        std::vector<uint8_t> buf(p, p + len);
        p += len;
        auto v = ValidationVoteV1::deserialize(buf);
        if (!v) return std::nullopt;
        r.votes.push_back(*v);
    }

    if (p != end) return std::nullopt;
    return r;
}

crypto::Hash256 validatorSetHashV1(const std::vector<crypto::PublicKey>& validators) {
    std::vector<crypto::PublicKey> sorted = validators;
    std::sort(sorted.begin(), sorted.end(), [](const crypto::PublicKey& a, const crypto::PublicKey& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
    });
    std::vector<uint8_t> buf;
    buf.reserve(sorted.size() * 33);
    for (const auto& v : sorted) buf.insert(buf.end(), v.begin(), v.end());
    return crypto::sha256(buf.data(), buf.size());
}

bool signKnowledgeEntryV1(KnowledgeEntryV1& entry, const crypto::PrivateKey& authorKey) {
    entry.authorPubKey = crypto::derivePublicKey(authorKey);
    entry.authorSig = crypto::sign(entry.signatureHash(), authorKey);
    return true;
}

bool signValidationVoteV1(ValidationVoteV1& vote, const crypto::PrivateKey& validatorKey) {
    vote.validatorPubKey = crypto::derivePublicKey(validatorKey);
    vote.signature = crypto::sign(vote.payloadHash(), validatorKey);
    return true;
}

}
