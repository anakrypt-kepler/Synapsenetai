#include "core/knowledge.h"
#include "database/database.h"
#include "utils/config.h"
#include <mutex>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <fstream>

namespace synapse {
namespace core {

namespace {

KnowledgeConfig loadKnowledgeConfig() {
    KnowledgeConfig config;
    const int configuredMinValidations = utils::Config::instance().getInt(
        "knowledge.min_validations_required",
        static_cast<int>(config.minValidationsRequired));
    if (configuredMinValidations >= 0) {
        config.minValidationsRequired = static_cast<uint32_t>(configuredMinValidations);
    }
    return config;
}

uint32_t resolveMinValidationsRequired(uint32_t configuredValue) {
    return configuredValue;
}

} // namespace



static void writeU64(std::vector<uint8_t>& out, uint64_t val) {
    for (int i = 0; i < 8; i++) out.push_back((val >> (i * 8)) & 0xff);
}

static uint64_t readU64(const uint8_t* p) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) val |= static_cast<uint64_t>(p[i]) << (i * 8);
    return val;
}

static void writeU32(std::vector<uint8_t>& out, uint32_t val) {
    for (int i = 0; i < 4; i++) out.push_back((val >> (i * 8)) & 0xff);
}

static uint32_t readU32(const uint8_t* p) {
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) val |= static_cast<uint32_t>(p[i]) << (i * 8);
    return val;
}

static void writeString(std::vector<uint8_t>& out, const std::string& s) {
    writeU32(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

static std::string readString(const uint8_t*& p, const uint8_t* end) {
    if (p + 4 > end) return std::string();
    uint32_t len = readU32(p); p += 4;
    if (p + len > end) return std::string();
    std::string s(reinterpret_cast<const char*>(p), len);
    p += len;
    return s;
}

std::vector<uint8_t> KnowledgeEntry::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, id);
    writeString(out, question);
    writeString(out, answer);
    writeString(out, source);
    writeU64(out, timestamp);
    out.insert(out.end(), author.begin(), author.end());
    out.insert(out.end(), signature.begin(), signature.end());
    uint64_t scoreBits;
    std::memcpy(&scoreBits, &score, sizeof(double));
    writeU64(out, scoreBits);
    writeU32(out, validations);
    writeU32(out, tags.size());
    for (const auto& tag : tags) writeString(out, tag);
    return out;
}

KnowledgeEntry KnowledgeEntry::deserialize(const std::vector<uint8_t>& data) {
    KnowledgeEntry e;
    if (data.size() < 50) return e;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    e.id = readU64(p); p += 8;
    e.question = readString(p, end);
    e.answer = readString(p, end);
    e.source = readString(p, end);
    e.timestamp = readU64(p); p += 8;
    std::memcpy(e.author.data(), p, 33); p += 33;
    std::memcpy(e.signature.data(), p, 64); p += 64;
    uint64_t scoreBits = readU64(p); p += 8;
    std::memcpy(&e.score, &scoreBits, sizeof(double));
    e.validations = readU32(p); p += 4;
    uint32_t tagCount = readU32(p); p += 4;
    for (uint32_t i = 0; i < tagCount; i++) {
        e.tags.push_back(readString(p, end));
    }
    e.hash = e.computeHash();
    return e;
}

crypto::Hash256 KnowledgeEntry::computeHash() const {
    std::vector<uint8_t> buf;
    writeU64(buf, id);
    buf.insert(buf.end(), question.begin(), question.end());
    buf.insert(buf.end(), answer.begin(), answer.end());
    buf.insert(buf.end(), source.begin(), source.end());
    writeU64(buf, timestamp);
    buf.insert(buf.end(), author.begin(), author.end());
    return crypto::doubleSha256(buf.data(), buf.size());
}

bool KnowledgeEntry::verify() const {
    crypto::Hash256 computed = computeHash();
    return crypto::verify(computed, signature, author);
}

struct KnowledgeNetwork::Impl {
    explicit Impl(const KnowledgeConfig& config)
        : minScore(config.minScore),
          maxEntries(config.maxEntries),
          minValidationsRequired(resolveMinValidationsRequired(config.minValidationsRequired)) {}

    database::Database db;
    uint64_t entryCounter = 0;
    mutable std::mutex mtx;
    std::function<void(const KnowledgeEntry&)> newEntryCallback;
    std::function<void(uint64_t, double)> validationCallback;
    std::unordered_map<uint64_t, KnowledgeEntry> entries;
    double minScore = 0.0;
    size_t maxEntries = 1000000;
    uint32_t minValidationsRequired;
};

KnowledgeNetwork::KnowledgeNetwork() : KnowledgeNetwork(loadKnowledgeConfig()) {}
KnowledgeNetwork::KnowledgeNetwork(const KnowledgeConfig& config) : impl_(std::make_unique<Impl>(config)) {}
KnowledgeNetwork::~KnowledgeNetwork() { close(); }

bool KnowledgeNetwork::open(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.open(dbPath)) return false;
    auto counterData = impl_->db.get("meta:counter");
    if (!counterData.empty()) {
        impl_->entryCounter = readU64(counterData.data());
    }
    return true;
}

void KnowledgeNetwork::close() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->db.close();
}

uint64_t KnowledgeNetwork::submit(const KnowledgeEntry& entry, const crypto::PrivateKey& authorKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    KnowledgeEntry e = entry;
    e.id = ++impl_->entryCounter;
    e.timestamp = std::time(nullptr);
    e.author = crypto::derivePublicKey(authorKey);
    e.score = std::clamp(entry.score, 0.0, 1.0);
    e.validations = 0;
    e.hash = e.computeHash();
    e.signature = crypto::sign(e.hash, authorKey);
    std::string key = "knowledge:" + std::to_string(e.id);
    impl_->db.put(key, e.serialize());
    impl_->entries[e.id] = e;
    std::vector<uint8_t> counterBuf;
    writeU64(counterBuf, impl_->entryCounter);
    impl_->db.put("meta:counter", counterBuf);
    // compute simhash (take first 8 bytes of sha256 of question+answer)
    std::vector<uint8_t> sbuf;
    sbuf.insert(sbuf.end(), e.question.begin(), e.question.end());
    sbuf.insert(sbuf.end(), e.answer.begin(), e.answer.end());
    auto sh = crypto::sha256(sbuf);
    uint64_t sim = 0;
    for (int i = 0; i < 8; ++i) sim |= static_cast<uint64_t>(sh[i]) << (8 * i);
    std::vector<uint8_t> simBuf;
    writeU64(simBuf, sim);
    impl_->db.put(std::string("knowledge:simhash:") + std::to_string(e.id), simBuf);
    // create simbuckets (4-bit bands, up to 16 bands)
    for (uint32_t band = 0; band < 16; ++band) {
        uint8_t b = static_cast<uint8_t>((sim >> (band * 4)) & 0x0F);
        std::string bk = std::string("knowledge:simbucket:") + std::to_string(band) + ":" + std::to_string(b) + ":" + std::to_string(e.id);
        impl_->db.put(bk, std::vector<uint8_t>{});
    }
    // secondary indexes: author, tags, time, pending
    std::string authorHex = crypto::toHex(e.author);
    impl_->db.put(std::string("knowledge:author:") + authorHex + ":" + std::to_string(e.id), std::vector<uint8_t>{});
    for (const auto& t : e.tags) {
        impl_->db.put(std::string("knowledge:tag:") + t + ":" + std::to_string(e.id), std::vector<uint8_t>{});
    }
    impl_->db.put(std::string("knowledge:time:") + std::to_string(e.timestamp) + ":" + std::to_string(e.id), std::vector<uint8_t>{});
    if (e.validations < impl_->minValidationsRequired) {
        impl_->db.put(std::string("knowledge:pending:") + std::to_string(e.id), std::vector<uint8_t>{});
    }
    if (impl_->newEntryCallback) impl_->newEntryCallback(e);
    return e.id;
}

static uint32_t hammingDistance64(uint64_t a, uint64_t b) {
    return static_cast<uint32_t>(__builtin_popcountll(a ^ b));
}


bool KnowledgeNetwork::importEntry(const KnowledgeEntry& entry) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (entry.id == 0) return false;
    if (impl_->entries.size() >= impl_->maxEntries) return false;
    
    KnowledgeEntry e = entry;
    if (e.hash == crypto::Hash256{}) {
        e.hash = e.computeHash();
    }
    if (!e.verify()) return false;
    // Inline duplicate check (impl_->mtx already held) to avoid re-locking inside isDuplicate()
    {
        // compute simhash same as on insert
        std::vector<uint8_t> sbuf;
        sbuf.insert(sbuf.end(), e.question.begin(), e.question.end());
        sbuf.insert(sbuf.end(), e.answer.begin(), e.answer.end());
        auto sh = crypto::sha256(sbuf);
        uint64_t sim = 0;
        for (int i = 0; i < 8; ++i) sim |= static_cast<uint64_t>(sh[i]) << (8 * i);

        const uint32_t maxH = 8; // allowed Hamming distance
        bool duplicateFound = false;
        for (uint32_t band = 0; band < 16 && !duplicateFound; ++band) {
            uint8_t b = static_cast<uint8_t>((sim >> (band * 4)) & 0x0F);
            std::string prefix = std::string("knowledge:simbucket:") + std::to_string(band) + ":" + std::to_string(b) + ":";
            auto keys = impl_->db.keys(prefix);
            for (const auto& k : keys) {
                auto pos = k.find_last_of(':');
                if (pos == std::string::npos) continue;
                uint64_t id = std::stoull(k.substr(pos + 1));
                auto otherBuf = impl_->db.get(std::string("knowledge:simhash:") + std::to_string(id));
                if (otherBuf.size() < 8) continue;
                uint64_t other = readU64(otherBuf.data());
                if (hammingDistance64(sim, other) <= maxH) {
                    // load entry directly from DB (avoid calling get() which locks)
                    auto data = impl_->db.get(std::string("knowledge:") + std::to_string(id));
                    if (data.empty()) continue; // missing entry - skip candidate
                    auto existing = KnowledgeEntry::deserialize(data);
                    if (existing.question == e.question && existing.answer == e.answer) {
                        duplicateFound = true;
                        break;
                    }
                }
            }
        }
        if (duplicateFound) return false;
    }
    if (e.score < impl_->minScore) return false;
    
    impl_->entries[e.id] = e;
    if (e.id > impl_->entryCounter) {
        impl_->entryCounter = e.id;
        std::vector<uint8_t> counterBuf;
        writeU64(counterBuf, impl_->entryCounter);
        impl_->db.put("meta:counter", counterBuf);
    }
    
    std::string key = "knowledge:" + std::to_string(e.id);
    impl_->db.put(key, e.serialize());
    // compute and store simhash and indexes same as submit
    std::vector<uint8_t> sbuf;
    sbuf.insert(sbuf.end(), e.question.begin(), e.question.end());
    sbuf.insert(sbuf.end(), e.answer.begin(), e.answer.end());
    auto sh = crypto::sha256(sbuf);
    uint64_t sim = 0;
    for (int i = 0; i < 8; ++i) sim |= static_cast<uint64_t>(sh[i]) << (8 * i);
    std::vector<uint8_t> simBuf;
    writeU64(simBuf, sim);
    impl_->db.put(std::string("knowledge:simhash:") + std::to_string(e.id), simBuf);
    for (uint32_t band = 0; band < 16; ++band) {
        uint8_t b = static_cast<uint8_t>((sim >> (band * 4)) & 0x0F);
        std::string bk = std::string("knowledge:simbucket:") + std::to_string(band) + ":" + std::to_string(b) + ":" + std::to_string(e.id);
        impl_->db.put(bk, std::vector<uint8_t>{});
    }
    std::string authorHex = crypto::toHex(e.author);
    impl_->db.put(std::string("knowledge:author:") + authorHex + ":" + std::to_string(e.id), std::vector<uint8_t>{});
    for (const auto& t : e.tags) {
        impl_->db.put(std::string("knowledge:tag:") + t + ":" + std::to_string(e.id), std::vector<uint8_t>{});
    }
    impl_->db.put(std::string("knowledge:time:") + std::to_string(e.timestamp) + ":" + std::to_string(e.id), std::vector<uint8_t>{});
    if (e.validations < impl_->minValidationsRequired) {
        impl_->db.put(std::string("knowledge:pending:") + std::to_string(e.id), std::vector<uint8_t>{});
    }
    
    if (impl_->newEntryCallback) impl_->newEntryCallback(e);
    
    return true;
}

bool KnowledgeNetwork::vote(const ValidationVote& vote, const crypto::PrivateKey& validatorKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->entries.find(vote.knowledgeId);
    if (it == impl_->entries.end()) return false;
    if (vote.approve) {
        it->second.validations++;
        it->second.score = (it->second.score * (it->second.validations - 1) + vote.scoreGiven) / it->second.validations;
    }
    impl_->db.put("knowledge:" + std::to_string(it->second.id), it->second.serialize());
    // update pending index
    if (it->second.validations < impl_->minValidationsRequired) {
        impl_->db.put(std::string("knowledge:pending:") + std::to_string(it->second.id), std::vector<uint8_t>{});
    } else {
        impl_->db.del(std::string("knowledge:pending:") + std::to_string(it->second.id));
    }
    if (impl_->validationCallback) impl_->validationCallback(it->second.id, it->second.score);
    return true;
}

KnowledgeEntry KnowledgeNetwork::get(uint64_t id) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->entries.find(id);
    if (it != impl_->entries.end()) return it->second;
    auto data = impl_->db.get("knowledge:" + std::to_string(id));
    if (data.empty()) return KnowledgeEntry{};
    return KnowledgeEntry::deserialize(data);
}

std::vector<KnowledgeEntry> KnowledgeNetwork::search(const std::string& query, size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<KnowledgeEntry> results;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    for (const auto& [id, e] : impl_->entries) {
        if (results.size() >= limit) break;
        std::string lowerQ = e.question;
        std::string lowerA = e.answer;
        std::transform(lowerQ.begin(), lowerQ.end(), lowerQ.begin(), ::tolower);
        std::transform(lowerA.begin(), lowerA.end(), lowerA.begin(), ::tolower);
        if (lowerQ.find(lowerQuery) != std::string::npos || lowerA.find(lowerQuery) != std::string::npos) {
            results.push_back(e);
        }
    }
    return results;
}

std::vector<KnowledgeEntry> KnowledgeNetwork::getByAuthor(const crypto::PublicKey& author, size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<KnowledgeEntry> results;
    std::string prefix = std::string("knowledge:author:") + crypto::toHex(author) + ":";
    auto keys = impl_->db.keys(prefix);
    std::sort(keys.begin(), keys.end());
    for (const auto& k : keys) {
        if (results.size() >= limit) break;
        auto pos = k.find_last_of(':');
        if (pos == std::string::npos) continue;
        std::string idStr = k.substr(pos + 1);
        uint64_t id = std::stoull(idStr);
        results.push_back(get(id));
    }
    return results;
}

std::vector<KnowledgeEntry> KnowledgeNetwork::getByTag(const std::string& tag, size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<KnowledgeEntry> results;
    std::string prefix = std::string("knowledge:tag:") + tag + ":";
    auto keys = impl_->db.keys(prefix);
    std::sort(keys.begin(), keys.end());
    for (const auto& k : keys) {
        if (results.size() >= limit) break;
        auto pos = k.find_last_of(':');
        if (pos == std::string::npos) continue;
        uint64_t id = std::stoull(k.substr(pos + 1));
        results.push_back(get(id));
    }
    return results;
}

std::vector<KnowledgeEntry> KnowledgeNetwork::getRecent(size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::pair<uint64_t, uint64_t>> items; // timestamp, id
    auto keys = impl_->db.keys("knowledge:time:");
    for (const auto& k : keys) {
        // key format knowledge:time:{timestamp}:{id}
        size_t p2 = k.find_last_of(':');
        if (p2 == std::string::npos) continue;
        size_t start = std::string("knowledge:time:").size();
        if (p2 <= start) continue;
        uint64_t ts = std::stoull(k.substr(start, p2 - start));
        uint64_t id = std::stoull(k.substr(p2 + 1));
        items.emplace_back(ts, id);
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b){ return a.first > b.first; });
    std::vector<KnowledgeEntry> results;
    for (size_t i = 0; i < items.size() && results.size() < limit; ++i) {
        results.push_back(get(items[i].second));
    }
    return results;
}

std::vector<KnowledgeEntry> KnowledgeNetwork::getPendingValidation(size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<KnowledgeEntry> results;
    auto keys = impl_->db.keys("knowledge:pending:");
    std::sort(keys.begin(), keys.end());
    for (const auto& k : keys) {
        if (results.size() >= limit) break;
        auto pos = k.find_last_of(':');
        if (pos == std::string::npos) continue;
        uint64_t id = std::stoull(k.substr(pos + 1));
        results.push_back(get(id));
    }
    return results;
}

uint64_t KnowledgeNetwork::totalEntries() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->entries.size();
}

double KnowledgeNetwork::getScore(uint64_t id) const {
    return get(id).score;
}

uint32_t KnowledgeNetwork::getValidationCount(uint64_t id) const {
    return get(id).validations;
}

double KnowledgeNetwork::calculateReward(const KnowledgeEntry& entry) const {
    double baseReward = 1.0;
    double scoreMultiplier = 1.0 + (entry.score * 4.0);
    double validationBonus = 1.0 + std::log2(1.0 + entry.validations);
    return baseReward * scoreMultiplier * validationBonus;
}

bool KnowledgeNetwork::isDuplicate(const KnowledgeEntry& entry) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    // compute simhash same as on insert
    std::vector<uint8_t> sbuf;
    sbuf.insert(sbuf.end(), entry.question.begin(), entry.question.end());
    sbuf.insert(sbuf.end(), entry.answer.begin(), entry.answer.end());
    auto sh = crypto::sha256(sbuf);
    uint64_t sim = 0;
    for (int i = 0; i < 8; ++i) sim |= static_cast<uint64_t>(sh[i]) << (8 * i);

    const uint32_t maxH = 8; // allowed Hamming distance
    for (uint32_t band = 0; band < 16; ++band) {
        uint8_t b = static_cast<uint8_t>((sim >> (band * 4)) & 0x0F);
        std::string prefix = std::string("knowledge:simbucket:") + std::to_string(band) + ":" + std::to_string(b) + ":";
        auto keys = impl_->db.keys(prefix);
        for (const auto& k : keys) {
            auto pos = k.find_last_of(':');
            if (pos == std::string::npos) continue;
            uint64_t id = std::stoull(k.substr(pos + 1));
            auto otherBuf = impl_->db.get(std::string("knowledge:simhash:") + std::to_string(id));
            if (otherBuf.size() < 8) continue;
            uint64_t other = readU64(otherBuf.data());
            if (hammingDistance64(sim, other) <= maxH) {
                // load entry directly from DB (avoid calling get() which locks)
                auto data = impl_->db.get(std::string("knowledge:") + std::to_string(id));
                if (data.empty()) continue; // missing entry - skip candidate
                auto e = KnowledgeEntry::deserialize(data);
                // confirm actual duplicate by exact content match (question+answer)
                if (e.question == entry.question && e.answer == entry.answer) return true;
                // not an exact match - continue searching other candidates
                continue;
            }
        }
    }
    return false;
}

void KnowledgeNetwork::onNewEntry(std::function<void(const KnowledgeEntry&)> callback) {
    impl_->newEntryCallback = callback;
}

void KnowledgeNetwork::onValidation(std::function<void(uint64_t, double)> callback) {
    impl_->validationCallback = callback;
}

KnowledgeStats KnowledgeNetwork::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    KnowledgeStats stats{};
    stats.totalEntries = impl_->entries.size();
    stats.validatedEntries = 0;
    stats.totalValidations = 0;
    stats.avgScore = 0;
    for (const auto& [id, entry] : impl_->entries) {
        if (entry.validations > 0) stats.validatedEntries++;
        stats.totalValidations += entry.validations;
        stats.avgScore += entry.score;
    }
    if (stats.totalEntries > 0) stats.avgScore /= stats.totalEntries;
    return stats;
}

std::vector<KnowledgeEntry> KnowledgeNetwork::getRecentEntries(size_t count) const {
    return getRecent(count);
}

std::vector<KnowledgeEntry> KnowledgeNetwork::getTopRated(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::pair<double, KnowledgeEntry>> sorted;
    for (const auto& [id, entry] : impl_->entries) {
        sorted.emplace_back(entry.score, entry);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<KnowledgeEntry> result;
    for (size_t i = 0; i < count && i < sorted.size(); i++) {
        result.push_back(sorted[i].second);
    }
    return result;
}

std::vector<std::string> KnowledgeNetwork::getCategories() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::set<std::string> categories;
    for (const auto& [id, entry] : impl_->entries) {
        for (const auto& tag : entry.tags) {
            categories.insert(tag);
        }
    }
    return std::vector<std::string>(categories.begin(), categories.end());
}

size_t KnowledgeNetwork::getCategoryCount(const std::string& category) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    size_t count = 0;
    for (const auto& [id, entry] : impl_->entries) {
        for (const auto& tag : entry.tags) {
            if (tag == category) { count++; break; }
        }
    }
    return count;
}

bool KnowledgeNetwork::exportToJson(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "[\n";
    bool first = true;
    for (const auto& [id, entry] : impl_->entries) {
        if (!first) f << ",\n";
        f << "  {\"id\": " << entry.id << ", \"question\": \"" << entry.question << "\", \"score\": " << entry.score << "}";
        first = false;
    }
    f << "\n]";
    return true;
}

void KnowledgeNetwork::setMinScore(double score) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->minScore = score;
}

void KnowledgeNetwork::setMaxEntries(size_t max) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxEntries = max;
}

}
}
