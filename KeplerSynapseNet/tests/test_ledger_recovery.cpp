#include "core/ledger.h"
#include "database/database.h"
#include "crypto/crypto.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

static std::string makeUniquePath(const std::string& suffix) {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto base = std::filesystem::temp_directory_path() / ("synapsenet_ledger_recovery_" + std::to_string(static_cast<unsigned long long>(now)) + suffix);
    return base.string();
}

static std::vector<uint8_t> encodeU64(uint64_t v) {
    std::vector<uint8_t> out;
    out.reserve(8);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    return out;
}

static void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

static void appendU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

static std::vector<uint8_t> makeSerializedBlockPrefix(uint32_t eventCount) {
    std::vector<uint8_t> out;
    out.reserve(8 + 8 + 32 + 32 + 4 + 4 + 8 + 4);
    appendU64(out, 7);
    appendU64(out, 1735000000);
    out.insert(out.end(), 32, 0);
    out.insert(out.end(), 32, 0);
    appendU32(out, 0);
    appendU32(out, 1);
    appendU64(out, 0);
    appendU32(out, eventCount);
    return out;
}

static synapse::core::Event makeKnowledgeEvent(uint64_t id, const std::string& payload) {
    synapse::core::Event e{};
    e.id = id;
    e.timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    e.type = synapse::core::EventType::KNOWLEDGE;
    e.data.assign(payload.begin(), payload.end());
    auto keyPair = synapse::crypto::generateKeyPair();
    e.author = keyPair.publicKey;
    e.hash = e.computeHash();
    return e;
}

static void testDatabaseIntegrityCheck() {
    std::string dbPath = makeUniquePath("_integrity.db");

    synapse::database::Database db;
    assert(db.open(dbPath));
    assert(db.put("key", "value"));

    std::string details;
    assert(db.integrityCheck(&details));

    db.close();
    std::filesystem::remove(dbPath);
}

static void testLedgerRecoversFromMissingBlocksAndRebuildsIndexes() {
    std::string dbPath = makeUniquePath("_ledger.db");

    synapse::core::Ledger ledger;
    assert(ledger.open(dbPath));

    synapse::core::Block b{};
    b.height = ledger.height();
    b.timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    b.prevHash = ledger.tipHash();
    b.difficulty = ledger.currentDifficulty();
    b.events.push_back(makeKnowledgeEvent(1, "recovery_test"));
    b.merkleRoot = b.computeMerkleRoot();
    b.hash = b.computeHash();

    assert(ledger.appendBlock(b));
    assert(ledger.height() == 2);
    assert(ledger.eventCount() == 1);

    ledger.close();
    assert(std::filesystem::exists(dbPath + ".bak"));

    synapse::database::Database db;
    assert(db.open(dbPath));
    assert(db.put("meta:height", encodeU64(9)));
    assert(db.del("block:1"));
    db.close();

    synapse::core::Ledger recovered;
    assert(recovered.open(dbPath));

    assert(recovered.height() == 1);
    assert(recovered.eventCount() == 0);
    assert(recovered.verifyChain());
    auto events = recovered.getEventsByType(synapse::core::EventType::KNOWLEDGE, 10);
    assert(events.empty());

    recovered.close();

    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-shm");
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + ".bak");
    std::filesystem::remove(dbPath + ".bak-shm");
    std::filesystem::remove(dbPath + ".bak-wal");
}

static void testBlockDeserializeRejectsOversizedEventCount() {
    auto payload = makeSerializedBlockPrefix(1001);
    synapse::core::Block parsed = synapse::core::Block::deserialize(payload);
    assert(parsed.hash == synapse::crypto::Hash256{});
    assert(parsed.events.empty());
}

static void testBlockDeserializeRejectsOversizedEventLength() {
    auto payload = makeSerializedBlockPrefix(1);
    appendU32(payload, (1U << 20) + 1U);

    synapse::core::Block parsed = synapse::core::Block::deserialize(payload);
    assert(parsed.hash == synapse::crypto::Hash256{});
    assert(parsed.events.empty());
}

static void testBlockDeserializeRejectsTruncatedEventPayload() {
    auto payload = makeSerializedBlockPrefix(1);
    appendU32(payload, 32);
    payload.insert(payload.end(), 8, 0xAB);

    synapse::core::Block parsed = synapse::core::Block::deserialize(payload);
    assert(parsed.hash == synapse::crypto::Hash256{});
    assert(parsed.events.empty());
}

static void testBlockDeserializeAcceptsValidPayload() {
    synapse::core::Event e{};
    e.id = 1;
    e.timestamp = 1735000000;
    e.type = synapse::core::EventType::KNOWLEDGE;
    e.data = {'o', 'k'};
    e.hash = e.computeHash();

    synapse::core::Block block{};
    block.height = 1;
    block.timestamp = 1735000001;
    block.prevHash.fill(0);
    block.events.push_back(e);
    block.nonce = 7;
    block.difficulty = 1;
    block.totalWork = 2;
    block.merkleRoot = block.computeMerkleRoot();
    block.hash = block.computeHash();

    const auto encoded = block.serialize();
    synapse::core::Block parsed = synapse::core::Block::deserialize(encoded);
    assert(parsed.events.size() == 1);
    assert(parsed.hash == parsed.computeHash());
}

int main() {
    testDatabaseIntegrityCheck();
    testLedgerRecoversFromMissingBlocksAndRebuildsIndexes();
    testBlockDeserializeRejectsOversizedEventCount();
    testBlockDeserializeRejectsOversizedEventLength();
    testBlockDeserializeRejectsTruncatedEventPayload();
    testBlockDeserializeAcceptsValidPayload();
    return 0;
}
