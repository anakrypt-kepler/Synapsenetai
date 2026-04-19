#include "core/agent_coordination.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace synapse::core {

namespace {
void writeU64LE(std::vector<uint8_t>& out, uint64_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
}

void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void writeBytes(std::vector<uint8_t>& out, const std::string& s) {
    writeU32LE(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

}

AgentIdentity AgentIdentity::generate() {
    auto kp = crypto::generateKeyPair();
    return {kp.publicKey, kp.privateKey};
}

AgentIdentity AgentIdentity::fromSeed(const crypto::Hash256& seed) {
    auto kp = crypto::keyPairFromSeed(seed);
    return {kp.publicKey, kp.privateKey};
}

std::vector<uint8_t> RoomMessage::canonicalBytes() const {
    std::vector<uint8_t> out;
    out.reserve(128 + roomId.size() + payload.size());
    
    writeU64LE(out, sequence);
    writeU64LE(out, timestamp);
    out.push_back(static_cast<uint8_t>(type));
    out.insert(out.end(), author.begin(), author.end());
    writeBytes(out, roomId);
    writeBytes(out, payload);
    
    return out;
}

crypto::Hash256 RoomMessage::computeHash() const {
    auto bytes = canonicalBytes();
    return crypto::sha256(bytes.data(), bytes.size());
}

AgentRoom::AgentRoom(const std::string& roomId, RoomType type, const RoomConfig& cfg)
    : roomId_(roomId), type_(type), cfg_(cfg) {}

bool AgentRoom::post(const AgentIdentity& author, RoomMessageType type, 
                     const std::string& payload, uint64_t timestamp) {
    if (!author.valid()) return false;
    if (payload.size() > cfg_.maxPayloadBytes) return false;
    
    RoomMessage msg;
    msg.sequence = nextSequence(author.id);
    msg.timestamp = timestamp;
    msg.type = type;
    msg.author = author.id;
    msg.roomId = roomId_;
    msg.payload = payload;
    
    auto canonical = msg.canonicalBytes();
    auto hash = crypto::sha256(canonical.data(), canonical.size());
    msg.signature = crypto::sign(hash, author.privateKey);
    
    return addVerifiedMessage(msg);
}

bool AgentRoom::validateMessage(const RoomMessage& msg) const {
    if (msg.roomId != roomId_) return false;
    if (msg.payload.size() > cfg_.maxPayloadBytes) return false;
    if (msg.sequence == 0 && msg.timestamp == 0) return false;
    
    auto canonical = msg.canonicalBytes();
    auto hash = crypto::sha256(canonical.data(), canonical.size());
    
    if (!crypto::verify(hash, msg.signature, msg.author)) {
        return false;
    }
    
    return true;
}

bool AgentRoom::checkAntiReplay(const RoomMessage& msg) const {
    auto hash = msg.computeHash();
    if (seenHashes_.count(hash) > 0) return false;
    
    auto it = authorSequences_.find(msg.author);
    if (it != authorSequences_.end()) {
        if (msg.sequence <= it->second) return false;
    }
    
    return true;
}

bool AgentRoom::addVerifiedMessage(const RoomMessage& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    if (!checkAntiReplay(msg)) return false;
    
    if (!validateMessage(msg)) return false;
    
    SignedArtifact artifact;
    artifact.hash = msg.computeHash();
    artifact.message = msg;
    artifact.insertedAt = msg.timestamp;
    
    artifacts_.push_back(artifact);
    hashIndex_[artifact.hash] = artifacts_.size() - 1;
    seenHashes_.insert(artifact.hash);
    authorSequences_[msg.author] = msg.sequence;
    
    if (cfg_.maxMessages > 0) {
        while (artifacts_.size() > cfg_.maxMessages) {
            auto oldest = artifacts_.front();
            hashIndex_.erase(oldest.hash);
            seenHashes_.erase(oldest.hash);
            artifacts_.erase(artifacts_.begin());
            
            for (auto& [k, v] : hashIndex_) {
                if (v > 0) v--;
            }
        }
    }
    
    return true;
}

std::vector<SignedArtifact> AgentRoom::getArtifacts(uint64_t sinceTimestamp, uint32_t limit) const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    std::vector<SignedArtifact> result;
    for (auto it = artifacts_.rbegin(); it != artifacts_.rend() && result.size() < limit; ++it) {
        if (it->insertedAt >= sinceTimestamp || sinceTimestamp == 0) {
            result.push_back(*it);
        }
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::optional<SignedArtifact> AgentRoom::getArtifact(const crypto::Hash256& hash) const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = hashIndex_.find(hash);
    if (it == hashIndex_.end()) return std::nullopt;
    return artifacts_[it->second];
}

uint64_t AgentRoom::nextSequence(const AgentId& author) const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = authorSequences_.find(author);
    if (it == authorSequences_.end()) return 1;
    return it->second + 1;
}

void AgentRoom::prune(uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    if (cfg_.retentionSeconds == 0) return;
    
    uint64_t cutoff = nowTimestamp > cfg_.retentionSeconds ? nowTimestamp - cfg_.retentionSeconds : 0;
    
    auto it = std::remove_if(artifacts_.begin(), artifacts_.end(), [cutoff](const SignedArtifact& a) {
        return a.insertedAt < cutoff;
    });
    
    if (it != artifacts_.end()) {
        for (auto rm = it; rm != artifacts_.end(); ++rm) {
            hashIndex_.erase(rm->hash);
            seenHashes_.erase(rm->hash);
        }
        artifacts_.erase(it, artifacts_.end());
        
        hashIndex_.clear();
        for (size_t i = 0; i < artifacts_.size(); ++i) {
            hashIndex_[artifacts_[i].hash] = i;
        }
    }
}

uint64_t AgentRoom::messageCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return artifacts_.size();
}

CoordinationHub::CoordinationHub(const CoordinationConfig& cfg) : cfg_(cfg) {}

CoordinationConfig CoordinationHub::config() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return cfg_;
}

void CoordinationHub::setConfig(const CoordinationConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);
    cfg_ = cfg;
}

bool CoordinationHub::createRoom(const std::string& roomId, RoomType type, const RoomConfig& roomCfg) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    if (rooms_.count(roomId) > 0) return false;
    
    uint32_t count = 0;
    for (const auto& [id, room] : rooms_) {
        if (room->roomType() == type) count++;
    }
    
    if (count >= cfg_.maxRoomsPerType) return false;
    
    rooms_[roomId] = std::make_unique<AgentRoom>(roomId, type, roomCfg);
    return true;
}

bool CoordinationHub::hasRoom(const std::string& roomId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    return rooms_.count(roomId) > 0;
}

AgentRoom* CoordinationHub::getRoom(const std::string& roomId) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = rooms_.find(roomId);
    return it != rooms_.end() ? it->second.get() : nullptr;
}

const AgentRoom* CoordinationHub::getRoom(const std::string& roomId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = rooms_.find(roomId);
    return it != rooms_.end() ? it->second.get() : nullptr;
}

bool CoordinationHub::postToRoom(const std::string& roomId, const AgentIdentity& author,
                                  RoomMessageType type, const std::string& payload, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    
    if (!it->second->post(author, type, payload, timestamp)) return false;
    
    auto artifacts = it->second->getArtifacts(timestamp, 1);
    if (!artifacts.empty()) {
        addToObservatory(artifacts.back());
    }
    
    return true;
}

std::vector<std::string> CoordinationHub::listRooms(RoomType type) const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    std::vector<std::string> result;
    for (const auto& [id, room] : rooms_) {
        if (room->roomType() == type) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<std::string> CoordinationHub::listAllRooms() const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    std::vector<std::string> result;
    for (const auto& [id, room] : rooms_) {
        result.push_back(id);
    }
    return result;
}

void CoordinationHub::addToObservatory(const SignedArtifact& artifact) {
    ObservatoryEntry entry;
    entry.hash = artifact.hash;
    entry.roomId = artifact.message.roomId;
    entry.type = artifact.message.type;
    entry.author = artifact.message.author;
    entry.timestamp = artifact.message.timestamp;
    
    size_t previewLen = std::min(artifact.message.payload.size(), size_t(200));
    entry.payloadPreview = artifact.message.payload.substr(0, previewLen);
    
    observatoryIndex_.push_back(entry);
    artifactIndex_[artifact.hash] = observatoryIndex_.size() - 1;
    
    if (cfg_.maxArtifactHistory > 0) {
        while (observatoryIndex_.size() > cfg_.maxArtifactHistory) {
            artifactIndex_.erase(observatoryIndex_.front().hash);
            observatoryIndex_.erase(observatoryIndex_.begin());
            
            for (auto& [k, v] : artifactIndex_) {
                if (v > 0) v--;
            }
        }
    }
}

std::vector<ObservatoryEntry> CoordinationHub::getObservatoryFeed(uint64_t sinceTimestamp, uint32_t limit) const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    std::vector<ObservatoryEntry> result;
    for (auto it = observatoryIndex_.rbegin(); it != observatoryIndex_.rend() && result.size() < limit; ++it) {
        if (it->timestamp >= sinceTimestamp || sinceTimestamp == 0) {
            result.push_back(*it);
        }
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::optional<SignedArtifact> CoordinationHub::getArtifact(const crypto::Hash256& hash) const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    for (const auto& [id, room] : rooms_) {
        auto artifact = room->getArtifact(hash);
        if (artifact) return artifact;
    }
    return std::nullopt;
}

void CoordinationHub::pruneAll(uint64_t nowTimestamp) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    for (auto& [id, room] : rooms_) {
        room->prune(nowTimestamp);
    }
    
    if (cfg_.pruneIntervalSeconds > 0) {
        uint64_t cutoff = nowTimestamp > cfg_.pruneIntervalSeconds * 10 ? 
                          nowTimestamp - cfg_.pruneIntervalSeconds * 10 : 0;
        
        auto it = std::remove_if(observatoryIndex_.begin(), observatoryIndex_.end(),
            [cutoff](const ObservatoryEntry& e) { return e.timestamp < cutoff; });
        
        if (it != observatoryIndex_.end()) {
            for (auto rm = it; rm != observatoryIndex_.end(); ++rm) {
                artifactIndex_.erase(rm->hash);
            }
            observatoryIndex_.erase(it, observatoryIndex_.end());
            
            artifactIndex_.clear();
            for (size_t i = 0; i < observatoryIndex_.size(); ++i) {
                artifactIndex_[observatoryIndex_[i].hash] = i;
            }
        }
    }
}

void CoordinationHub::periodicMaintenance(uint64_t nowTimestamp) {
    if (cfg_.pruneIntervalSeconds > 0 && 
        (nowTimestamp - lastPrune_ >= cfg_.pruneIntervalSeconds || lastPrune_ == 0)) {
        pruneAll(nowTimestamp);
        lastPrune_ = nowTimestamp;
    }
}

bool CoordinationHub::verifyAndRepairIndexes(CoordinationRecoveryStats* stats,
                                             std::string* reason) {
    std::lock_guard<std::mutex> lock(mtx_);

    CoordinationRecoveryStats out;
    out.roomCount = rooms_.size();
    out.observatoryEntries = observatoryIndex_.size();

    std::vector<ObservatoryEntry> rebuilt;
    for (const auto& [roomId, room] : rooms_) {
        auto artifacts = room->getArtifacts(0, std::numeric_limits<uint32_t>::max());
        out.roomArtifacts += artifacts.size();
        for (const auto& artifact : artifacts) {
            ObservatoryEntry entry;
            entry.hash = artifact.hash;
            entry.roomId = artifact.message.roomId;
            entry.type = artifact.message.type;
            entry.author = artifact.message.author;
            entry.timestamp = artifact.message.timestamp;

            size_t previewLen = std::min(artifact.message.payload.size(), size_t(200));
            entry.payloadPreview = artifact.message.payload.substr(0, previewLen);
            rebuilt.push_back(std::move(entry));
        }
    }

    std::sort(rebuilt.begin(), rebuilt.end(), [](const ObservatoryEntry& a, const ObservatoryEntry& b) {
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
        if (a.hash != b.hash) return a.hash < b.hash;
        if (a.roomId != b.roomId) return a.roomId < b.roomId;
        if (a.type != b.type) return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
        if (a.author != b.author) return a.author < b.author;
        return a.payloadPreview < b.payloadPreview;
    });

    if (cfg_.maxArtifactHistory > 0 && rebuilt.size() > cfg_.maxArtifactHistory) {
        rebuilt.erase(rebuilt.begin(), rebuilt.end() - static_cast<std::ptrdiff_t>(cfg_.maxArtifactHistory));
    }

    std::vector<ObservatoryEntry> deduped;
    deduped.reserve(rebuilt.size());
    std::map<crypto::Hash256, size_t> rebuiltIndex;
    for (const auto& entry : rebuilt) {
        if (rebuiltIndex.count(entry.hash) > 0) continue;
        rebuiltIndex[entry.hash] = deduped.size();
        deduped.push_back(entry);
    }

    const auto sameEntries = [&]() {
        if (observatoryIndex_.size() != deduped.size()) return false;
        for (size_t i = 0; i < deduped.size(); ++i) {
            const auto& a = observatoryIndex_[i];
            const auto& b = deduped[i];
            if (a.hash != b.hash) return false;
            if (a.roomId != b.roomId) return false;
            if (a.type != b.type) return false;
            if (a.author != b.author) return false;
            if (a.payloadPreview != b.payloadPreview) return false;
            if (a.timestamp != b.timestamp) return false;
        }
        return true;
    };

    const bool entriesChanged = !sameEntries();
    const bool indexChanged = artifactIndex_ != rebuiltIndex;

    if (entriesChanged || indexChanged) {
        observatoryIndex_ = std::move(deduped);
        artifactIndex_ = std::move(rebuiltIndex);
        out.rebuiltEntries = observatoryIndex_.size();
        out.rebuiltIndexEntries = artifactIndex_.size();
    }

    out.observatoryEntries = observatoryIndex_.size();
    if (stats) *stats = out;
    if (reason) *reason = (entriesChanged || indexChanged) ? "repaired" : "ok";
    return true;
}

uint64_t CoordinationHub::totalMessages() const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    uint64_t total = 0;
    for (const auto& [id, room] : rooms_) {
        total += room->messageCount();
    }
    return total;
}

ObservatoryFeed::ObservatoryFeed(const CoordinationHub& hub) : hub_(hub) {}

std::vector<ObservatoryEntry> ObservatoryFeed::getFeed(uint64_t sinceTimestamp, uint32_t limit) const {
    return hub_.getObservatoryFeed(sinceTimestamp, limit);
}

std::optional<SignedArtifact> ObservatoryFeed::getArtifact(const crypto::Hash256& hash) const {
    return hub_.getArtifact(hash);
}

std::optional<RoomMessage> ObservatoryFeed::getMessage(const crypto::Hash256& hash) const {
    auto artifact = getArtifact(hash);
    if (artifact) return artifact->message;
    return std::nullopt;
}

std::string roomMessageTypeToString(RoomMessageType type) {
    switch (type) {
        case RoomMessageType::TASK: return "task";
        case RoomMessageType::REVIEW: return "review";
        case RoomMessageType::DISPUTE: return "dispute";
        case RoomMessageType::ALERT: return "alert";
        default: return "unknown";
    }
}

RoomMessageType stringToRoomMessageType(const std::string& s) {
    if (s == "task") return RoomMessageType::TASK;
    if (s == "review") return RoomMessageType::REVIEW;
    if (s == "dispute") return RoomMessageType::DISPUTE;
    if (s == "alert") return RoomMessageType::ALERT;
    return RoomMessageType::TASK;
}

std::string roomTypeToString(RoomType type) {
    switch (type) {
        case RoomType::TASKS: return "tasks";
        case RoomType::REVIEWS: return "reviews";
        case RoomType::DISPUTES: return "disputes";
        case RoomType::ALERTS: return "alerts";
        default: return "unknown";
    }
}

RoomType stringToRoomType(const std::string& s) {
    if (s == "tasks") return RoomType::TASKS;
    if (s == "reviews") return RoomType::REVIEWS;
    if (s == "disputes") return RoomType::DISPUTES;
    if (s == "alerts") return RoomType::ALERTS;
    return RoomType::TASKS;
}

}
