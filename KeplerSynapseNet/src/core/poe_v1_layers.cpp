// Additive PoE layers: harvest recipes, mouth isolation, absence quorum,
// half-life, retraction, anonymous seniority (ed25519 ring), two clocks,
// door scars, and lymph. No network I/O here.

#include "core/poe_v1_layers.h"
#include "crypto/ring_signature.h"

#include <sodium.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <queue>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace synapse::core::poe_v1 {

namespace {

void writeU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void writeU64LE(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

uint8_t readU8(const uint8_t*& p, const uint8_t* end, bool& ok) {
    if (p + 1 > end) { ok = false; return 0; }
    return *p++;
}

uint32_t readU32LE(const uint8_t*& p, const uint8_t* end, bool& ok) {
    if (p + 4 > end) { ok = false; return 0; }
    uint32_t v = static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
    p += 4;
    return v;
}

uint64_t readU64LE(const uint8_t*& p, const uint8_t* end, bool& ok) {
    if (p + 8 > end) { ok = false; return 0; }
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    p += 8;
    return v;
}

void writeVarBytes(std::vector<uint8_t>& out, const std::string& s) {
    writeU32LE(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

void writeVarBytes(std::vector<uint8_t>& out, const std::vector<uint8_t>& s) {
    writeU32LE(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

std::optional<std::string> readVarBytesString(const uint8_t*& p, const uint8_t* end, bool& ok, uint32_t maxLen) {
    uint32_t len = readU32LE(p, end, ok);
    if (!ok) return std::nullopt;
    if (len > maxLen) { ok = false; return std::nullopt; }
    if (p + len > end) { ok = false; return std::nullopt; }
    std::string s(reinterpret_cast<const char*>(p), len);
    p += len;
    return s;
}

std::optional<std::vector<uint8_t>> readVarBytes(const uint8_t*& p, const uint8_t* end, bool& ok, uint32_t maxLen) {
    uint32_t len = readU32LE(p, end, ok);
    if (!ok) return std::nullopt;
    if (len > maxLen) { ok = false; return std::nullopt; }
    if (p + len > end) { ok = false; return std::nullopt; }
    std::vector<uint8_t> s(p, p + len);
    p += len;
    return s;
}

bool readExact(const uint8_t*& p, const uint8_t* end, uint8_t* dest, size_t n) {
    if (p + n > end) return false;
    std::memcpy(dest, p, n);
    p += n;
    return true;
}

std::string toLowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void requireSodium() {
    static bool initialized = false;
    if (!initialized) {
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium initialization failed");
        }
        initialized = true;
    }
}

} // namespace

std::vector<uint8_t> HarvestRecipeV1::identityBytes() const {
    std::vector<uint8_t> out;
    writeVarBytes(out, locator);
    writeVarBytes(out, selector);
    writeVarBytes(out, mediaType);
    out.insert(out.end(), bodyHash.begin(), bodyHash.end());
    writeU32LE(out, static_cast<uint32_t>(citations.size()));
    for (const auto& c : citations) out.insert(out.end(), c.begin(), c.end());
    return out;
}

std::vector<uint8_t> HarvestRecipeV1::canonicalBytes() const {
    std::vector<uint8_t> out = identityBytes();
    out.insert(out.end(), authorPubKey.begin(), authorPubKey.end());
    return out;
}

crypto::Hash256 HarvestRecipeV1::recipeId() const {
    auto buf = identityBytes();
    return crypto::sha256(buf.data(), buf.size());
}

crypto::Hash256 HarvestRecipeV1::signatureHash() const {
    auto buf = canonicalBytes();
    return crypto::sha256(buf.data(), buf.size());
}

bool HarvestRecipeV1::checkLimits(std::string* reason) const {
    if (version != 1) {
        if (reason) *reason = "unsupported_version";
        return false;
    }
    if (locator.empty() || selector.empty()) {
        if (reason) *reason = "empty_recipe_fields";
        return false;
    }
    if (locator.size() > kMaxLocatorBytes || selector.size() > kMaxSelectorBytes ||
        mediaType.size() > kMaxMediaTypeBytes) {
        if (reason) *reason = "recipe_too_large";
        return false;
    }
    if (citations.size() > 16) {
        if (reason) *reason = "too_many_citations";
        return false;
    }
    if (containsInferenceKernelPayload(locator) || containsInferenceKernelPayload(selector) ||
        containsInferenceKernelPayload(mediaType)) {
        if (reason) *reason = "inference_kernel_payload";
        return false;
    }
    return true;
}

bool HarvestRecipeV1::verifySignature(std::string* reason) const {
    if (!crypto::verify(signatureHash(), authorSig, authorPubKey)) {
        if (reason) *reason = "recipe_sig_failed";
        return false;
    }
    return true;
}

bool HarvestRecipeV1::verifyAll(std::string* reason) const {
    if (!checkLimits(reason)) return false;
    return verifySignature(reason);
}

std::vector<uint8_t> HarvestRecipeV1::serialize() const {
    std::vector<uint8_t> out;
    writeU8(out, version);
    writeVarBytes(out, locator);
    writeVarBytes(out, selector);
    writeVarBytes(out, mediaType);
    out.insert(out.end(), bodyHash.begin(), bodyHash.end());
    out.insert(out.end(), authorPubKey.begin(), authorPubKey.end());
    writeU64LE(out, authoredAt);
    writeU32LE(out, static_cast<uint32_t>(citations.size()));
    for (const auto& c : citations) out.insert(out.end(), c.begin(), c.end());
    out.insert(out.end(), authorSig.begin(), authorSig.end());
    return out;
}

std::optional<HarvestRecipeV1> HarvestRecipeV1::deserialize(const std::vector<uint8_t>& data) {
    HarvestRecipeV1 r;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    r.version = readU8(p, end, ok);
    auto loc = readVarBytesString(p, end, ok, kMaxLocatorBytes);
    auto sel = readVarBytesString(p, end, ok, kMaxSelectorBytes);
    auto mt = readVarBytesString(p, end, ok, kMaxMediaTypeBytes);
    if (!ok || !loc || !sel || !mt) return std::nullopt;
    r.locator = *loc;
    r.selector = *sel;
    r.mediaType = *mt;
    if (!readExact(p, end, r.bodyHash.data(), r.bodyHash.size())) return std::nullopt;
    if (!readExact(p, end, r.authorPubKey.data(), r.authorPubKey.size())) return std::nullopt;
    r.authoredAt = readU64LE(p, end, ok);
    uint32_t n = readU32LE(p, end, ok);
    if (!ok || n > 16) return std::nullopt;
    r.citations.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        crypto::Hash256 h{};
        if (!readExact(p, end, h.data(), h.size())) return std::nullopt;
        r.citations.push_back(h);
    }
    if (!readExact(p, end, r.authorSig.data(), r.authorSig.size())) return std::nullopt;
    if (p != end) return std::nullopt;
    return r;
}

std::vector<uint8_t> RecipeReplayV1::payloadBytes() const {
    std::vector<uint8_t> out;
    writeU8(out, version);
    out.insert(out.end(), recipeId.begin(), recipeId.end());
    out.insert(out.end(), reporterPubKey.begin(), reporterPubKey.end());
    out.insert(out.end(), observedBodyHash.begin(), observedBodyHash.end());
    writeU8(out, match);
    return out;
}

crypto::Hash256 RecipeReplayV1::payloadHash() const {
    auto buf = payloadBytes();
    return crypto::sha256(buf.data(), buf.size());
}

bool RecipeReplayV1::verifySignature(std::string* reason) const {
    if (!crypto::verify(payloadHash(), signature, reporterPubKey)) {
        if (reason) *reason = "replay_sig_failed";
        return false;
    }
    return true;
}

std::vector<uint8_t> RecipeReplayV1::serialize() const {
    auto out = payloadBytes();
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

std::optional<RecipeReplayV1> RecipeReplayV1::deserialize(const std::vector<uint8_t>& data) {
    RecipeReplayV1 r;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    r.version = readU8(p, end, ok);
    if (!ok) return std::nullopt;
    if (!readExact(p, end, r.recipeId.data(), r.recipeId.size())) return std::nullopt;
    if (!readExact(p, end, r.reporterPubKey.data(), r.reporterPubKey.size())) return std::nullopt;
    if (!readExact(p, end, r.observedBodyHash.data(), r.observedBodyHash.size())) return std::nullopt;
    r.match = readU8(p, end, ok);
    if (!ok) return std::nullopt;
    if (!readExact(p, end, r.signature.data(), r.signature.size())) return std::nullopt;
    if (p != end) return std::nullopt;
    return r;
}

std::vector<uint8_t> AbsenceReportV1::payloadBytes() const {
    std::vector<uint8_t> out;
    writeU8(out, version);
    out.insert(out.end(), recipeId.begin(), recipeId.end());
    out.insert(out.end(), reporterPubKey.begin(), reporterPubKey.end());
    writeU64LE(out, windowStart);
    writeU64LE(out, windowEnd);
    writeU8(out, contradictionFound);
    writeU32LE(out, static_cast<uint32_t>(contradictingHashes.size()));
    for (const auto& h : contradictingHashes) out.insert(out.end(), h.begin(), h.end());
    return out;
}

crypto::Hash256 AbsenceReportV1::payloadHash() const {
    auto buf = payloadBytes();
    return crypto::sha256(buf.data(), buf.size());
}

bool AbsenceReportV1::verifySignature(std::string* reason) const {
    if (!crypto::verify(payloadHash(), signature, reporterPubKey)) {
        if (reason) *reason = "absence_sig_failed";
        return false;
    }
    return true;
}

std::vector<uint8_t> AbsenceReportV1::serialize() const {
    auto out = payloadBytes();
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

std::optional<AbsenceReportV1> AbsenceReportV1::deserialize(const std::vector<uint8_t>& data) {
    AbsenceReportV1 r;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    r.version = readU8(p, end, ok);
    if (!ok) return std::nullopt;
    if (!readExact(p, end, r.recipeId.data(), r.recipeId.size())) return std::nullopt;
    if (!readExact(p, end, r.reporterPubKey.data(), r.reporterPubKey.size())) return std::nullopt;
    r.windowStart = readU64LE(p, end, ok);
    r.windowEnd = readU64LE(p, end, ok);
    r.contradictionFound = readU8(p, end, ok);
    uint32_t n = readU32LE(p, end, ok);
    if (!ok || n > 64) return std::nullopt;
    for (uint32_t i = 0; i < n; ++i) {
        crypto::Hash256 h{};
        if (!readExact(p, end, h.data(), h.size())) return std::nullopt;
        r.contradictingHashes.push_back(h);
    }
    if (!readExact(p, end, r.signature.data(), r.signature.size())) return std::nullopt;
    if (p != end) return std::nullopt;
    return r;
}

std::vector<uint8_t> AbsenceQuorumV1::serialize() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), recipeId.begin(), recipeId.end());
    writeU8(out, static_cast<uint8_t>(outcome));
    writeU32LE(out, static_cast<uint32_t>(reports.size()));
    for (const auto& r : reports) {
        auto b = r.serialize();
        writeU32LE(out, static_cast<uint32_t>(b.size()));
        out.insert(out.end(), b.begin(), b.end());
    }
    return out;
}

std::optional<AbsenceQuorumV1> AbsenceQuorumV1::deserialize(const std::vector<uint8_t>& data) {
    AbsenceQuorumV1 q;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    if (!readExact(p, end, q.recipeId.data(), q.recipeId.size())) return std::nullopt;
    q.outcome = static_cast<AbsenceOutcome>(readU8(p, end, ok));
    uint32_t n = readU32LE(p, end, ok);
    if (!ok || n > 64) return std::nullopt;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t len = readU32LE(p, end, ok);
        if (!ok || p + len > end) return std::nullopt;
        std::vector<uint8_t> buf(p, p + len);
        p += len;
        auto r = AbsenceReportV1::deserialize(buf);
        if (!r) return std::nullopt;
        q.reports.push_back(*r);
    }
    if (p != end) return std::nullopt;
    return q;
}

std::vector<uint8_t> RetractV1::payloadBytes() const {
    std::vector<uint8_t> out;
    writeU8(out, version);
    out.insert(out.end(), submitId.begin(), submitId.end());
    out.insert(out.end(), authorPubKey.begin(), authorPubKey.end());
    writeU64LE(out, retractedAt);
    return out;
}

crypto::Hash256 RetractV1::payloadHash() const {
    auto buf = payloadBytes();
    return crypto::sha256(buf.data(), buf.size());
}

bool RetractV1::verifySignature(std::string* reason) const {
    if (!crypto::verify(payloadHash(), signature, authorPubKey)) {
        if (reason) *reason = "retract_sig_failed";
        return false;
    }
    return true;
}

std::vector<uint8_t> RetractV1::serialize() const {
    auto out = payloadBytes();
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

std::optional<RetractV1> RetractV1::deserialize(const std::vector<uint8_t>& data) {
    RetractV1 r;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    r.version = readU8(p, end, ok);
    if (!ok) return std::nullopt;
    if (!readExact(p, end, r.submitId.data(), r.submitId.size())) return std::nullopt;
    if (!readExact(p, end, r.authorPubKey.data(), r.authorPubKey.size())) return std::nullopt;
    r.retractedAt = readU64LE(p, end, ok);
    if (!ok) return std::nullopt;
    if (!readExact(p, end, r.signature.data(), r.signature.size())) return std::nullopt;
    if (p != end) return std::nullopt;
    return r;
}

std::vector<uint8_t> WitnessV1::payloadBytes() const {
    std::vector<uint8_t> out;
    writeU8(out, version);
    out.insert(out.end(), submitId.begin(), submitId.end());
    out.insert(out.end(), reporterPubKey.begin(), reporterPubKey.end());
    writeU64LE(out, witnessedAt);
    return out;
}

crypto::Hash256 WitnessV1::payloadHash() const {
    auto buf = payloadBytes();
    return crypto::sha256(buf.data(), buf.size());
}

bool WitnessV1::verifySignature(std::string* reason) const {
    if (!crypto::verify(payloadHash(), signature, reporterPubKey)) {
        if (reason) *reason = "witness_sig_failed";
        return false;
    }
    return true;
}

std::vector<uint8_t> WitnessV1::serialize() const {
    auto out = payloadBytes();
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

std::optional<WitnessV1> WitnessV1::deserialize(const std::vector<uint8_t>& data) {
    WitnessV1 w;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    w.version = readU8(p, end, ok);
    if (!ok) return std::nullopt;
    if (!readExact(p, end, w.submitId.data(), w.submitId.size())) return std::nullopt;
    if (!readExact(p, end, w.reporterPubKey.data(), w.reporterPubKey.size())) return std::nullopt;
    w.witnessedAt = readU64LE(p, end, ok);
    if (!ok) return std::nullopt;
    if (!readExact(p, end, w.signature.data(), w.signature.size())) return std::nullopt;
    if (p != end) return std::nullopt;
    return w;
}

crypto::Hash256 ScarV1::scarId() const {
    std::vector<uint8_t> buf;
    writeVarBytes(buf, gateClass);
    writeVarBytes(buf, dayUtc);
    writeVarBytes(buf, methodClass);
    return crypto::sha256(buf.data(), buf.size());
}

std::vector<uint8_t> ScarV1::serialize() const {
    std::vector<uint8_t> out;
    writeU8(out, version);
    writeVarBytes(out, gateClass);
    writeVarBytes(out, dayUtc);
    writeVarBytes(out, methodClass);
    return out;
}

std::optional<ScarV1> ScarV1::deserialize(const std::vector<uint8_t>& data) {
    ScarV1 s;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    s.version = readU8(p, end, ok);
    auto g = readVarBytesString(p, end, ok, kMaxScarFieldBytes);
    auto d = readVarBytesString(p, end, ok, kMaxScarFieldBytes);
    auto m = readVarBytesString(p, end, ok, kMaxScarFieldBytes);
    if (!ok || !g || !d || !m) return std::nullopt;
    s.gateClass = *g;
    s.dayUtc = *d;
    s.methodClass = *m;
    if (p != end) return std::nullopt;
    return s;
}

std::vector<uint8_t> SeniorityTokenV1::payloadBytes() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), submitId.begin(), submitId.end());
    out.insert(out.end(), point.begin(), point.end());
    out.insert(out.end(), authorPubKey.begin(), authorPubKey.end());
    return out;
}

crypto::Hash256 SeniorityTokenV1::payloadHash() const {
    auto buf = payloadBytes();
    return crypto::sha256(buf.data(), buf.size());
}

bool SeniorityTokenV1::verifyBinding(std::string* reason) const {
    if (!crypto::verify(payloadHash(), bindingSig, authorPubKey)) {
        if (reason) *reason = "seniority_bind_failed";
        return false;
    }
    return true;
}

std::vector<uint8_t> SeniorityTokenV1::serialize() const {
    auto out = payloadBytes();
    out.insert(out.end(), bindingSig.begin(), bindingSig.end());
    return out;
}

std::optional<SeniorityTokenV1> SeniorityTokenV1::deserialize(const std::vector<uint8_t>& data) {
    SeniorityTokenV1 t;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    if (!readExact(p, end, t.submitId.data(), t.submitId.size())) return std::nullopt;
    if (!readExact(p, end, t.point.data(), t.point.size())) return std::nullopt;
    if (!readExact(p, end, t.authorPubKey.data(), t.authorPubKey.size())) return std::nullopt;
    if (!readExact(p, end, t.bindingSig.data(), t.bindingSig.size())) return std::nullopt;
    if (p != end) return std::nullopt;
    return t;
}

std::vector<uint8_t> SeniorityProofV1::serialize() const {
    std::vector<uint8_t> out;
    writeU32LE(out, threshold);
    writeU32LE(out, static_cast<uint32_t>(ring.size()));
    for (const auto& p : ring) out.insert(out.end(), p.begin(), p.end());
    writeU32LE(out, static_cast<uint32_t>(ringSigs.size()));
    for (const auto& s : ringSigs) writeVarBytes(out, s);
    return out;
}

std::optional<SeniorityProofV1> SeniorityProofV1::deserialize(const std::vector<uint8_t>& data) {
    SeniorityProofV1 pr;
    bool ok = true;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    pr.threshold = readU32LE(p, end, ok);
    uint32_t n = readU32LE(p, end, ok);
    if (!ok || n > 64) return std::nullopt;
    for (uint32_t i = 0; i < n; ++i) {
        std::array<uint8_t, kSeniorityEd25519Bytes> pt{};
        if (!readExact(p, end, pt.data(), pt.size())) return std::nullopt;
        pr.ring.push_back(pt);
    }
    uint32_t m = readU32LE(p, end, ok);
    if (!ok || m > 64) return std::nullopt;
    for (uint32_t i = 0; i < m; ++i) {
        auto sig = readVarBytes(p, end, ok, 65536);
        if (!ok || !sig) return std::nullopt;
        pr.ringSigs.push_back(*sig);
    }
    if (p != end) return std::nullopt;
    return pr;
}

bool containsInferenceKernelPayload(const std::string& text) {
    if (text.empty()) return false;
    const std::string lower = toLowerAscii(text);
    static const char* kPhrase[] = {
        "score from model",
        "score_from_model",
        "inference_kernel",
        "gguf_commentary",
        "gguf commentary",
        "prompt dump",
        "prompt_dump",
        "llm_token",
        "### instruction:",
        "### response:",
        "<|im_start|>",
        "<|im_end|>",
        "<|assistant|>",
        "<|user|>",
        "<|system|>",
        "[inst]",
        "<<sys>>"
    };
    for (const char* p : kPhrase) {
        if (lower.find(p) != std::string::npos) return true;
    }
    if (text.find("</s>") != std::string::npos) return true;
    if (text.find("<s>") != std::string::npos) return true;
    return false;
}

bool consensusPayloadClean(const std::string& title, const std::string& body, std::string* reason) {
    if (containsInferenceKernelPayload(title) || containsInferenceKernelPayload(body)) {
        if (reason) *reason = "inference_kernel_payload";
        return false;
    }
    return true;
}

bool voteNoteClean(const std::string& note, std::string* reason) {
    if (note.size() > kMaxVoteNoteBytes) {
        if (reason) *reason = "vote_note_too_large";
        return false;
    }
    if (containsInferenceKernelPayload(note)) {
        if (reason) *reason = "inference_kernel_payload";
        return false;
    }
    return true;
}

bool isUtcDateYYYYMMDD(const std::string& day) {
    if (day.size() != 10) return false;
    if (day[4] != '-' || day[7] != '-') return false;
    for (size_t i = 0; i < day.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (!std::isdigit(static_cast<unsigned char>(day[i]))) return false;
    }
    int y = (day[0] - '0') * 1000 + (day[1] - '0') * 100 + (day[2] - '0') * 10 + (day[3] - '0');
    int m = (day[5] - '0') * 10 + (day[6] - '0');
    int d = (day[8] - '0') * 10 + (day[9] - '0');
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    return true;
}

bool scarAcceptable(const ScarV1& scar, std::string* reason) {
    if (scar.version != 1) {
        if (reason) *reason = "unsupported_version";
        return false;
    }
    if (!scar.cookie.empty() || !scar.session.empty() || !scar.header.empty() ||
        !scar.credential.empty() || !scar.urlQuery.empty()) {
        if (reason) *reason = "scar_secret_field";
        return false;
    }
    if (scar.gateClass.empty() || scar.dayUtc.empty() || scar.methodClass.empty()) {
        if (reason) *reason = "scar_missing_class";
        return false;
    }
    if (scar.gateClass.size() > kMaxScarFieldBytes || scar.dayUtc.size() > kMaxScarFieldBytes ||
        scar.methodClass.size() > kMaxScarFieldBytes) {
        if (reason) *reason = "scar_too_large";
        return false;
    }
    if (!isUtcDateYYYYMMDD(scar.dayUtc)) {
        if (reason) *reason = "scar_day_not_utc_date";
        return false;
    }
    auto forbidden = [](const std::string& s) {
        std::string l = toLowerAscii(s);
        return l.find("cookie") != std::string::npos ||
               l.find("session") != std::string::npos ||
               l.find("authorization") != std::string::npos ||
               l.find("set-cookie") != std::string::npos ||
               l.find("?") != std::string::npos;
    };
    if (forbidden(scar.gateClass) || forbidden(scar.methodClass) || forbidden(scar.dayUtc)) {
        if (reason) *reason = "scar_secret_field";
        return false;
    }
    if (containsInferenceKernelPayload(scar.gateClass) || containsInferenceKernelPayload(scar.methodClass)) {
        if (reason) *reason = "inference_kernel_payload";
        return false;
    }
    return true;
}

crypto::Hash256 hashFetchedBody(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return crypto::sha256(std::string());
    return crypto::sha256(bytes.data(), bytes.size());
}

std::optional<HarvestRecipeV1> lymphExportIfMatch(
    const LymphDraftV1& draft,
    const std::vector<uint8_t>& replayBytes,
    std::string* reason
) {
    if (draft.pageBytes.size() > kMaxLymphPageBytes || replayBytes.size() > kMaxLymphPageBytes) {
        if (reason) *reason = "lymph_too_large";
        return std::nullopt;
    }
    if (draft.pageBytes.empty()) {
        if (reason) *reason = "lymph_empty";
        return std::nullopt;
    }
    auto localHash = hashFetchedBody(draft.pageBytes);
    auto replayHash = hashFetchedBody(replayBytes);
    if (localHash != replayHash) {
        if (reason) *reason = "lymph_mismatch";
        return std::nullopt;
    }
    HarvestRecipeV1 out = draft.recipe;
    if (std::all_of(out.bodyHash.begin(), out.bodyHash.end(), [](uint8_t b) { return b == 0; })) {
        out.bodyHash = localHash;
    } else if (out.bodyHash != localHash) {
        if (reason) *reason = "lymph_mismatch";
        return std::nullopt;
    }
    if (!out.checkLimits(reason)) return std::nullopt;
    auto ser = out.serialize();
    // Exported object is the recipe record. Page bytes never leave lymph.
    auto again = HarvestRecipeV1::deserialize(ser);
    if (!again) {
        if (reason) *reason = "lymph_serialize_failed";
        return std::nullopt;
    }
    return out;
}

bool signHarvestRecipeV1(HarvestRecipeV1& recipe, const crypto::PrivateKey& authorKey) {
    recipe.authorPubKey = crypto::derivePublicKey(authorKey);
    recipe.authorSig = crypto::sign(recipe.signatureHash(), authorKey);
    return true;
}

bool signRecipeReplayV1(RecipeReplayV1& replay, const crypto::PrivateKey& reporterKey) {
    replay.reporterPubKey = crypto::derivePublicKey(reporterKey);
    replay.signature = crypto::sign(replay.payloadHash(), reporterKey);
    return true;
}

bool signAbsenceReportV1(AbsenceReportV1& report, const crypto::PrivateKey& reporterKey) {
    report.reporterPubKey = crypto::derivePublicKey(reporterKey);
    report.signature = crypto::sign(report.payloadHash(), reporterKey);
    return true;
}

bool signRetractV1(RetractV1& retract, const crypto::PrivateKey& authorKey) {
    retract.authorPubKey = crypto::derivePublicKey(authorKey);
    retract.signature = crypto::sign(retract.payloadHash(), authorKey);
    return true;
}

bool signWitnessV1(WitnessV1& witness, const crypto::PrivateKey& reporterKey) {
    witness.reporterPubKey = crypto::derivePublicKey(reporterKey);
    witness.signature = crypto::sign(witness.payloadHash(), reporterKey);
    return true;
}

bool deriveSeniorityScalar(
    const crypto::PrivateKey& authorKey,
    const crypto::Hash256& submitId,
    std::vector<uint8_t>* outScalar
) {
    if (!outScalar) return false;
    requireSodium();
    std::vector<uint8_t> data;
    const char tag[] = "poe.seniority.v1";
    data.insert(data.end(), tag, tag + sizeof(tag) - 1);
    data.insert(data.end(), authorKey.begin(), authorKey.end());
    data.insert(data.end(), submitId.begin(), submitId.end());
    std::vector<uint8_t> hash(crypto_core_ed25519_NONREDUCEDSCALARBYTES);
    crypto_hash_sha512(hash.data(), data.data(), data.size());
    outScalar->assign(crypto_core_ed25519_SCALARBYTES, 0);
    crypto_core_ed25519_scalar_reduce(outScalar->data(), hash.data());
    return true;
}

bool deriveSeniorityPoint(
    const crypto::PrivateKey& authorKey,
    const crypto::Hash256& submitId,
    std::array<uint8_t, kSeniorityEd25519Bytes>* outPoint
) {
    if (!outPoint) return false;
    std::vector<uint8_t> scalar;
    if (!deriveSeniorityScalar(authorKey, submitId, &scalar)) return false;
    requireSodium();
    if (crypto_scalarmult_ed25519_base_noclamp(outPoint->data(), scalar.data()) != 0) return false;
    return true;
}

bool bindSeniorityToken(SeniorityTokenV1& token, const crypto::PrivateKey& authorKey) {
    token.authorPubKey = crypto::derivePublicKey(authorKey);
    if (!deriveSeniorityPoint(authorKey, token.submitId, &token.point)) return false;
    token.bindingSig = crypto::sign(token.payloadHash(), authorKey);
    return true;
}

std::optional<SeniorityProofV1> proveAnonymousSeniority(
    const crypto::PrivateKey& authorKey,
    const std::vector<crypto::Hash256>& ownedSubmitIds,
    const std::vector<std::array<uint8_t, kSeniorityEd25519Bytes>>& ringPoints,
    uint32_t threshold,
    std::string* reason
) {
    if (threshold == 0) {
        if (reason) *reason = "seniority_threshold_zero";
        return std::nullopt;
    }
    if (ringPoints.size() < 2 || ringPoints.size() > 64) {
        if (reason) *reason = "seniority_ring_size";
        return std::nullopt;
    }
    if (ownedSubmitIds.size() < threshold) {
        if (reason) *reason = "seniority_not_enough_owned";
        return std::nullopt;
    }

    std::vector<std::array<uint8_t, kSeniorityEd25519Bytes>> ring = ringPoints;
    std::sort(ring.begin(), ring.end());
    ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
    if (ring.size() < 2) {
        if (reason) *reason = "seniority_ring_size";
        return std::nullopt;
    }

    std::vector<std::vector<uint8_t>> ringVec;
    ringVec.reserve(ring.size());
    for (const auto& p : ring) {
        ringVec.emplace_back(p.begin(), p.end());
    }

    std::vector<uint8_t> message;
    const char tag[] = "poe.seniority.proof.v1";
    message.insert(message.end(), tag, tag + sizeof(tag) - 1);
    writeU32LE(message, threshold);
    for (const auto& p : ring) message.insert(message.end(), p.begin(), p.end());

    struct Owned {
        std::vector<uint8_t> scalar;
        size_t index = 0;
    };
    std::vector<Owned> owned;
    for (const auto& sid : ownedSubmitIds) {
        std::array<uint8_t, kSeniorityEd25519Bytes> pt{};
        std::vector<uint8_t> scalar;
        if (!deriveSeniorityScalar(authorKey, sid, &scalar)) continue;
        if (!deriveSeniorityPoint(authorKey, sid, &pt)) continue;
        auto it = std::find(ring.begin(), ring.end(), pt);
        if (it == ring.end()) continue;
        Owned o;
        o.scalar = std::move(scalar);
        o.index = static_cast<size_t>(it - ring.begin());
        bool dup = false;
        for (const auto& prev : owned) {
            if (prev.index == o.index) { dup = true; break; }
        }
        if (!dup) owned.push_back(std::move(o));
    }
    if (owned.size() < threshold) {
        if (reason) *reason = "seniority_owned_not_in_ring";
        return std::nullopt;
    }
    owned.resize(threshold);

    SeniorityProofV1 proof;
    proof.threshold = threshold;
    proof.ring = ring;
    try {
        for (const auto& o : owned) {
            auto sig = crypto::RingSign::sign(message, ringVec, o.scalar, o.index, false);
            proof.ringSigs.push_back(sig.serialize());
        }
    } catch (...) {
        if (reason) *reason = "seniority_sign_failed";
        return std::nullopt;
    }
    return proof;
}

bool verifyAnonymousSeniority(
    const SeniorityProofV1& proof,
    const std::vector<std::array<uint8_t, kSeniorityEd25519Bytes>>& publishedPoints,
    std::string* reason
) {
    if (proof.threshold == 0 || proof.ringSigs.size() != proof.threshold) {
        if (reason) *reason = "seniority_threshold_mismatch";
        return false;
    }
    if (proof.ring.size() < 2 || proof.ring.size() > 64) {
        if (reason) *reason = "seniority_ring_size";
        return false;
    }
    std::set<std::array<uint8_t, kSeniorityEd25519Bytes>> published(publishedPoints.begin(), publishedPoints.end());
    for (const auto& p : proof.ring) {
        if (!published.count(p)) {
            if (reason) *reason = "seniority_ring_not_published";
            return false;
        }
    }
    auto sorted = proof.ring;
    std::sort(sorted.begin(), sorted.end());
    if (sorted != proof.ring) {
        if (reason) *reason = "seniority_ring_unsorted";
        return false;
    }

    std::vector<std::vector<uint8_t>> ringVec;
    ringVec.reserve(proof.ring.size());
    for (const auto& p : proof.ring) ringVec.emplace_back(p.begin(), p.end());

    std::vector<uint8_t> message;
    const char tag[] = "poe.seniority.proof.v1";
    message.insert(message.end(), tag, tag + sizeof(tag) - 1);
    writeU32LE(message, proof.threshold);
    for (const auto& p : proof.ring) message.insert(message.end(), p.begin(), p.end());

    std::set<std::vector<uint8_t>> images;
    try {
        for (const auto& raw : proof.ringSigs) {
            auto sig = crypto::RingSignature::deserialize(raw);
            if (!crypto::RingSign::verify(message, ringVec, sig)) {
                if (reason) *reason = "seniority_sig_invalid";
                return false;
            }
            if (!images.insert(sig.keyImage).second) {
                if (reason) *reason = "seniority_duplicate_image";
                return false;
            }
        }
    } catch (...) {
        if (reason) *reason = "seniority_sig_invalid";
        return false;
    }
    return true;
}

std::vector<crypto::Hash256> citationDagOrder(
    const std::vector<crypto::Hash256>& submitIds,
    const std::vector<std::vector<crypto::Hash256>>& citedContentIds,
    const std::vector<crypto::Hash256>& nodeContentIds
) {
    const size_t n = submitIds.size();
    std::vector<crypto::Hash256> empty;
    if (n == 0 || citedContentIds.size() != n || nodeContentIds.size() != n) return empty;

    std::unordered_map<std::string, size_t> contentIndex;
    contentIndex.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        contentIndex[crypto::toHex(nodeContentIds[i])] = i;
    }

    std::vector<std::vector<size_t>> outgoing(n);
    std::vector<int> indeg(n, 0);
    for (size_t i = 0; i < n; ++i) {
        std::set<size_t> preds;
        for (const auto& cid : citedContentIds[i]) {
            auto it = contentIndex.find(crypto::toHex(cid));
            if (it == contentIndex.end()) continue;
            if (it->second == i) continue;
            preds.insert(it->second);
        }
        for (size_t p : preds) {
            outgoing[p].push_back(i);
            indeg[i] += 1;
        }
    }

    auto idMinHeap = [&](size_t a, size_t b) {
        // Min-heap on submitId bytes. Arrival/wall clock is not an input.
        return std::lexicographical_compare(
            submitIds[b].begin(), submitIds[b].end(),
            submitIds[a].begin(), submitIds[a].end());
    };
    std::priority_queue<size_t, std::vector<size_t>, decltype(idMinHeap)> ready(idMinHeap);
    for (size_t i = 0; i < n; ++i) {
        if (indeg[i] == 0) ready.push(i);
    }

    std::vector<crypto::Hash256> order;
    order.reserve(n);
    while (!ready.empty()) {
        size_t i = ready.top();
        ready.pop();
        order.push_back(submitIds[i]);
        for (size_t j : outgoing[i]) {
            indeg[j] -= 1;
            if (indeg[j] == 0) ready.push(j);
        }
    }
    if (order.size() != n) return empty;
    return order;
}

}
