#include "core/poe_v1_engine.h"
#include "database/database.h"
#include "utils/logger.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <numeric>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

namespace synapse::core {

static void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void writeU64LE(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

static uint32_t readU32LEAt(const std::vector<uint8_t>& data, size_t offset, uint32_t def) {
    if (data.size() < offset + 4) return def;
    uint32_t v = 0;
    v |= static_cast<uint32_t>(data[offset + 0]) << 0;
    v |= static_cast<uint32_t>(data[offset + 1]) << 8;
    v |= static_cast<uint32_t>(data[offset + 2]) << 16;
    v |= static_cast<uint32_t>(data[offset + 3]) << 24;
    return v;
}

static uint64_t readU64LE(const std::vector<uint8_t>& data, uint64_t def) {
    if (data.size() < 8) return def;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(data[i]) << (8 * i);
    return v;
}

static uint64_t readU64LEAt(const std::vector<uint8_t>& data, size_t offset, uint64_t def) {
    if (data.size() < offset + 8) return def;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(data[offset + static_cast<size_t>(i)]) << (8 * i);
    return v;
}

static std::string hex32(const crypto::Hash256& h) {
    return crypto::toHex(h.data(), h.size());
}

static std::string toLowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool isZeroPubKey(const crypto::PublicKey& pk) {
    return std::all_of(pk.begin(), pk.end(), [](uint8_t b) { return b == 0; });
}

static std::vector<crypto::PublicKey> normalizeValidatorList(const std::vector<crypto::PublicKey>& in) {
    std::vector<crypto::PublicKey> out;
    out.reserve(in.size());
    for (const auto& pk : in) {
        if (isZeroPubKey(pk)) continue;
        out.push_back(pk);
    }
    std::sort(out.begin(), out.end(), [](const crypto::PublicKey& a, const crypto::PublicKey& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
    });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static std::string validatorIdentityKey(const crypto::PublicKey& pk) {
    return "poe:v1:validator:id:" + crypto::toHex(pk);
}

static std::string validatorStakeKey(const crypto::PublicKey& pk) {
    return "poe:v1:validator:stake:" + crypto::toHex(pk);
}

static std::optional<crypto::PublicKey> parseValidatorPubKeyFromKey(const std::string& key, const std::string& prefix) {
    if (key.size() != prefix.size() + crypto::PUBLIC_KEY_SIZE * 2) return std::nullopt;
    std::string hex = key.substr(prefix.size());
    auto bytes = crypto::fromHex(hex);
    if (bytes.size() != crypto::PUBLIC_KEY_SIZE) return std::nullopt;
    crypto::PublicKey pk{};
    std::memcpy(pk.data(), bytes.data(), pk.size());
    return pk;
}

static std::vector<crypto::PublicKey> loadDeterministicValidators(
    database::Database& db,
    const PoeV1Config& cfg,
    const std::vector<crypto::PublicKey>& staticValidators
) {
    std::vector<crypto::PublicKey> pool = normalizeValidatorList(staticValidators);

    std::string mode = toLowerAscii(cfg.validatorMode);
    if (mode != "stake") {
        return pool;
    }

    const std::string idPrefix = "poe:v1:validator:id:";
    auto idKeys = db.keys(idPrefix);
    pool.reserve(pool.size() + idKeys.size());
    for (const auto& k : idKeys) {
        auto pk = parseValidatorPubKeyFromKey(k, idPrefix);
        if (!pk) continue;
        pool.push_back(*pk);
    }
    pool = normalizeValidatorList(pool);

    std::vector<crypto::PublicKey> eligible;
    eligible.reserve(pool.size());
    for (const auto& pk : pool) {
        if (!db.exists(validatorIdentityKey(pk))) continue;
        uint64_t stakeAtoms = readU64LE(db.get(validatorStakeKey(pk)), 0);
        if (stakeAtoms < cfg.validatorMinStakeAtoms) continue;
        eligible.push_back(pk);
    }
    return eligible;
}

static uint32_t effectiveSelectedValidatorCount(const PoeV1Config& cfg, size_t validatorCount) {
    if (validatorCount == 0) return 0;
    uint32_t available = static_cast<uint32_t>(std::min<size_t>(validatorCount, 64));
    if (cfg.adaptiveQuorum && cfg.validatorsN == 0) {
        return available;
    }
    if (cfg.validatorsN == 0) return 0;
    return std::min<uint32_t>(available, cfg.validatorsN);
}

static uint32_t effectiveRequiredVotesForSelected(const PoeV1Config& cfg, uint32_t selectedCount) {
    if (selectedCount == 0) return 0;
    if (!cfg.adaptiveQuorum) {
        if (cfg.validatorsM == 0 || cfg.validatorsM > selectedCount) return 0;
        return cfg.validatorsM;
    }

    if (cfg.adaptiveMajority) {
        uint32_t majority = (selectedCount + 1) / 2;
        return std::max<uint32_t>(1u, majority);
    }

    uint32_t minVotes = std::max<uint32_t>(1, cfg.adaptiveMinVotes);
    uint32_t configured = cfg.validatorsM == 0
        ? selectedCount
        : std::min<uint32_t>(cfg.validatorsM, selectedCount);
    configured = std::max(configured, minVotes);
    return std::min<uint32_t>(configured, selectedCount);
}

static bool pubKeyLess(const crypto::PublicKey& a, const crypto::PublicKey& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

static std::string hex2(uint8_t v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(2);
    s.push_back(d[(v >> 4) & 0xF]);
    s.push_back(d[v & 0xF]);
    return s;
}

static std::vector<uint8_t> u64le(uint64_t v) {
    std::vector<uint8_t> out;
    writeU64LE(out, v);
    return out;
}

struct PoeV1Engine::Impl {
    database::Database db;
    PoeV1Config cfg{};
    std::vector<crypto::PublicKey> validators;
    crypto::Hash256 seed{};
    uint64_t entryCount = 0;
    uint64_t finalizedCount = 0;
    mutable std::mutex mtx;
    std::unordered_map<std::string, uint64_t> authorLastSubmit_;
};

PoeV1Engine::PoeV1Engine() : impl_(std::make_unique<Impl>()) {}
PoeV1Engine::~PoeV1Engine() { close(); }

bool PoeV1Engine::open(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.open(dbPath)) return false;

    std::string ver = impl_->db.getString("meta:poe_v1:version");
    if (ver.empty()) {
        impl_->db.put("meta:poe_v1:version", "1");
        crypto::Hash256 s = crypto::sha256(std::string("synapsenet_poe_v1_genesis"));
        impl_->seed = s;
        impl_->db.put("meta:poe_v1:seed", std::vector<uint8_t>(s.begin(), s.end()));
        std::vector<uint8_t> z;
        writeU64LE(z, 0);
        impl_->db.put("meta:poe_v1:entries", z);
        impl_->db.put("meta:poe_v1:finalized", z);
        impl_->db.put("meta:poe_v1:epoch_id", z);
    } else {
        auto seedData = impl_->db.get("meta:poe_v1:seed");
        if (seedData.size() == impl_->seed.size()) {
            std::memcpy(impl_->seed.data(), seedData.data(), impl_->seed.size());
        } else {
            crypto::Hash256 s = crypto::sha256(std::string("synapsenet_poe_v1_genesis"));
            impl_->seed = s;
            impl_->db.put("meta:poe_v1:seed", std::vector<uint8_t>(s.begin(), s.end()));
        }
        impl_->entryCount = readU64LE(impl_->db.get("meta:poe_v1:entries"), 0);
        impl_->finalizedCount = readU64LE(impl_->db.get("meta:poe_v1:finalized"), 0);
        auto epochData = impl_->db.get("meta:poe_v1:epoch_id");
        if (epochData.empty()) {
            std::vector<uint8_t> z;
            writeU64LE(z, 0);
            impl_->db.put("meta:poe_v1:epoch_id", z);
        }
    }

    {
        auto keys = impl_->db.keys("poe:v1:vote:");
        std::sort(keys.begin(), keys.end());
        for (const auto& k : keys) {
            auto data = impl_->db.get(k);
            if (data.empty()) continue;
            auto v = poe_v1::ValidationVoteV1::deserialize(data);
            if (!v) continue;
            crypto::Hash256 vid = v->payloadHash();
            std::string vidKey = "poe:v1:voteid:" + crypto::toHex(vid);
            if (!impl_->db.exists(vidKey)) {
                impl_->db.put(vidKey, data);
            }
        }
    }

    {
        std::string simVer = impl_->db.getString("meta:poe_v1:simindex_version");
        if (simVer != "3") {
            auto keys = impl_->db.keys("poe:v1:entry:");
            std::sort(keys.begin(), keys.end());
            const std::string prefix = "poe:v1:entry:";
            for (const auto& k : keys) {
                if (k.size() != prefix.size() + 64) continue;
                std::string sidHex = k.substr(prefix.size());
                auto data = impl_->db.get(k);
                if (data.empty()) continue;
                auto e = poe_v1::KnowledgeEntryV1::deserialize(data);
                if (!e) continue;
                uint64_t sh = e->contentSimhash64();
                impl_->db.put("poe:v1:simhash:" + sidHex, u64le(sh));
                impl_->db.put("poe:v1:contentid:" + hex32(e->contentId()), sidHex);
                uint32_t bands = impl_->cfg.noveltyBands == 0 ? 0 : impl_->cfg.noveltyBands;
                if (bands > 16) bands = 16;
                for (uint32_t band = 0; band < bands; ++band) {
                    uint8_t b = static_cast<uint8_t>((sh >> (band * 4)) & 0x0F);
                    std::string bk = "poe:v1:simbucket:" + std::to_string(band) + ":" + hex2(b) + ":" + sidHex;
                    if (!impl_->db.exists(bk)) impl_->db.put(bk, std::vector<uint8_t>{});
                }
            }
            impl_->db.put("meta:poe_v1:simindex_version", "3");
        }
    }

    {
        uint64_t actualEntries = static_cast<uint64_t>(impl_->db.count("poe:v1:entry:"));
        if (impl_->entryCount != actualEntries) {
            impl_->entryCount = actualEntries;
            std::vector<uint8_t> cbuf;
            writeU64LE(cbuf, impl_->entryCount);
            impl_->db.put("meta:poe_v1:entries", cbuf);
        }

        uint64_t actualFinalized = static_cast<uint64_t>(impl_->db.count("poe:v1:final:"));
        if (impl_->finalizedCount != actualFinalized) {
            impl_->finalizedCount = actualFinalized;
            std::vector<uint8_t> fbuf;
            writeU64LE(fbuf, impl_->finalizedCount);
            impl_->db.put("meta:poe_v1:finalized", fbuf);
        }
    }

    return true;
}

void PoeV1Engine::close() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->db.close();
}

void PoeV1Engine::setConfig(const PoeV1Config& cfg) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->cfg = cfg;
    impl_->cfg.validatorMode = toLowerAscii(impl_->cfg.validatorMode);
    if (impl_->cfg.validatorMode != "stake") impl_->cfg.validatorMode = "static";
}

PoeV1Config PoeV1Engine::getConfig() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->cfg;
}

void PoeV1Engine::setStaticValidators(const std::vector<crypto::PublicKey>& validators) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->validators = normalizeValidatorList(validators);
}

std::vector<crypto::PublicKey> PoeV1Engine::getStaticValidators() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->validators;
}

void PoeV1Engine::setValidatorIdentity(const crypto::PublicKey& validator, bool enabled) {
    if (isZeroPubKey(validator)) return;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.isOpen()) return;
    const std::string key = validatorIdentityKey(validator);
    if (enabled) {
        impl_->db.put(key, std::vector<uint8_t>{1});
        const std::string stakeKey = validatorStakeKey(validator);
        if (!impl_->db.exists(stakeKey)) {
            impl_->db.put(stakeKey, u64le(0));
        }
    } else {
        impl_->db.del(key);
    }
}

bool PoeV1Engine::hasValidatorIdentity(const crypto::PublicKey& validator) const {
    if (isZeroPubKey(validator)) return false;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.isOpen()) return false;
    return impl_->db.exists(validatorIdentityKey(validator));
}

void PoeV1Engine::setValidatorStake(const crypto::PublicKey& validator, uint64_t stakeAtoms) {
    if (isZeroPubKey(validator)) return;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.isOpen()) return;
    impl_->db.put(validatorStakeKey(validator), u64le(stakeAtoms));
}

uint64_t PoeV1Engine::getValidatorStake(const crypto::PublicKey& validator) const {
    if (isZeroPubKey(validator)) return 0;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.isOpen()) return 0;
    return readU64LE(impl_->db.get(validatorStakeKey(validator)), 0);
}

std::vector<crypto::PublicKey> PoeV1Engine::getDeterministicValidators() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.isOpen()) {
        return normalizeValidatorList(impl_->validators);
    }
    return loadDeterministicValidators(impl_->db, impl_->cfg, impl_->validators);
}

uint32_t PoeV1Engine::effectiveSelectedValidators() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<crypto::PublicKey> validators;
    if (!impl_->db.isOpen()) {
        validators = normalizeValidatorList(impl_->validators);
    } else {
        validators = loadDeterministicValidators(impl_->db, impl_->cfg, impl_->validators);
    }
    return effectiveSelectedValidatorCount(impl_->cfg, validators.size());
}

uint32_t PoeV1Engine::effectiveRequiredVotes() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<crypto::PublicKey> validators;
    if (!impl_->db.isOpen()) {
        validators = normalizeValidatorList(impl_->validators);
    } else {
        validators = loadDeterministicValidators(impl_->db, impl_->cfg, impl_->validators);
    }
    uint32_t selectedCount = effectiveSelectedValidatorCount(impl_->cfg, validators.size());
    return effectiveRequiredVotesForSelected(impl_->cfg, selectedCount);
}

static crypto::Hash256 rewardIdForAcceptance(const crypto::Hash256& submitId) {
    std::vector<uint8_t> buf;
    const std::string tag = "poe_v1_accept";
    buf.insert(buf.end(), tag.begin(), tag.end());
    buf.insert(buf.end(), submitId.begin(), submitId.end());
    return crypto::sha256(buf.data(), buf.size());
}

static bool passesNoveltyGate(database::Database& db, const PoeV1Config& cfg, uint64_t simhash64, std::string* reason) {
    uint32_t bands = cfg.noveltyBands;
    if (bands == 0) return true;
    if (bands > 16) bands = 16;
    uint32_t maxH = cfg.noveltyMaxHamming;
    if (maxH > 64) maxH = 64;

    std::unordered_set<std::string> candidates;
    candidates.reserve(64);

    for (uint32_t band = 0; band < bands; ++band) {
        uint8_t b = static_cast<uint8_t>((simhash64 >> (band * 4)) & 0x0F);
        std::string prefix = "poe:v1:simbucket:" + std::to_string(band) + ":" + hex2(b) + ":";
        auto keys = db.keys(prefix);
        for (const auto& k : keys) {
            if (k.size() != prefix.size() + 64) continue;
            candidates.insert(k.substr(prefix.size()));
        }
    }

    uint32_t best = 65;
    for (const auto& sidHex : candidates) {
        auto data = db.get("poe:v1:simhash:" + sidHex);
        if (data.size() != 8) continue;
        uint64_t other = readU64LE(data, 0);
        uint32_t d = poe_v1::hammingDistance64(simhash64, other);
        if (d < best) best = d;
        if (best <= maxH) {
            if (reason) {
                if (best == 0) *reason = "exact_duplicate";
                else if (best < (maxH / 2)) *reason = "near_duplicate";
                else *reason = "too_similar";
            }
            return false;
        }
    }

    return true;
}

uint64_t PoeV1Engine::calculateAcceptanceReward(const poe_v1::KnowledgeEntryV1& entry) const {
    PoeV1Config cfg = getConfig();
    uint64_t reward = cfg.acceptanceBaseReward;
    if (entry.powBits > cfg.limits.minPowBits) {
        uint32_t extra = entry.powBits - cfg.limits.minPowBits;
        uint64_t bonus = static_cast<uint64_t>(extra) * static_cast<uint64_t>(cfg.acceptanceBonusPerPowBit);
        reward += bonus;
    }
    uint64_t size = static_cast<uint64_t>(entry.title.size() + entry.body.size());
    if (cfg.acceptanceSizePenaltyBytes > 0) {
        uint64_t chunks = size / static_cast<uint64_t>(cfg.acceptanceSizePenaltyBytes);
        uint64_t pen = chunks * static_cast<uint64_t>(cfg.acceptancePenaltyPerChunk);
        reward = reward > pen ? (reward - pen) : 0;
    }
    if (reward < cfg.acceptanceMinReward) reward = cfg.acceptanceMinReward;
    if (reward > cfg.acceptanceMaxReward) reward = cfg.acceptanceMaxReward;
    return reward;
}

PoeSubmitResult PoeV1Engine::submit(
    const poe_v1::ContentType type,
    const std::string& title,
    const std::string& body,
    const std::vector<crypto::Hash256>& citations,
    const crypto::PrivateKey& authorKey,
    bool autoFinalize
) {
    PoeSubmitResult res;
    PoeV1Config cfg = getConfig();

    // Fast pre-checks before constructing the entry or acquiring any locks
    if (title.size() < 10) {
        res.ok = false;
        res.error = "title_too_short";
        return res;
    }
    if (body.size() < 50) {
        res.ok = false;
        res.error = "body_too_short";
        return res;
    }
    if (body.size() > (1 * 1024 * 1024)) {
        res.ok = false;
        res.error = "body_too_large";
        return res;
    }
    if (citations.size() > cfg.maxCitations) {
        res.ok = false;
        res.error = "too_many_citations";
        return res;
    }

    poe_v1::KnowledgeEntryV1 entry;
    entry.version = 1;
    entry.timestamp = static_cast<uint64_t>(std::time(nullptr));
    entry.authorPubKey = crypto::derivePublicKey(authorKey);
    entry.contentType = type;
    entry.title = title;
    entry.body = body;
    entry.citations = citations;
    entry.powBits = cfg.powBits;
    if (!entry.checkLimits(cfg.limits, &res.error)) {
        res.ok = false;
        return res;
    }

    // Fast fail checks (fail-before expensive PoW)
    if (entry.title.size() > cfg.limits.maxTitleBytes) {
        res.ok = false;
        res.error = "title_too_long";
        return res;
    }
    if (entry.body.size() > cfg.limits.maxBodyBytes) {
        res.ok = false;
        res.error = "body_too_large";
        return res;
    }
    if (entry.citations.size() > cfg.limits.maxCitations) {
        res.ok = false;
        res.error = "too_many_citations";
        return res;
    }

    crypto::Hash256 cidPre = entry.contentId();
    uint64_t shPre = entry.contentSimhash64();
    const crypto::Hash256 bfp = entry.bodyFingerprint();
    const std::string bfpKey = std::string("poe:v1:bodyfp:") + hex32(bfp);
    const std::string ckeyPre = std::string("poe:v1:contentid:") + hex32(cidPre);
    const std::vector<uint8_t> reservationVal = {0x52}; // 'R' reserved marker
    bool reserved = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        if (impl_->db.exists(bfpKey)) {
            res.ok = false;
            res.error = "duplicate_content";
            return res;
        }
        if (impl_->db.exists(ckeyPre)) {
            res.ok = false;
            res.error = "duplicate_content";
            return res;
        }
        if (!passesNoveltyGate(impl_->db, cfg, shPre, &res.error)) {
            res.ok = false;
            return res;
        }
        // rate-limit check per-author
        try {
            std::string authorHex = crypto::toHex(entry.authorPubKey);
            uint64_t now = static_cast<uint64_t>(std::time(nullptr));
            auto it = impl_->authorLastSubmit_.find(authorHex);
            if (it != impl_->authorLastSubmit_.end()) {
                uint64_t last = it->second;
                if (now > last && (now - last) < cfg.minSubmitIntervalSeconds) {
                    res.ok = false;
                    res.error = "rate_limited";
                    return res;
                }
            }
        } catch (...) {}
        for (const auto& cit : entry.citations) {
            std::string citeKey = std::string("poe:v1:contentid:") + hex32(cit);
            auto cur = impl_->db.get(citeKey);
            if (cur.empty() || (cur.size() == 1 && cur[0] == 0x52)) {
                res.ok = false;
                res.error = "unknown_citation";
                return res;
            }
        }
        // create reservation to block concurrent submissions with same content id
        impl_->db.put(ckeyPre, reservationVal);
        reserved = true;
    }

    uint64_t nonce = 0;
    uint64_t attempts = 0;
    uint64_t maxAttempts = cfg.powMaxAttempts == 0 ? UINT64_MAX : cfg.powMaxAttempts;
    bool powFound = false;
    while (attempts < maxAttempts) {
        entry.powNonce = nonce++;
        ++attempts;
        if (poe_v1::hasLeadingZeroBits(entry.submitId(), entry.powBits)) { powFound = true; break; }
    }
    if (!powFound) {
        // cleanup reservation if present
        try {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            std::string ckey = std::string("poe:v1:contentid:") + hex32(cidPre);
            auto cur = impl_->db.get(ckey);
            if (!cur.empty() && cur.size() == 1 && cur[0] == 0x52) {
                impl_->db.del(ckey);
            }
        } catch (...) {}
        res.ok = false;
        res.error = "pow_failed";
        return res;
    }
    poe_v1::signKnowledgeEntryV1(entry, authorKey);
    if (!entry.verifyAll(cfg.limits, &res.error)) {
        res.ok = false;
        return res;
    }

    crypto::Hash256 sid = entry.submitId();
    crypto::Hash256 cid = entry.contentId();

    bool insertionSucceeded = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        std::string key = "poe:v1:entry:" + hex32(sid);
        if (impl_->db.exists(key)) {
            res.ok = false;
            res.error = "duplicate_submit";
            // cleanup reservation if it still points to our marker
            if (reserved) {
                auto cur = impl_->db.get(ckeyPre);
                if (cur == reservationVal) impl_->db.del(ckeyPre);
            }
            return res;
        }
        std::string ckey = "poe:v1:contentid:" + hex32(cid);
        // if content id mapped already and not our reservation, fail
        if (impl_->db.exists(ckey)) {
            auto cur = impl_->db.get(ckey);
            if (!(reserved && cur == reservationVal)) {
                res.ok = false;
                res.error = "duplicate_content";
                // cleanup reservation if it still points to our marker
                if (reserved) {
                    auto cur2 = impl_->db.get(ckeyPre);
                    if (cur2 == reservationVal) impl_->db.del(ckeyPre);
                }
                return res;
            }
        }
        // perform the insertion and replace reservation with real mapping
        impl_->db.put(key, entry.serialize());
        impl_->db.put(ckey, hex32(sid));
        impl_->db.put(bfpKey, hex32(sid));
        uint64_t sh = entry.contentSimhash64();
        impl_->db.put("poe:v1:simhash:" + hex32(sid), u64le(sh));
        uint32_t bands = cfg.noveltyBands == 0 ? 0 : cfg.noveltyBands;
        if (bands > 16) bands = 16;
        for (uint32_t band = 0; band < bands; ++band) {
            uint8_t b = static_cast<uint8_t>((sh >> (band * 4)) & 0x0F);
            impl_->db.put("poe:v1:simbucket:" + std::to_string(band) + ":" + hex2(b) + ":" + hex32(sid), std::vector<uint8_t>{});
        }
        impl_->entryCount += 1;
        std::vector<uint8_t> cbuf;
        writeU64LE(cbuf, impl_->entryCount);
        impl_->db.put("meta:poe_v1:entries", cbuf);
        insertionSucceeded = true;
        // update per-author last submit timestamp
        try {
            std::string authorHex = crypto::toHex(entry.authorPubKey);
            impl_->authorLastSubmit_[authorHex] = entry.timestamp;
        } catch (...) {}
    }

    // If insertion failed for any reason, ensure reservation is cleaned up
    if (!insertionSucceeded && reserved) {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        auto cur = impl_->db.get(ckeyPre);
        if (cur == reservationVal) impl_->db.del(ckeyPre);
    }

    setValidatorIdentity(entry.authorPubKey, true);

    res.ok = true;
    res.submitId = sid;
    res.contentId = cid;
    res.simhash64 = shPre;

    if (autoFinalize) {
        std::vector<crypto::PublicKey> validatorSet = getDeterministicValidators();
        if (validatorSet.empty() && cfg.validatorMode != "stake" && cfg.allowSelfBootstrapValidator) {
            validatorSet.push_back(entry.authorPubKey);
            setStaticValidators(validatorSet);
            validatorSet = getDeterministicValidators();
        }
        if (!validatorSet.empty()) {
            uint32_t selectedCount = effectiveSelectedValidatorCount(cfg, validatorSet.size());
            std::vector<crypto::PublicKey> selected = poe_v1::selectValidators(chainSeed(), sid, validatorSet, selectedCount);
            auto it = std::find(selected.begin(), selected.end(), entry.authorPubKey);
            if (it != selected.end()) {
                poe_v1::ValidationVoteV1 v;
                v.version = 1;
                v.submitId = sid;
                v.prevBlockHash = chainSeed();
                v.flags = 0;
                v.scores = {100, 100, 100};
                poe_v1::signValidationVoteV1(v, authorKey);
                addVote(v);
            }
            auto fin = finalize(sid);
            if (fin) {
                res.finalized = true;
                res.acceptanceReward = calculateAcceptanceReward(entry);
            }
        }
    }

    return res;
}

bool PoeV1Engine::precheckEntry(const poe_v1::KnowledgeEntryV1& entry, std::string* reason) const {
    PoeV1Config cfg = getConfig();
    // Very small/invalid submissions should be rejected early
    if (entry.title.size() < 10) {
        if (reason) *reason = "title_too_short";
        return false;
    }
    if (entry.body.size() < 50) {
        if (reason) *reason = "body_too_short";
        return false;
    }
    if (entry.body.size() > (1 * 1024 * 1024)) {
        if (reason) *reason = "body_too_large";
        return false;
    }
    // non-printable / control character scan (allow tab, LF, CR)
    size_t nonPrintable = 0;
    for (unsigned char c : entry.body) {
        if (c == 0x09 || c == 0x0A || c == 0x0D) continue;
        if (c == 0x7F) { ++nonPrintable; continue; }
        if (std::iscntrl(c)) ++nonPrintable;
    }
    if (!entry.body.empty() && (static_cast<double>(nonPrintable) / static_cast<double>(entry.body.size()) > 0.30)) {
        if (reason) *reason = "non_printable_content";
        return false;
    }

    if (!entry.verifyAll(cfg.limits, reason)) return false;

    crypto::Hash256 sid = entry.submitId();
    crypto::Hash256 cid = entry.contentId();
    uint64_t sh = entry.contentSimhash64();

    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string ckey = "poe:v1:contentid:" + hex32(cid);
    if (impl_->db.exists(ckey)) {
        if (reason) *reason = "duplicate_content";
        return false;
    }
    if (!passesNoveltyGate(impl_->db, cfg, sh, reason)) return false;
    std::string key = "poe:v1:entry:" + hex32(sid);
    if (impl_->db.exists(key)) {
        if (reason) *reason = "duplicate_submit";
        return false;
    }
    return true;
}

bool PoeV1Engine::importEntry(const poe_v1::KnowledgeEntryV1& entry, std::string* reason) {
    PoeV1Config cfg = getConfig();
    if (!entry.verifyAll(cfg.limits, reason)) return false;

    crypto::Hash256 sid = entry.submitId();
    crypto::Hash256 cid = entry.contentId();
    uint64_t sh = entry.contentSimhash64();
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        std::string ckey = "poe:v1:contentid:" + hex32(cid);
        if (impl_->db.exists(ckey)) {
            if (reason) *reason = "duplicate_content";
            return false;
        }
        if (!passesNoveltyGate(impl_->db, cfg, sh, reason)) return false;
        std::string key = "poe:v1:entry:" + hex32(sid);
        if (impl_->db.exists(key)) {
            if (reason) *reason = "duplicate_submit";
            return false;
        }
        impl_->db.put(key, entry.serialize());
        impl_->db.put("poe:v1:contentid:" + hex32(cid), hex32(sid));
        impl_->db.put("poe:v1:simhash:" + hex32(sid), u64le(sh));
        uint32_t bands = cfg.noveltyBands == 0 ? 0 : cfg.noveltyBands;
        if (bands > 16) bands = 16;
        for (uint32_t band = 0; band < bands; ++band) {
            uint8_t b = static_cast<uint8_t>((sh >> (band * 4)) & 0x0F);
            impl_->db.put("poe:v1:simbucket:" + std::to_string(band) + ":" + hex2(b) + ":" + hex32(sid), std::vector<uint8_t>{});
        }
        impl_->entryCount += 1;
        std::vector<uint8_t> cbuf;
        writeU64LE(cbuf, impl_->entryCount);
        impl_->db.put("meta:poe_v1:entries", cbuf);
    }

    setValidatorIdentity(entry.authorPubKey, true);

    return true;
}

bool PoeV1Engine::addVote(const poe_v1::ValidationVoteV1& vote) {
    if (vote.version != 1) return false;
    if (!vote.verifySignature()) return false;

    crypto::Hash256 seed{};
    std::vector<crypto::PublicKey> validators;
    PoeV1Config cfg{};
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        seed = impl_->seed;
        validators = loadDeterministicValidators(impl_->db, impl_->cfg, impl_->validators);
        cfg = impl_->cfg;
    }
    uint32_t validatorsN = effectiveSelectedValidatorCount(cfg, validators.size());
    if (vote.prevBlockHash != seed) return false;
    if (validators.empty() || validatorsN == 0) return false;

    auto selected = poe_v1::selectValidators(seed, vote.submitId, validators, validatorsN);
    if (selected.empty()) return false;
    if (std::find(selected.begin(), selected.end(), vote.validatorPubKey) == selected.end()) return false;

    std::string submitHex = crypto::toHex(vote.submitId);
    std::string validatorHex = crypto::toHex(vote.validatorPubKey);
    crypto::Hash256 voteId = vote.payloadHash();
    std::string voteIdHex = crypto::toHex(voteId);

    auto data = vote.serialize();

    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.exists("poe:v1:entry:" + submitHex)) return false;
    if (impl_->db.exists("poe:v1:final:" + submitHex)) return false;
    std::string key = "poe:v1:vote:" + submitHex + ":" + validatorHex;
    if (impl_->db.exists(key)) return false;
    if (impl_->db.exists("poe:v1:voteid:" + voteIdHex)) return false;
    impl_->db.put(key, data);
    impl_->db.put("poe:v1:voteid:" + voteIdHex, data);
    impl_->db.put(validatorIdentityKey(vote.validatorPubKey), std::vector<uint8_t>{1});
    if (!impl_->db.exists(validatorStakeKey(vote.validatorPubKey))) {
        impl_->db.put(validatorStakeKey(vote.validatorPubKey), u64le(0));
    }
    return true;
}

std::optional<poe_v1::ValidationVoteV1> PoeV1Engine::getVoteById(const crypto::Hash256& voteId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto data = impl_->db.get("poe:v1:voteid:" + crypto::toHex(voteId));
    if (data.empty()) return std::nullopt;
    return poe_v1::ValidationVoteV1::deserialize(data);
}

std::vector<crypto::Hash256> PoeV1Engine::listEntryIds(size_t limit) const {
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        keys = impl_->db.keys("poe:v1:entry:");
    }
    std::sort(keys.begin(), keys.end());

    const std::string prefix = "poe:v1:entry:";
    std::vector<crypto::Hash256> out;
    out.reserve(keys.size());
    for (const auto& k : keys) {
        if (k.size() != prefix.size() + 64) continue;
        auto bytes = crypto::fromHex(k.substr(prefix.size()));
        if (bytes.size() != crypto::SHA256_SIZE) continue;
        crypto::Hash256 h{};
        std::memcpy(h.data(), bytes.data(), h.size());
        out.push_back(h);
    }

    if (limit > 0 && out.size() > limit) {
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(limit));
    }
    return out;
}

std::vector<crypto::Hash256> PoeV1Engine::listVoteIds(size_t limit) const {
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        keys = impl_->db.keys("poe:v1:voteid:");
    }
    std::sort(keys.begin(), keys.end());

    const std::string prefix = "poe:v1:voteid:";
    std::vector<crypto::Hash256> out;
    out.reserve(keys.size());
    for (const auto& k : keys) {
        if (k.size() != prefix.size() + 64) continue;
        auto bytes = crypto::fromHex(k.substr(prefix.size()));
        if (bytes.size() != crypto::SHA256_SIZE) continue;
        crypto::Hash256 h{};
        std::memcpy(h.data(), bytes.data(), h.size());
        out.push_back(h);
    }

    if (limit > 0 && out.size() > limit) {
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(limit));
    }
    return out;
}

std::vector<poe_v1::ValidationVoteV1> PoeV1Engine::getVotesForSubmit(const crypto::Hash256& submitId) const {
    std::string prefix = "poe:v1:vote:" + crypto::toHex(submitId) + ":";
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        keys = impl_->db.keys(prefix);
    }
    std::sort(keys.begin(), keys.end());

    std::vector<poe_v1::ValidationVoteV1> votes;
    votes.reserve(keys.size());
    for (const auto& k : keys) {
        std::vector<uint8_t> data;
        {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            data = impl_->db.get(k);
        }
        auto v = poe_v1::ValidationVoteV1::deserialize(data);
        if (!v) continue;
        votes.push_back(*v);
    }
    return votes;
}

std::optional<poe_v1::KnowledgeEntryV1> PoeV1Engine::getEntry(const crypto::Hash256& submitId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto data = impl_->db.get("poe:v1:entry:" + crypto::toHex(submitId));
    if (data.empty()) return std::nullopt;
    return poe_v1::KnowledgeEntryV1::deserialize(data);
}

std::optional<crypto::Hash256> PoeV1Engine::getSubmitIdByContentId(const crypto::Hash256& contentId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string sidHex = impl_->db.getString("poe:v1:contentid:" + hex32(contentId));
    if (sidHex.size() != 64) return std::nullopt;
    auto bytes = crypto::fromHex(sidHex);
    if (bytes.size() != crypto::SHA256_SIZE) return std::nullopt;
    crypto::Hash256 sid{};
    std::memcpy(sid.data(), bytes.data(), sid.size());
    return sid;
}

std::optional<poe_v1::KnowledgeEntryV1> PoeV1Engine::getEntryByContentId(const crypto::Hash256& contentId) const {
    std::vector<uint8_t> data;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        std::string sidHex = impl_->db.getString("poe:v1:contentid:" + hex32(contentId));
        if (sidHex.size() != 64) return std::nullopt;
        data = impl_->db.get("poe:v1:entry:" + sidHex);
    }
    if (data.empty()) return std::nullopt;
    return poe_v1::KnowledgeEntryV1::deserialize(data);
}

std::optional<poe_v1::FinalizationRecordV1> PoeV1Engine::getFinalization(const crypto::Hash256& submitId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto data = impl_->db.get("poe:v1:final:" + crypto::toHex(submitId));
    if (data.empty()) return std::nullopt;
    return poe_v1::FinalizationRecordV1::deserialize(data);
}

bool PoeV1Engine::isFinalized(const crypto::Hash256& submitId) const {
    return getFinalization(submitId).has_value();
}

uint64_t PoeV1Engine::totalEntries() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->entryCount;
}

uint64_t PoeV1Engine::totalFinalized() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->finalizedCount;
}

crypto::Hash256 PoeV1Engine::chainSeed() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->seed;
}

std::optional<poe_v1::FinalizationRecordV1> PoeV1Engine::finalize(const crypto::Hash256& submitId) {
    PoeV1Config cfg = getConfig();

    auto entryOpt = getEntry(submitId);
    if (!entryOpt) return std::nullopt;
    auto existing = getFinalization(submitId);
    if (existing) return existing;

    std::vector<crypto::PublicKey> validatorSet = getDeterministicValidators();
    if (validatorSet.empty()) return std::nullopt;

    crypto::Hash256 seed = chainSeed();
    uint32_t selectedCount = effectiveSelectedValidatorCount(cfg, validatorSet.size());
    std::vector<crypto::PublicKey> selected = poe_v1::selectValidators(seed, submitId, validatorSet, selectedCount);
    uint32_t requiredVotes = effectiveRequiredVotesForSelected(cfg, static_cast<uint32_t>(selected.size()));
    if (requiredVotes == 0) return std::nullopt;
    crypto::Hash256 vsetHash = poe_v1::validatorSetHashV1(selected);

    std::string prefix = "poe:v1:vote:" + crypto::toHex(submitId) + ":";
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        keys = impl_->db.keys(prefix);
    }
    std::sort(keys.begin(), keys.end());

    std::vector<poe_v1::ValidationVoteV1> votes;
    votes.reserve(keys.size());

    for (const auto& k : keys) {
        std::vector<uint8_t> data;
        {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            data = impl_->db.get(k);
        }
        auto v = poe_v1::ValidationVoteV1::deserialize(data);
        if (!v) continue;
        if (v->submitId != submitId) continue;
        if (v->prevBlockHash != seed) continue;
        if (!v->verifySignature()) continue;
        if ((v->flags & 0x1u) != 0) return std::nullopt;
        auto it = std::find(selected.begin(), selected.end(), v->validatorPubKey);
        if (it == selected.end()) continue;
        votes.push_back(*v);
    }

    std::sort(votes.begin(), votes.end(), [](const poe_v1::ValidationVoteV1& a, const poe_v1::ValidationVoteV1& b) {
        return std::lexicographical_compare(a.validatorPubKey.begin(), a.validatorPubKey.end(),
                                           b.validatorPubKey.begin(), b.validatorPubKey.end());
    });
    votes.erase(std::unique(votes.begin(), votes.end(), [](const poe_v1::ValidationVoteV1& a, const poe_v1::ValidationVoteV1& b) {
        return a.validatorPubKey == b.validatorPubKey;
    }), votes.end());

    if (votes.size() < requiredVotes) return std::nullopt;

    poe_v1::FinalizationRecordV1 fin;
    fin.submitId = submitId;
    fin.prevBlockHash = seed;
    fin.validatorSetHash = vsetHash;
    fin.votes = votes;
    fin.finalizedAt = entryOpt->timestamp;

    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        std::string key = "poe:v1:final:" + crypto::toHex(submitId);
        if (impl_->db.exists(key)) return poe_v1::FinalizationRecordV1::deserialize(impl_->db.get(key));
        impl_->db.put(key, fin.serialize());
        impl_->finalizedCount += 1;
        std::vector<uint8_t> fbuf;
        writeU64LE(fbuf, impl_->finalizedCount);
        impl_->db.put("meta:poe_v1:finalized", fbuf);
        impl_->db.put("poe:v1:reward_id:" + crypto::toHex(submitId), std::vector<uint8_t>(rewardIdForAcceptance(submitId).begin(), rewardIdForAcceptance(submitId).end()));
    }

    return fin;
}

static crypto::Hash256 rewardIdForEpoch(uint64_t epochId, const crypto::Hash256& contentId) {
    std::vector<uint8_t> buf;
    const std::string tag = "poe_v1_epoch";
    buf.insert(buf.end(), tag.begin(), tag.end());
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((epochId >> (8 * i)) & 0xFF));
    buf.insert(buf.end(), contentId.begin(), contentId.end());
    return crypto::sha256(buf.data(), buf.size());
}

static crypto::Hash256 epochSeedFrom(uint64_t epochId, const crypto::Hash256& chainSeed) {
    std::vector<uint8_t> buf;
    const std::string tag = "poe_v1_epoch_seed";
    buf.insert(buf.end(), tag.begin(), tag.end());
    buf.insert(buf.end(), chainSeed.begin(), chainSeed.end());
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((epochId >> (8 * i)) & 0xFF));
    return crypto::sha256(buf.data(), buf.size());
}

static crypto::Hash256 allocationHashV1(const std::vector<PoeEpochAllocation>& allocs) {
    std::vector<PoeEpochAllocation> sorted = allocs;
    std::sort(sorted.begin(), sorted.end(), [](const PoeEpochAllocation& a, const PoeEpochAllocation& b) {
        return std::lexicographical_compare(a.contentId.begin(), a.contentId.end(), b.contentId.begin(), b.contentId.end());
    });
    std::vector<uint8_t> buf;
    buf.reserve(sorted.size() * (32 + 8));
    for (const auto& a : sorted) {
        buf.insert(buf.end(), a.contentId.begin(), a.contentId.end());
        writeU64LE(buf, a.amount);
    }
    return crypto::sha256(buf.data(), buf.size());
}

struct EpochNode {
    crypto::Hash256 submitId{};
    crypto::Hash256 contentId{};
    crypto::PublicKey author{};
    std::vector<crypto::Hash256> citations;
};

static bool computeEpochAllocationsFromNodes(
    std::vector<EpochNode> nodes,
    uint64_t totalBudget,
    uint32_t iterations,
    std::vector<PoeEpochAllocation>* outAllocations,
    crypto::Hash256* outAllocHash,
    std::string* outError
) {
    if (totalBudget == 0) {
        if (outError) *outError = "budget_zero";
        return false;
    }
    if (iterations == 0) iterations = 1;
    if (nodes.empty()) {
        if (outError) *outError = "no_finalized_entries";
        return false;
    }

    std::sort(nodes.begin(), nodes.end(), [](const EpochNode& a, const EpochNode& b) {
        return std::lexicographical_compare(a.contentId.begin(), a.contentId.end(), b.contentId.begin(), b.contentId.end());
    });
    nodes.erase(std::unique(nodes.begin(), nodes.end(), [](const EpochNode& a, const EpochNode& b) {
        return a.contentId == b.contentId;
    }), nodes.end());

    const size_t N = nodes.size();
    if (N == 0) {
        if (outError) *outError = "no_finalized_entries";
        return false;
    }

    std::vector<std::string> contentHex(N);
    for (size_t i = 0; i < N; ++i) contentHex[i] = crypto::toHex(nodes[i].contentId);
    std::vector<std::pair<std::string, size_t>> idx;
    idx.reserve(N);
    for (size_t i = 0; i < N; ++i) idx.emplace_back(contentHex[i], i);
    std::sort(idx.begin(), idx.end());

    auto findIndex = [&](const crypto::Hash256& cid) -> std::optional<size_t> {
        std::string h = crypto::toHex(cid);
        auto it = std::lower_bound(idx.begin(), idx.end(), std::make_pair(h, static_cast<size_t>(0)));
        if (it == idx.end() || it->first != h) return std::nullopt;
        return it->second;
    };

    std::vector<std::vector<size_t>> outTargets(N);
    for (size_t i = 0; i < N; ++i) {
        std::vector<size_t> targets;
        targets.reserve(nodes[i].citations.size());
        for (const auto& c : nodes[i].citations) {
            auto ti = findIndex(c);
            if (!ti) continue;
            if (nodes[*ti].author == nodes[i].author) continue;
            targets.push_back(*ti);
        }
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        outTargets[i] = std::move(targets);
    }

    static constexpr uint64_t SCALE = 1000000000000ULL;
    static constexpr uint32_t D_NUM = 85;
    static constexpr uint32_t D_DEN = 100;

    std::vector<uint64_t> score(N, 0);
    uint64_t base = SCALE / static_cast<uint64_t>(N);
    uint64_t remBase = SCALE % static_cast<uint64_t>(N);
    for (size_t i = 0; i < N; ++i) score[i] = base + (i < static_cast<size_t>(remBase) ? 1ULL : 0ULL);

    for (uint32_t it = 0; it < iterations; ++it) {
        std::vector<uint64_t> inbound(N, 0);
        uint64_t danglingMass = 0;

        for (size_t j = 0; j < N; ++j) {
            const auto& outs = outTargets[j];
            if (outs.empty()) {
                danglingMass += score[j];
                continue;
            }
            uint64_t share = score[j] / static_cast<uint64_t>(outs.size());
            uint64_t rem = score[j] % static_cast<uint64_t>(outs.size());
            for (size_t k = 0; k < outs.size(); ++k) {
                inbound[outs[k]] += share + (k < static_cast<size_t>(rem) ? 1ULL : 0ULL);
            }
        }

        if (danglingMass > 0) {
            uint64_t share = danglingMass / static_cast<uint64_t>(N);
            uint64_t rem = danglingMass % static_cast<uint64_t>(N);
            for (size_t i = 0; i < N; ++i) inbound[i] += share + (i < static_cast<size_t>(rem) ? 1ULL : 0ULL);
        }

        std::vector<uint64_t> damped(N, 0);
        std::vector<uint32_t> remainders(N, 0);
        uint64_t remSum = 0;
        for (size_t i = 0; i < N; ++i) {
            unsigned __int128 tmp = static_cast<unsigned __int128>(inbound[i]) * static_cast<unsigned __int128>(D_NUM);
            damped[i] = static_cast<uint64_t>(tmp / static_cast<unsigned __int128>(D_DEN));
            uint32_t r = static_cast<uint32_t>(tmp % static_cast<unsigned __int128>(D_DEN));
            remainders[i] = r;
            remSum += r;
        }
        uint64_t lost = remSum / static_cast<uint64_t>(D_DEN);
        if (lost > 0) {
            std::vector<size_t> ord(N);
            std::iota(ord.begin(), ord.end(), 0);
            std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
                if (remainders[a] != remainders[b]) return remainders[a] > remainders[b];
                return a < b;
            });
            for (uint64_t k = 0; k < lost && k < ord.size(); ++k) {
                damped[ord[static_cast<size_t>(k)]] += 1;
            }
        }

        uint64_t dampedTotal = (SCALE / static_cast<uint64_t>(D_DEN)) * static_cast<uint64_t>(D_NUM);
        uint64_t baseTotal = SCALE - dampedTotal;
        uint64_t baseEach = baseTotal / static_cast<uint64_t>(N);
        uint64_t baseRem2 = baseTotal % static_cast<uint64_t>(N);

        std::vector<uint64_t> nextScore(N, 0);
        for (size_t i = 0; i < N; ++i) {
            nextScore[i] = damped[i] + baseEach + (i < static_cast<size_t>(baseRem2) ? 1ULL : 0ULL);
        }
        score.swap(nextScore);
    }

    std::vector<uint64_t> amounts(N, 0);
    std::vector<uint64_t> amountRemainders(N, 0);
    uint64_t sumAmounts = 0;
    for (size_t i = 0; i < N; ++i) {
        unsigned __int128 prod = static_cast<unsigned __int128>(totalBudget) * static_cast<unsigned __int128>(score[i]);
        uint64_t amt = static_cast<uint64_t>(prod / static_cast<unsigned __int128>(SCALE));
        uint64_t rem = static_cast<uint64_t>(prod % static_cast<unsigned __int128>(SCALE));
        amounts[i] = amt;
        amountRemainders[i] = rem;
        sumAmounts += amt;
    }
    uint64_t leftover = totalBudget - sumAmounts;
    if (leftover > 0) {
        std::vector<size_t> ord(N);
        std::iota(ord.begin(), ord.end(), 0);
        std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
            if (amountRemainders[a] != amountRemainders[b]) return amountRemainders[a] > amountRemainders[b];
            return a < b;
        });
        for (uint64_t k = 0; k < leftover && k < ord.size(); ++k) {
            amounts[ord[static_cast<size_t>(k)]] += 1;
        }
    }

    std::vector<PoeEpochAllocation> allocations;
    allocations.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        if (amounts[i] == 0) continue;
        PoeEpochAllocation a;
        a.submitId = nodes[i].submitId;
        a.contentId = nodes[i].contentId;
        a.authorPubKey = nodes[i].author;
        a.score = score[i];
        a.amount = amounts[i];
        allocations.push_back(a);
    }

    if (outAllocHash) *outAllocHash = allocationHashV1(allocations);
    if (outAllocations) *outAllocations = std::move(allocations);
    return true;
}

PoeEpochResult PoeV1Engine::runEpoch(uint64_t totalBudget, uint32_t iterations) {
    PoeEpochResult res;
    if (totalBudget == 0) {
        res.ok = false;
        res.error = "budget_zero";
        return res;
    }
    if (iterations == 0) iterations = 1;

    std::vector<crypto::Hash256> submitIds;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        auto keys = impl_->db.keys("poe:v1:final:");
        std::sort(keys.begin(), keys.end());
        submitIds.reserve(keys.size());
        const std::string prefix = "poe:v1:final:";
        for (const auto& k : keys) {
            if (k.size() != prefix.size() + 64) continue;
            std::string hex = k.substr(prefix.size());
            auto bytes = crypto::fromHex(hex);
            if (bytes.size() != crypto::SHA256_SIZE) continue;
            crypto::Hash256 sid{};
            std::memcpy(sid.data(), bytes.data(), sid.size());
            submitIds.push_back(sid);
        }
    }

    std::vector<EpochNode> nodes;
    nodes.reserve(submitIds.size());
    for (const auto& sid : submitIds) {
        auto entryOpt = getEntry(sid);
        if (!entryOpt) continue;
        EpochNode n;
        n.submitId = sid;
        n.contentId = entryOpt->contentId();
        n.author = entryOpt->authorPubKey;
        n.citations = entryOpt->citations;
        nodes.push_back(std::move(n));
    }

    std::vector<PoeEpochAllocation> allocations;
    crypto::Hash256 allocHash{};
    if (!computeEpochAllocationsFromNodes(std::move(nodes), totalBudget, iterations, &allocations, &allocHash, &res.error)) {
        res.ok = false;
        return res;
    }

    uint64_t epochId = 0;
    crypto::Hash256 chain = chainSeed();
    crypto::Hash256 epochSeed{};
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        epochId = readU64LE(impl_->db.get("meta:poe_v1:epoch_id"), 0) + 1;
        std::vector<uint8_t> ebuf;
        writeU64LE(ebuf, epochId);
        impl_->db.put("meta:poe_v1:epoch_id", ebuf);
    }

    epochSeed = epochSeedFrom(epochId, chain);

    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        std::vector<uint8_t> rec;
        writeU64LE(rec, epochId);
        writeU32LE(rec, iterations);
        rec.insert(rec.end(), epochSeed.begin(), epochSeed.end());
        writeU64LE(rec, totalBudget);
        rec.insert(rec.end(), allocHash.begin(), allocHash.end());
        writeU32LE(rec, static_cast<uint32_t>(allocations.size()));
        impl_->db.put("poe:v1:epoch:" + std::to_string(epochId), rec);

        for (const auto& a : allocations) {
            std::vector<uint8_t> v;
            writeU64LE(v, a.amount);
            writeU64LE(v, a.score);
            v.insert(v.end(), a.authorPubKey.begin(), a.authorPubKey.end());
            v.insert(v.end(), a.submitId.begin(), a.submitId.end());
            impl_->db.put("poe:v1:epoch_alloc:" + std::to_string(epochId) + ":" + crypto::toHex(a.contentId), v);
            impl_->db.put("poe:v1:epoch_reward_id:" + std::to_string(epochId) + ":" + crypto::toHex(a.contentId),
                          std::vector<uint8_t>(rewardIdForEpoch(epochId, a.contentId).begin(), rewardIdForEpoch(epochId, a.contentId).end()));
        }
    }

    res.ok = true;
    res.epochId = epochId;
    res.iterations = iterations;
    res.epochSeed = epochSeed;
    res.totalBudget = totalBudget;
    res.allocationHash = allocHash;
    res.allocations = std::move(allocations);
    return res;
}

std::vector<uint64_t> PoeV1Engine::listEpochIds(size_t limit) const {
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        keys = impl_->db.keys("poe:v1:epoch:");
    }
    std::sort(keys.begin(), keys.end());

    const std::string prefix = "poe:v1:epoch:";
    std::vector<uint64_t> out;
    out.reserve(keys.size());
    for (const auto& k : keys) {
        if (k.size() <= prefix.size()) continue;
        const std::string tail = k.substr(prefix.size());
        char* end = nullptr;
        unsigned long long v = std::strtoull(tail.c_str(), &end, 10);
        if (end == nullptr || *end != '\0') continue;
        out.push_back(static_cast<uint64_t>(v));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());

    if (limit > 0 && out.size() > limit) {
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(limit));
    }
    return out;
}

std::optional<PoeEpochResult> PoeV1Engine::getEpoch(uint64_t epochId) const {
    std::vector<uint8_t> rec;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        rec = impl_->db.get("poe:v1:epoch:" + std::to_string(epochId));
    }
    if (rec.size() < 84) return std::nullopt;

    size_t off = 0;
    uint64_t storedId = readU64LEAt(rec, off, 0);
    off += 8;

    uint32_t iterations = 20;
    crypto::Hash256 epochSeed{};
    uint64_t totalBudget = 0;
    crypto::Hash256 allocHash{};
    uint32_t allocCount = 0;

    if (rec.size() >= 88) {
        iterations = readU32LEAt(rec, off, 0);
        off += 4;
    }

    if (rec.size() < off + epochSeed.size() + 8 + allocHash.size() + 4) return std::nullopt;
    std::memcpy(epochSeed.data(), rec.data() + off, epochSeed.size());
    off += epochSeed.size();
    totalBudget = readU64LEAt(rec, off, 0);
    off += 8;
    std::memcpy(allocHash.data(), rec.data() + off, allocHash.size());
    off += allocHash.size();
    allocCount = readU32LEAt(rec, off, 0);

    std::vector<std::string> allocKeys;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        allocKeys = impl_->db.keys("poe:v1:epoch_alloc:" + std::to_string(epochId) + ":");
    }
    std::sort(allocKeys.begin(), allocKeys.end());

    const std::string allocPrefix = "poe:v1:epoch_alloc:" + std::to_string(epochId) + ":";
    std::vector<PoeEpochAllocation> allocations;
    allocations.reserve(allocKeys.size());
    for (const auto& k : allocKeys) {
        if (k.size() != allocPrefix.size() + 64) continue;
        auto cidBytes = crypto::fromHex(k.substr(allocPrefix.size()));
        if (cidBytes.size() != crypto::SHA256_SIZE) continue;
        crypto::Hash256 contentId{};
        std::memcpy(contentId.data(), cidBytes.data(), contentId.size());

        std::vector<uint8_t> v;
        {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            v = impl_->db.get(k);
        }
        if (v.size() < 16 + crypto::PublicKey{}.size() + crypto::Hash256{}.size()) continue;
        size_t voff = 0;
        uint64_t amount = readU64LEAt(v, voff, 0);
        voff += 8;
        uint64_t score = readU64LEAt(v, voff, 0);
        voff += 8;

        crypto::PublicKey author{};
        std::memcpy(author.data(), v.data() + voff, author.size());
        voff += author.size();

        crypto::Hash256 submitId{};
        std::memcpy(submitId.data(), v.data() + voff, submitId.size());

        PoeEpochAllocation a;
        a.submitId = submitId;
        a.contentId = contentId;
        a.authorPubKey = author;
        a.score = score;
        a.amount = amount;
        allocations.push_back(a);
    }

    if (allocCount != 0 && allocations.size() != static_cast<size_t>(allocCount)) return std::nullopt;

    PoeEpochResult out;
    out.ok = true;
    out.epochId = storedId != 0 ? storedId : epochId;
    out.iterations = iterations;
    out.epochSeed = epochSeed;
    out.totalBudget = totalBudget;
    out.allocationHash = allocHash;
    out.allocations = std::move(allocations);
    return out;
}

bool PoeV1Engine::importEpoch(const PoeEpochResult& epoch) {
    if (!epoch.ok) return false;
    if (epoch.epochId == 0) return false;
    if (epoch.totalBudget == 0) return false;
    if (epoch.iterations == 0) return false;

    crypto::Hash256 expectedSeed = epochSeedFrom(epoch.epochId, chainSeed());
    if (expectedSeed != epoch.epochSeed) return false;

    if (!epoch.allocations.empty()) {
        if (allocationHashV1(epoch.allocations) != epoch.allocationHash) return false;
    }

    std::vector<crypto::Hash256> submitIds;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        auto keys = impl_->db.keys("poe:v1:final:");
        std::sort(keys.begin(), keys.end());
        submitIds.reserve(keys.size());
        const std::string prefix = "poe:v1:final:";
        for (const auto& k : keys) {
            if (k.size() != prefix.size() + 64) continue;
            std::string hex = k.substr(prefix.size());
            auto bytes = crypto::fromHex(hex);
            if (bytes.size() != crypto::SHA256_SIZE) continue;
            crypto::Hash256 sid{};
            std::memcpy(sid.data(), bytes.data(), sid.size());
            submitIds.push_back(sid);
        }
    }

    std::vector<EpochNode> nodes;
    nodes.reserve(submitIds.size());
    for (const auto& sid : submitIds) {
        auto entryOpt = getEntry(sid);
        if (!entryOpt) continue;
        EpochNode n;
        n.submitId = sid;
        n.contentId = entryOpt->contentId();
        n.author = entryOpt->authorPubKey;
        n.citations = entryOpt->citations;
        nodes.push_back(std::move(n));
    }

    std::string err;
    std::vector<PoeEpochAllocation> computed;
    crypto::Hash256 computedHash{};
    if (!computeEpochAllocationsFromNodes(std::move(nodes), epoch.totalBudget, epoch.iterations, &computed, &computedHash, &err)) {
        return false;
    }
    if (computedHash != epoch.allocationHash) return false;

    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        uint64_t cur = readU64LE(impl_->db.get("meta:poe_v1:epoch_id"), 0);
        std::string epochKey = "poe:v1:epoch:" + std::to_string(epoch.epochId);
        if (epoch.epochId <= cur) return impl_->db.exists(epochKey);
        if (epoch.epochId != cur + 1) return false;

        std::vector<uint8_t> ebuf;
        writeU64LE(ebuf, epoch.epochId);
        impl_->db.put("meta:poe_v1:epoch_id", ebuf);

        std::vector<uint8_t> rec;
        writeU64LE(rec, epoch.epochId);
        writeU32LE(rec, epoch.iterations);
        rec.insert(rec.end(), epoch.epochSeed.begin(), epoch.epochSeed.end());
        writeU64LE(rec, epoch.totalBudget);
        rec.insert(rec.end(), epoch.allocationHash.begin(), epoch.allocationHash.end());
        writeU32LE(rec, static_cast<uint32_t>(computed.size()));
        impl_->db.put(epochKey, rec);

        for (const auto& a : computed) {
            std::vector<uint8_t> v;
            writeU64LE(v, a.amount);
            writeU64LE(v, a.score);
            v.insert(v.end(), a.authorPubKey.begin(), a.authorPubKey.end());
            v.insert(v.end(), a.submitId.begin(), a.submitId.end());
            impl_->db.put("poe:v1:epoch_alloc:" + std::to_string(epoch.epochId) + ":" + crypto::toHex(a.contentId), v);
            impl_->db.put("poe:v1:epoch_reward_id:" + std::to_string(epoch.epochId) + ":" + crypto::toHex(a.contentId),
                          std::vector<uint8_t>(rewardIdForEpoch(epoch.epochId, a.contentId).begin(), rewardIdForEpoch(epoch.epochId, a.contentId).end()));
        }
    }

    return true;
}

}
