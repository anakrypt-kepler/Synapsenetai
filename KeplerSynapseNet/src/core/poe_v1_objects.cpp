#include "core/poe_v1_objects.h"
#include "quantum/application_signature.h"
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

bool KnowledgeEntryV1::verifyZKProof(std::string* reason) const {
    bool empty = true;
    for (auto b : zkCommitment) { if (b != 0) { empty = false; break; } }
    if (empty) return true;

    std::vector<uint8_t> challengeInput;
    challengeInput.insert(challengeInput.end(), zkCommitment.begin(), zkCommitment.end());
    challengeInput.insert(challengeInput.end(), authorPubKey.begin(), authorPubKey.end());
    auto cid = contentId();
    challengeInput.insert(challengeInput.end(), cid.begin(), cid.end());
    crypto::Hash256 challenge = crypto::sha256(challengeInput.data(), challengeInput.size());

    crypto::Hash256 recK{};
    for (size_t i = 0; i < 32; ++i) recK[i] = zkResponse[i] ^ challenge[i];
    std::vector<uint8_t> verBuf;
    verBuf.insert(verBuf.end(), recK.begin(), recK.end());
    verBuf.insert(verBuf.end(), authorPubKey.begin(), authorPubKey.end());
    auto verHash = crypto::sha256(verBuf.data(), verBuf.size());
    crypto::PublicKey expected{};
    std::memcpy(expected.data(), verHash.data(), std::min(verHash.size(), expected.size()));
    if (expected != zkCommitment) {
        if (reason) *reason = "zk_proof_failed";
        return false;
    }
    return true;
}

bool KnowledgeEntryV1::verifyAll(const LimitsV1& limits, std::string* reason) const {
    if (!checkLimits(limits, reason)) return false;
    if (!verifyPoW(reason)) return false;
    if (!verifySignature(reason)) return false;
    if (!verifyZKProof(reason)) return false;
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
    out.insert(out.end(), zkCommitment.begin(), zkCommitment.end());
    out.insert(out.end(), zkResponse.begin(), zkResponse.end());
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

    if (p + e.zkCommitment.size() + e.zkResponse.size() <= end) {
        std::memcpy(e.zkCommitment.data(), p, e.zkCommitment.size());
        p += e.zkCommitment.size();
        std::memcpy(e.zkResponse.data(), p, e.zkResponse.size());
        p += e.zkResponse.size();
    }

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

static constexpr uint8_t POE_VOTE_TRAILER_V1_QUANTUM_SIG = 0x01;
static constexpr uint32_t POE_VOTE_MAX_QUANTUM_SIGNATURE_SIZE = 65536;

bool ValidationVoteV1::verifySignature(std::string* reason) const {
    crypto::Hash256 h = payloadHash();
    if (!crypto::verify(h, signature, validatorPubKey)) {
        if (reason) *reason = "vote_sig_failed";
        return false;
    }
    if (!quantumSignature.empty() && !quantum::isApplicationSignatureEnvelope(quantumSignature)) {
        if (reason) *reason = "vote_pq_envelope_invalid";
        return false;
    }
    return true;
}

std::vector<uint8_t> ValidationVoteV1::serialize() const {
    auto out = payloadBytes();
    out.insert(out.end(), signature.begin(), signature.end());
    if (!quantumSignature.empty()) {
        out.push_back(POE_VOTE_TRAILER_V1_QUANTUM_SIG);
        writeU32LE(out, static_cast<uint32_t>(quantumSignature.size()));
        out.insert(out.end(), quantumSignature.begin(), quantumSignature.end());
    }
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

    if (end - p >= 1 && *p == POE_VOTE_TRAILER_V1_QUANTUM_SIG) {
        ++p;
        uint32_t qsLen = readU32LE(p, end, ok);
        if (!ok) return std::nullopt;
        if (qsLen > POE_VOTE_MAX_QUANTUM_SIGNATURE_SIZE) return std::nullopt;
        if (p + qsLen > end) return std::nullopt;
        v.quantumSignature.assign(p, p + qsLen);
        p += qsLen;
    }

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
    generateZKProof(entry, authorKey);
    return true;
}

bool generateZKProof(KnowledgeEntryV1& entry, const crypto::PrivateKey& authorKey) {
    auto nonce = crypto::randomBytes(32);
    crypto::Hash256 k{};
    std::memcpy(k.data(), nonce.data(), 32);

    std::vector<uint8_t> commitBuf;
    commitBuf.insert(commitBuf.end(), k.begin(), k.end());
    commitBuf.insert(commitBuf.end(), entry.authorPubKey.begin(), entry.authorPubKey.end());
    auto commitHash = crypto::sha256(commitBuf.data(), commitBuf.size());
    std::memcpy(entry.zkCommitment.data(), commitHash.data(), std::min(commitHash.size(), entry.zkCommitment.size()));

    std::vector<uint8_t> challengeInput;
    challengeInput.insert(challengeInput.end(), entry.zkCommitment.begin(), entry.zkCommitment.end());
    challengeInput.insert(challengeInput.end(), entry.authorPubKey.begin(), entry.authorPubKey.end());
    auto cid = entry.contentId();
    challengeInput.insert(challengeInput.end(), cid.begin(), cid.end());
    crypto::Hash256 challenge = crypto::sha256(challengeInput.data(), challengeInput.size());

    for (size_t i = 0; i < 32; ++i) {
        entry.zkResponse[i] = k[i] ^ challenge[i];
    }

    crypto::Hash256 recK{};
    for (size_t i = 0; i < 32; ++i) recK[i] = entry.zkResponse[i] ^ challenge[i];
    std::vector<uint8_t> verBuf;
    verBuf.insert(verBuf.end(), recK.begin(), recK.end());
    verBuf.insert(verBuf.end(), entry.authorPubKey.begin(), entry.authorPubKey.end());
    auto verHash = crypto::sha256(verBuf.data(), verBuf.size());
    crypto::PublicKey expected{};
    std::memcpy(expected.data(), verHash.data(), std::min(verHash.size(), expected.size()));
    return expected == entry.zkCommitment;
}

bool signValidationVoteV1(ValidationVoteV1& vote, const crypto::PrivateKey& validatorKey) {
    vote.quantumSignature.clear();
    vote.validatorPubKey = crypto::derivePublicKey(validatorKey);
    vote.signature = crypto::sign(vote.payloadHash(), validatorKey);
    return true;
}

bool signValidationVoteV1(ValidationVoteV1& vote, const crypto::PrivateKey& validatorKey,
                          const quantum::HybridKeyPair& quantumKeyPair) {
    if (!signValidationVoteV1(vote, validatorKey)) return false;
    if (quantumKeyPair.classicSecretKey.empty() || quantumKeyPair.pqcSecretKey.empty()) {
        return false;
    }
    std::vector<uint8_t> binding(vote.validatorPubKey.begin(), vote.validatorPubKey.end());
    auto payload = vote.serialize();
    auto envelope = quantum::signApplicationPayload(
        "core.poe.validation_vote",
        payload,
        binding,
        quantumKeyPair);
    if (envelope.empty()) return false;
    vote.quantumSignature = std::move(envelope);
    return true;
}

}
