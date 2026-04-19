#include "core/ledger.h"
#include "database/database.h"
#include <mutex>
#include <cstring>
#include <ctime>
#include <fstream>
#include <algorithm>
#include <queue>
#include <set>
#include <map>
#include <unordered_set>
#include <functional>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <limits>

namespace synapse {
namespace core {

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

static void writeU16(std::vector<uint8_t>& out, uint16_t val) {
    out.push_back(val & 0xff);
    out.push_back((val >> 8) & 0xff);
}

static uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

constexpr uint32_t MAX_EVENTS_PER_BLOCK = 1000;
constexpr size_t MAX_EVENT_SIZE = 1 << 20; // 1MB

static uint64_t maxEventIdFromBlocks(const std::vector<Block>& blocks) {
    uint64_t maxId = 0;
    for (const auto& block : blocks) {
        for (const auto& event : block.events) {
            if (event.id > maxId) maxId = event.id;
        }
    }
    return maxId;
}

static uint64_t saturatingAddWork(uint64_t work, uint32_t difficulty) {
    if (difficulty >= 64) {
        return std::numeric_limits<uint64_t>::max();
    }

    const uint64_t increment = 1ULL << difficulty;
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    if (work > max - increment) {
        return max;
    }
    return work + increment;
}

static uint64_t chainWorkFromBlocks(const std::vector<Block>& blocks) {
    uint64_t work = 0;
    for (const auto& block : blocks) {
        work = saturatingAddWork(work, block.difficulty);
    }
    return work;
}

std::vector<uint8_t> Event::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, id);
    writeU64(out, timestamp);
    out.push_back(static_cast<uint8_t>(type));
    writeU32(out, data.size());
    out.insert(out.end(), data.begin(), data.end());
    out.insert(out.end(), prevHash.begin(), prevHash.end());
    out.insert(out.end(), author.begin(), author.end());
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

Event Event::deserialize(const std::vector<uint8_t>& buf) {
    Event e{};
    if (buf.size() < 8 + 8 + 1 + 4 + 32 + 33 + 64) return e;
    const uint8_t* p = buf.data();
    const uint8_t* end = buf.data() + buf.size();
    e.id = readU64(p); p += 8;
    e.timestamp = readU64(p); p += 8;
    e.type = static_cast<EventType>(*p++);
    if (static_cast<size_t>(end - p) < sizeof(uint32_t)) return e;
    uint32_t dataLen = readU32(p); p += 4;
    if (dataLen > MAX_EVENT_SIZE) return e;
    if (static_cast<size_t>(end - p) < static_cast<size_t>(dataLen) + 32 + 33 + 64) return e;
    e.data.assign(p, p + dataLen); p += dataLen;
    std::memcpy(e.prevHash.data(), p, 32); p += 32;
    std::memcpy(e.author.data(), p, 33); p += 33;
    std::memcpy(e.signature.data(), p, 64);
    e.hash = e.computeHash();
    return e;
}

crypto::Hash256 Event::computeHash() const {
    std::vector<uint8_t> buf;
    writeU64(buf, id);
    writeU64(buf, timestamp);
    buf.push_back(static_cast<uint8_t>(type));
    buf.insert(buf.end(), data.begin(), data.end());
    buf.insert(buf.end(), prevHash.begin(), prevHash.end());
    buf.insert(buf.end(), author.begin(), author.end());
    return crypto::doubleSha256(buf.data(), buf.size());
}

bool Event::verify() const {
    crypto::Hash256 computed = computeHash();
    if (computed != hash) return false;
    if (type == EventType::GENESIS) return true;
    return crypto::verify(hash, signature, author);
}

std::string Event::toJson() const {
    std::stringstream ss;
    ss << "{";
    ss << "\"id\":" << id << ",";
    ss << "\"timestamp\":" << timestamp << ",";
    ss << "\"type\":" << static_cast<int>(type) << ",";
    ss << "\"dataSize\":" << data.size() << ",";
    ss << "\"hash\":\"";
    for (uint8_t b : hash) ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    ss << "\"}";
    return ss.str();
}

std::vector<uint8_t> Block::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, height);
    writeU64(out, timestamp);
    out.insert(out.end(), prevHash.begin(), prevHash.end());
    out.insert(out.end(), merkleRoot.begin(), merkleRoot.end());
    writeU32(out, nonce);
    writeU32(out, difficulty);
    writeU64(out, totalWork);
    writeU32(out, events.size());
    for (const auto& e : events) {
        auto eData = e.serialize();
        writeU32(out, eData.size());
        out.insert(out.end(), eData.begin(), eData.end());
    }
    return out;
}

Block Block::deserialize(const std::vector<uint8_t>& buf) {
    Block b{};
    if (buf.size() < 8 + 8 + 32 + 32 + 4 + 4 + 8 + 4) return b;
    const uint8_t* p = buf.data();
    const uint8_t* end = buf.data() + buf.size();
    b.height = readU64(p); p += 8;
    b.timestamp = readU64(p); p += 8;
    std::memcpy(b.prevHash.data(), p, 32); p += 32;
    std::memcpy(b.merkleRoot.data(), p, 32); p += 32;
    b.nonce = readU32(p); p += 4;
    b.difficulty = readU32(p); p += 4;
    b.totalWork = readU64(p); p += 8;
    if (static_cast<size_t>(end - p) < sizeof(uint32_t)) return b;
    uint32_t evtCount = readU32(p); p += 4;
    if (evtCount > MAX_EVENTS_PER_BLOCK) return b;
    const size_t maxBlockEventBytes = static_cast<size_t>(evtCount) * (sizeof(uint32_t) + MAX_EVENT_SIZE);
    if (static_cast<size_t>(end - p) > maxBlockEventBytes) {
        return b; // reject oversized
    }
    for (uint32_t i = 0; i < evtCount; i++) {
        if (static_cast<size_t>(end - p) < sizeof(uint32_t)) return b;
        uint32_t evtLen = readU32(p); p += sizeof(uint32_t);
        if (evtLen > MAX_EVENT_SIZE || static_cast<size_t>(end - p) < evtLen) return b;
        std::vector<uint8_t> evtData(p, p + evtLen); p += evtLen;
        b.events.push_back(Event::deserialize(evtData));
    }
    if (p != end) return b;
    b.hash = b.computeHash();
    return b;
}

crypto::Hash256 Block::computeHash() const {
    std::vector<uint8_t> buf;
    writeU64(buf, height);
    writeU64(buf, timestamp);
    buf.insert(buf.end(), prevHash.begin(), prevHash.end());
    buf.insert(buf.end(), merkleRoot.begin(), merkleRoot.end());
    writeU32(buf, nonce);
    writeU32(buf, difficulty);
    return crypto::doubleSha256(buf.data(), buf.size());
}

crypto::Hash256 Block::computeMerkleRoot() const {
    if (events.empty()) return crypto::Hash256{};
    std::vector<crypto::Hash256> hashes;
    for (const auto& e : events) hashes.push_back(e.hash);
    while (hashes.size() > 1) {
        std::vector<crypto::Hash256> next;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            std::vector<uint8_t> combined;
            combined.insert(combined.end(), hashes[i].begin(), hashes[i].end());
            size_t j = (i + 1 < hashes.size()) ? i + 1 : i;
            combined.insert(combined.end(), hashes[j].begin(), hashes[j].end());
            next.push_back(crypto::doubleSha256(combined.data(), combined.size()));
        }
        hashes = std::move(next);
    }
    return hashes[0];
}

std::string Block::toJson() const {
    std::stringstream ss;
    ss << "{";
    ss << "\"height\":" << height << ",";
    ss << "\"timestamp\":" << timestamp << ",";
    ss << "\"nonce\":" << nonce << ",";
    ss << "\"difficulty\":" << difficulty << ",";
    ss << "\"eventCount\":" << events.size() << ",";
    ss << "\"hash\":\"";
    for (uint8_t b : hash) ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    ss << "\"}";
    return ss.str();
}

bool Block::meetsTarget() const {
    uint32_t leadingZeros = 0;
    for (uint8_t b : hash) {
        if (b == 0) leadingZeros += 8;
        else {
            for (int i = 7; i >= 0; i--) {
                if ((b >> i) & 1) break;
                leadingZeros++;
            }
            break;
        }
    }
    return leadingZeros >= difficulty;
}

struct BlockIndex {
    uint64_t height;
    uint64_t timestamp;
    crypto::Hash256 hash;
    crypto::Hash256 prevHash;
    uint32_t eventCount;
    uint64_t fileOffset;
    uint32_t serializedSize;
};

struct EventIndex {
    uint64_t id;
    uint64_t blockHeight;
    uint32_t indexInBlock;
    EventType type;
    crypto::PublicKey author;
    crypto::Hash256 hash;
};

struct ChainFork {
    uint64_t forkHeight;
    crypto::Hash256 forkHash;
    std::vector<Block> alternateChain;
    uint64_t totalWork;
};

struct Ledger::Impl {
    database::Database db;
    std::vector<Event> pendingEvents;
    std::vector<Block> blocks;
    std::map<crypto::Hash256, uint64_t> blockHashIndex;
    std::map<uint64_t, EventIndex> eventIndex;
    std::map<crypto::PublicKey, std::vector<uint64_t>> eventsByAuthor;
    std::map<EventType, std::vector<uint64_t>> eventsByType;
    std::vector<ChainFork> forks;
    uint64_t currentHeight;
    uint64_t eventCounter;
    uint64_t totalWork;
    uint32_t currentDifficulty;
    mutable std::mutex mtx;
    mutable std::mutex eventMtx;
    std::condition_variable cv;
    std::atomic<bool> running;
    std::thread maintenanceThread;
    std::function<void(const Block&)> blockCallback;
    std::function<void(const Event&)> eventCallback;
    std::function<crypto::Signature(const crypto::Hash256&)> signer;
    std::function<void(uint64_t, uint64_t)> reorgCallback;
    
    size_t maxPendingEvents;
    size_t maxBlocksInMemory;
    uint64_t targetBlockTime;
    uint64_t difficultyAdjustmentInterval;
    bool autoCompact;
    
    void runMaintenance();
    void adjustDifficulty();
    void pruneOldBlocks();
    void rebuildIndexes();
    bool validateBlock(const Block& block);
    bool validateEvent(const Event& event);
    bool appendBlockUnlocked(const Block& block);
    void handleReorg(const std::vector<Block>& newChain);
    uint64_t calculateChainWork(const std::vector<Block>& chain);
};

void Ledger::Impl::runMaintenance() {
    while (running) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(60), [this] { return !running.load(); });
        
        if (!running) break;
        
        adjustDifficulty();
        
        if (autoCompact && blocks.size() > maxBlocksInMemory) {
            pruneOldBlocks();
        }
    }
}

void Ledger::Impl::adjustDifficulty() {
    if (blocks.size() < difficultyAdjustmentInterval) return;
    
    uint64_t startIdx = blocks.size() - difficultyAdjustmentInterval;
    uint64_t startTime = blocks[startIdx].timestamp;
    uint64_t endTime = blocks.back().timestamp;
    
    uint64_t actualTime = endTime - startTime;
    uint64_t expectedTime = targetBlockTime * difficultyAdjustmentInterval;
    
    if (actualTime < expectedTime / 2) {
        currentDifficulty++;
    } else if (actualTime > expectedTime * 2) {
        if (currentDifficulty > 1) currentDifficulty--;
    }
}

void Ledger::Impl::pruneOldBlocks() {
    if (blocks.size() <= maxBlocksInMemory) return;
    
    size_t toRemove = blocks.size() - maxBlocksInMemory;
    
    for (size_t i = 0; i < toRemove; i++) {
        std::string key = "block:" + std::to_string(blocks[i].height);
        db.put(key, blocks[i].serialize());
    }
    
    blocks.erase(blocks.begin(), blocks.begin() + toRemove);
}

void Ledger::Impl::rebuildIndexes() {
    blockHashIndex.clear();
    eventIndex.clear();
    eventsByAuthor.clear();
    eventsByType.clear();
    
    for (const auto& block : blocks) {
        blockHashIndex[block.hash] = block.height;
        
        for (size_t i = 0; i < block.events.size(); i++) {
            const auto& event = block.events[i];
            
            EventIndex idx;
            idx.id = event.id;
            idx.blockHeight = block.height;
            idx.indexInBlock = i;
            idx.type = event.type;
            idx.author = event.author;
            idx.hash = event.hash;
            
            eventIndex[event.id] = idx;
            eventsByAuthor[event.author].push_back(event.id);
            eventsByType[event.type].push_back(event.id);
        }
    }
}

bool Ledger::Impl::validateBlock(const Block& block) {
    if (block.computeHash() != block.hash) return false;
    if (block.computeMerkleRoot() != block.merkleRoot) return false;
    if (!block.meetsTarget()) return false;
    
    if (block.height > 0) {
        if (blocks.empty()) return false;
        if (block.prevHash != blocks.back().hash) return false;
        if (block.height != blocks.back().height + 1) return false;
        if (block.timestamp < blocks.back().timestamp) return false;
    }
    
    for (const auto& event : block.events) {
        if (!validateEvent(event)) return false;
    }
    
    return true;
}

bool Ledger::Impl::validateEvent(const Event& event) {
    if (event.computeHash() != event.hash) return false;
    return event.verify();
}

void Ledger::Impl::handleReorg(const std::vector<Block>& newChain) {
    if (newChain.empty()) return;
    
    uint64_t forkHeight = newChain[0].height;
    
    if (forkHeight >= blocks.size()) return;
    
    uint64_t newWork = calculateChainWork(newChain);
    uint64_t currentWork = 0;
    for (size_t i = forkHeight; i < blocks.size(); i++) {
        currentWork = saturatingAddWork(currentWork, blocks[i].difficulty);
    }
    
    if (newWork <= currentWork) return;
    
    ChainFork fork;
    fork.forkHeight = forkHeight;
    fork.forkHash = blocks[forkHeight].prevHash;
    fork.alternateChain.assign(blocks.begin() + forkHeight, blocks.end());
    fork.totalWork = currentWork;
    forks.push_back(fork);
    
    blocks.erase(blocks.begin() + forkHeight, blocks.end());
    
    for (const auto& block : newChain) {
        blocks.push_back(block);
        blockHashIndex[block.hash] = block.height;
    }
    
    currentHeight = blocks.back().height + 1;
    totalWork = calculateChainWork(blocks);
    
    rebuildIndexes();
    
    if (reorgCallback) {
        reorgCallback(forkHeight, blocks.back().height);
    }
}

uint64_t Ledger::Impl::calculateChainWork(const std::vector<Block>& chain) {
    uint64_t work = 0;
    for (const auto& block : chain) {
        work = saturatingAddWork(work, block.difficulty);
    }
    return work;
}

Ledger::Ledger() : impl_(std::make_unique<Impl>()) {
    impl_->currentHeight = 0;
    impl_->eventCounter = 0;
    impl_->totalWork = 0;
    impl_->currentDifficulty = 1;
    impl_->running = false;
    impl_->maxPendingEvents = 10000;
    impl_->maxBlocksInMemory = 1000;
    impl_->targetBlockTime = 600;
    impl_->difficultyAdjustmentInterval = 2016;
    impl_->autoCompact = true;
}

Ledger::~Ledger() {
    close();
}

bool Ledger::open(const std::string& dbPath) {
    bool needGenesis = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);

        if (!impl_->db.open(dbPath)) return false;

        std::string integrityDetails;
        if (!impl_->db.integrityCheck(&integrityDetails)) {
            std::string backupPath = dbPath + ".bak";
            if (!std::filesystem::exists(backupPath)) {
                impl_->db.close();
                return false;
            }
            if (!impl_->db.restore(backupPath)) {
                impl_->db.close();
                return false;
            }
            if (!impl_->db.integrityCheck(&integrityDetails)) {
                impl_->db.close();
                return false;
            }
        }

        impl_->pendingEvents.clear();
        impl_->blocks.clear();
        impl_->blockHashIndex.clear();
        impl_->eventIndex.clear();
        impl_->eventsByAuthor.clear();
        impl_->eventsByType.clear();
        impl_->forks.clear();

        impl_->currentHeight = 0;
        impl_->eventCounter = 0;
        impl_->totalWork = 0;
        impl_->currentDifficulty = 1;

        auto heightData = impl_->db.get("meta:height");
        if (!heightData.empty()) {
            impl_->currentHeight = readU64(heightData.data());
        }

        auto counterData = impl_->db.get("meta:eventCounter");
        if (!counterData.empty()) {
            impl_->eventCounter = readU64(counterData.data());
        }

        auto workData = impl_->db.get("meta:totalWork");
        if (!workData.empty()) {
            impl_->totalWork = readU64(workData.data());
        }

        auto diffData = impl_->db.get("meta:difficulty");
        if (!diffData.empty()) {
            impl_->currentDifficulty = readU32(diffData.data());
        }

        uint64_t storedHeight = impl_->currentHeight;
        uint64_t loadLimit = std::min(storedHeight, static_cast<uint64_t>(impl_->maxBlocksInMemory));
        bool recovered = false;
        uint64_t recoveredHeight = 0;

        for (uint64_t i = 0; i < loadLimit; i++) {
            auto blockData = impl_->db.get("block:" + std::to_string(i));
            if (blockData.empty()) {
                recovered = true;
                break;
            }

            Block block = Block::deserialize(blockData);
            if (block.height != i || block.computeHash() != block.hash) {
                recovered = true;
                break;
            }
            if (i > 0 && block.prevHash != impl_->blocks.back().hash) {
                recovered = true;
                break;
            }

            impl_->blocks.push_back(block);
            recoveredHeight = i + 1;
        }

        if (recovered) {
            impl_->currentHeight = recoveredHeight;
            impl_->eventCounter = maxEventIdFromBlocks(impl_->blocks);
            impl_->totalWork = chainWorkFromBlocks(impl_->blocks);
            impl_->currentDifficulty = impl_->blocks.empty() ? 1 : impl_->blocks.back().difficulty;

            for (uint64_t i = recoveredHeight; i < storedHeight; i++) {
                impl_->db.del("block:" + std::to_string(i));
            }

            std::vector<uint8_t> heightBuf;
            writeU64(heightBuf, impl_->currentHeight);
            impl_->db.put("meta:height", heightBuf);

            std::vector<uint8_t> counterBuf;
            writeU64(counterBuf, impl_->eventCounter);
            impl_->db.put("meta:eventCounter", counterBuf);

            std::vector<uint8_t> workBuf;
            writeU64(workBuf, impl_->totalWork);
            impl_->db.put("meta:totalWork", workBuf);

            std::vector<uint8_t> diffBuf;
            writeU32(diffBuf, impl_->currentDifficulty);
            impl_->db.put("meta:difficulty", diffBuf);
        }

        impl_->rebuildIndexes();

        needGenesis = (impl_->currentHeight == 0);
    }

    if (needGenesis) {
        Block genesis = createGenesisBlock();
        appendBlock(genesis);
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->running = true;
    }
    impl_->maintenanceThread = std::thread(&Impl::runMaintenance, impl_.get());

    return true;
}

void Ledger::close() {
    impl_->running = false;
    impl_->cv.notify_all();
    
    if (impl_->maintenanceThread.joinable()) {
        impl_->maintenanceThread.join();
    }
    
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> heightBuf;
    writeU64(heightBuf, impl_->currentHeight);
    impl_->db.put("meta:height", heightBuf);
    
    std::vector<uint8_t> counterBuf;
    writeU64(counterBuf, impl_->eventCounter);
    impl_->db.put("meta:eventCounter", counterBuf);
    
    std::vector<uint8_t> workBuf;
    writeU64(workBuf, impl_->totalWork);
    impl_->db.put("meta:totalWork", workBuf);
    
    std::vector<uint8_t> diffBuf;
    writeU32(diffBuf, impl_->currentDifficulty);
    impl_->db.put("meta:difficulty", diffBuf);

    std::string backupPath = impl_->db.getPath().empty() ? "" : (impl_->db.getPath() + ".bak");
    if (!backupPath.empty()) {
        impl_->db.backup(backupPath);
    }

    impl_->db.close();
}

bool Ledger::append(const Event& event) {
    Event e = event;

    {
        std::lock_guard<std::mutex> lock(impl_->mtx);

        if (e.id == 0) {
            e.id = ++impl_->eventCounter;
        } else if (e.id > impl_->eventCounter) {
            impl_->eventCounter = e.id;
        }

        if (!impl_->blocks.empty()) {
            e.prevHash = impl_->blocks.back().hash;
        }

        e.hash = e.computeHash();
        if (impl_->signer) {
            bool emptySig = true;
            for (uint8_t b : e.signature) {
                if (b != 0) { emptySig = false; break; }
            }
            if (emptySig) {
                e.signature = impl_->signer(e.hash);
            }
        }

        std::string key = "event:" + std::to_string(e.id);
        impl_->db.put(key, e.serialize());

        std::vector<uint8_t> counterBuf;
        writeU64(counterBuf, impl_->eventCounter);
        impl_->db.put("meta:eventCounter", counterBuf);
    }

    {
        std::lock_guard<std::mutex> lock(impl_->eventMtx);
        if (impl_->pendingEvents.size() >= impl_->maxPendingEvents) {
            return false;
        }
        impl_->pendingEvents.push_back(e);
    }

    if (impl_->eventCallback) impl_->eventCallback(e);
    return true;
}

bool Ledger::Impl::appendBlockUnlocked(const Block& block) {
    Block b = block;

    if (b.difficulty == 0) {
        b.difficulty = currentDifficulty;
    }
    if (b.merkleRoot == crypto::Hash256{}) {
        b.merkleRoot = b.computeMerkleRoot();
    }
    if (b.hash == crypto::Hash256{}) {
        b.hash = b.computeHash();
    }
    b.totalWork = saturatingAddWork(totalWork, b.difficulty);

    blocks.push_back(b);
    blockHashIndex[b.hash] = b.height;
    currentHeight = b.height + 1;
    totalWork = b.totalWork;

    uint64_t maxEventId = eventCounter;
    for (size_t i = 0; i < b.events.size(); i++) {
        const auto& event = b.events[i];

        if (event.id > maxEventId) maxEventId = event.id;

        EventIndex idx;
        idx.id = event.id;
        idx.blockHeight = b.height;
        idx.indexInBlock = i;
        idx.type = event.type;
        idx.author = event.author;
        idx.hash = event.hash;

        eventIndex[event.id] = idx;
        eventsByAuthor[event.author].push_back(event.id);
        eventsByType[event.type].push_back(event.id);

        std::string ekey = "event:" + std::to_string(event.id);
        if (!db.exists(ekey)) {
            db.put(ekey, event.serialize());
        }
    }
    eventCounter = maxEventId;

    {
        std::lock_guard<std::mutex> lock(eventMtx);
        if (!pendingEvents.empty() && !b.events.empty()) {
            std::unordered_set<std::string> included;
            included.reserve(b.events.size() * 2);
            for (const auto& e : b.events) included.insert(crypto::toHex(e.hash));
            pendingEvents.erase(
                std::remove_if(pendingEvents.begin(), pendingEvents.end(),
                               [&included](const Event& e) { return included.count(crypto::toHex(e.hash)) > 0; }),
                pendingEvents.end());
        }
    }

    std::string key = "block:" + std::to_string(b.height);
    db.put(key, b.serialize());

    std::vector<uint8_t> heightBuf;
    writeU64(heightBuf, currentHeight);
    db.put("meta:height", heightBuf);

    std::vector<uint8_t> workBuf;
    writeU64(workBuf, totalWork);
    db.put("meta:totalWork", workBuf);

    std::vector<uint8_t> counterBuf;
    writeU64(counterBuf, eventCounter);
    db.put("meta:eventCounter", counterBuf);

    std::vector<uint8_t> diffBuf;
    writeU32(diffBuf, currentDifficulty);
    db.put("meta:difficulty", diffBuf);

    if (blockCallback) blockCallback(b);
    return true;
}

bool Ledger::appendBlock(const Block& block) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->appendBlockUnlocked(block);
}

bool Ledger::appendBlockWithValidation(const Block& block) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->validateBlock(block)) {
        return false;
    }

    return impl_->appendBlockUnlocked(block);
}

Event Ledger::getEvent(uint64_t id) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->eventIndex.find(id);
    if (it != impl_->eventIndex.end()) {
        const EventIndex& idx = it->second;
        if (idx.blockHeight < impl_->blocks.size()) {
            const Block& block = impl_->blocks[idx.blockHeight];
            if (idx.indexInBlock < block.events.size()) {
                return block.events[idx.indexInBlock];
            }
        }
    }
    
    auto data = impl_->db.get("event:" + std::to_string(id));
    if (data.empty()) return Event{};
    return Event::deserialize(data);
}

Block Ledger::getBlock(uint64_t height) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (height < impl_->blocks.size()) {
        return impl_->blocks[height];
    }
    
    auto data = impl_->db.get("block:" + std::to_string(height));
    if (data.empty()) return Block{};
    return Block::deserialize(data);
}

Block Ledger::getBlockByHash(const crypto::Hash256& hash) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->blockHashIndex.find(hash);
    if (it != impl_->blockHashIndex.end()) {
        if (it->second < impl_->blocks.size()) {
            return impl_->blocks[it->second];
        }
    }
    
    for (const auto& b : impl_->blocks) {
        if (b.hash == hash) return b;
    }
    
    return Block{};
}

std::vector<Event> Ledger::getEventsByType(EventType type, size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<Event> result;
    
    auto it = impl_->eventsByType.find(type);
    if (it != impl_->eventsByType.end()) {
        for (size_t i = 0; i < it->second.size() && result.size() < limit; i++) {
            Event e = getEvent(it->second[i]);
            if (e.id != 0) result.push_back(e);
        }
    }
    
    return result;
}

std::vector<Event> Ledger::getEventsByAuthor(const crypto::PublicKey& author) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<Event> result;
    
    auto it = impl_->eventsByAuthor.find(author);
    if (it != impl_->eventsByAuthor.end()) {
        for (uint64_t id : it->second) {
            Event e = getEvent(id);
            if (e.id != 0) result.push_back(e);
        }
    }
    
    return result;
}

std::vector<Event> Ledger::getEventsByTimeRange(uint64_t startTime, uint64_t endTime, size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<Event> result;
    
    for (const auto& block : impl_->blocks) {
        if (block.timestamp < startTime) continue;
        if (block.timestamp > endTime) break;
        
        for (const auto& event : block.events) {
            if (event.timestamp >= startTime && event.timestamp <= endTime) {
                result.push_back(event);
                if (result.size() >= limit) return result;
            }
        }
    }
    
    return result;
}

std::vector<Event> Ledger::getPendingEvents() const {
    std::lock_guard<std::mutex> lock(impl_->eventMtx);
    return impl_->pendingEvents;
}

size_t Ledger::getPendingEventCount() const {
    std::lock_guard<std::mutex> lock(impl_->eventMtx);
    return impl_->pendingEvents.size();
}

uint64_t Ledger::height() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->currentHeight;
}

uint64_t Ledger::eventCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->eventCounter;
}

crypto::Hash256 Ledger::tipHash() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->blocks.empty()) return crypto::Hash256{};
    return impl_->blocks.back().hash;
}

Block Ledger::tip() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->blocks.empty()) return Block{};
    return impl_->blocks.back();
}

uint32_t Ledger::currentDifficulty() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->currentDifficulty;
}

bool Ledger::verify() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    for (size_t i = 0; i < impl_->blocks.size(); i++) {
        if (!verifyBlock(impl_->blocks[i])) return false;
        
        if (i > 0) {
            if (impl_->blocks[i].prevHash != impl_->blocks[i-1].hash) return false;
            if (impl_->blocks[i].height != impl_->blocks[i-1].height + 1) return false;
        }
    }
    
    return true;
}

bool Ledger::verifyEvent(const Event& event) const {
    return event.verify();
}

bool Ledger::verifyBlock(const Block& block) const {
    if (block.computeHash() != block.hash) return false;
    if (block.computeMerkleRoot() != block.merkleRoot) return false;
    
    for (const auto& e : block.events) {
        if (!e.verify()) return false;
    }
    
    return true;
}

bool Ledger::verifyChain() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    crypto::Hash256 prevHash{};
    uint64_t expectedHeight = 0;
    uint64_t totalWork = 0;
    
    for (const auto& block : impl_->blocks) {
        if (block.height != expectedHeight) return false;
        if (expectedHeight > 0 && block.prevHash != prevHash) return false;
        if (block.computeHash() != block.hash) return false;
        if (block.computeMerkleRoot() != block.merkleRoot) return false;
        
        prevHash = block.hash;
        expectedHeight++;
        totalWork = saturatingAddWork(totalWork, block.difficulty);
    }
    
    return true;
}

void Ledger::onNewBlock(std::function<void(const Block&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->blockCallback = callback;
}

void Ledger::onNewEvent(std::function<void(const Event&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->eventCallback = callback;
}

void Ledger::setSigner(std::function<crypto::Signature(const crypto::Hash256&)> signer) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->signer = signer;
}

Block Ledger::createGenesisBlock() {
    Block genesis;
    genesis.height = 0;
    genesis.timestamp = 1735000000;
    genesis.prevHash = crypto::Hash256{};
    genesis.nonce = 0;
    genesis.difficulty = 1;
    genesis.totalWork = 2;
    
    Event genesisEvent{};
    genesisEvent.id = 0;
    genesisEvent.timestamp = genesis.timestamp;
    genesisEvent.type = EventType::GENESIS;
    std::string msg = "Satoshi gave us money without banks. I give you brains without corporations. - Kepler";
    genesisEvent.data.assign(msg.begin(), msg.end());
    genesisEvent.prevHash = crypto::Hash256{};
    genesisEvent.author = crypto::PublicKey{};
    genesisEvent.signature = crypto::Signature{};
    genesisEvent.hash = genesisEvent.computeHash();
    
    genesis.events.push_back(genesisEvent);
    genesis.merkleRoot = genesis.computeMerkleRoot();
    genesis.hash = genesis.computeHash();
    
    return genesis;
}

Ledger::LedgerStats Ledger::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    Ledger::LedgerStats stats{};
    stats.totalBlocks = impl_->blocks.size();
    stats.totalEvents = impl_->eventCounter;
    stats.totalSize = 0;
    
    for (const auto& block : impl_->blocks) {
        stats.totalSize += block.serialize().size();
    }
    
    if (stats.totalBlocks > 0) {
        stats.avgBlockSize = stats.totalSize / stats.totalBlocks;
        stats.avgEventsPerBlock = stats.totalEvents / stats.totalBlocks;
    }
    
    stats.verificationRate = 1.0;
    
    return stats;
}

std::vector<Block> Ledger::getRecentBlocks(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<Block> result;
    size_t start = impl_->blocks.size() > count ? impl_->blocks.size() - count : 0;
    
    for (size_t i = start; i < impl_->blocks.size(); i++) {
        result.push_back(impl_->blocks[i]);
    }
    
    return result;
}

bool Ledger::exportToFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    
    uint64_t blockCount = impl_->blocks.size();
    f.write(reinterpret_cast<char*>(&blockCount), 8);
    
    for (const auto& block : impl_->blocks) {
        auto data = block.serialize();
        uint32_t size = data.size();
        f.write(reinterpret_cast<char*>(&size), 4);
        f.write(reinterpret_cast<const char*>(data.data()), size);
    }
    
    return true;
}

bool Ledger::importFromFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    
    uint64_t blockCount = 0;
    f.read(reinterpret_cast<char*>(&blockCount), 8);
    
    impl_->blocks.clear();
    impl_->blockHashIndex.clear();
    impl_->eventIndex.clear();
    impl_->eventsByAuthor.clear();
    impl_->eventsByType.clear();
    
    for (uint64_t i = 0; i < blockCount; i++) {
        uint32_t size;
        f.read(reinterpret_cast<char*>(&size), 4);
        if (f.eof()) break;
        
        std::vector<uint8_t> data(size);
        f.read(reinterpret_cast<char*>(data.data()), size);
        
        Block block = Block::deserialize(data);
        impl_->blocks.push_back(block);
        impl_->blockHashIndex[block.hash] = block.height;
        
        if (block.height >= impl_->currentHeight) {
            impl_->currentHeight = block.height + 1;
        }
    }
    
    impl_->rebuildIndexes();
    
    return true;
}

void Ledger::compact() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->db.compact();
}

uint64_t Ledger::getChainWork() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->totalWork;
}

}
}
