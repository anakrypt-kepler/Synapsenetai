#include "core/consensus.h"
#include "core/ledger.h"
#include "core/poe_v1_objects.h"
#include "core/transfer.h"
#include "crypto/address.h"
#include "crypto/crypto.h"
#include "quantum/application_signature.h"
#include "quantum/identity_registry.h"
#include "quantum/quantum_security.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace synapse;

namespace {

core::TxInput makeInput(const crypto::PublicKey& pub, const crypto::PrivateKey& priv,
                       const crypto::Hash256& prevHash, uint32_t index) {
    core::TxInput in{};
    in.prevTxHash = prevHash;
    in.outputIndex = index;
    in.pubKey = pub;
    crypto::Hash256 stub{};
    in.signature = crypto::sign(stub, priv);
    return in;
}

core::Transaction makeTx(const crypto::PrivateKey& priv, const std::string& toAddr) {
    core::Transaction tx{};
    crypto::PublicKey pub = crypto::derivePublicKey(priv);
    tx.timestamp = 1735000000ULL;
    tx.fee = 1;
    tx.status = core::TxStatus::PENDING;
    crypto::Hash256 zero{};
    tx.inputs.push_back(makeInput(pub, priv, zero, 0));
    core::TxOutput out{};
    out.amount = 1000;
    out.address = toAddr;
    tx.outputs.push_back(out);
    return tx;
}

quantum::HybridKeyPair freshHybrid() {
    quantum::HybridSig signer;
    return signer.generateKeyPair();
}

void testTransactionRoundTrip() {
    crypto::PrivateKey priv{};
    auto rnd = crypto::randomBytes(32);
    std::memcpy(priv.data(), rnd.data(), 32);
    crypto::PublicKey pub = crypto::derivePublicKey(priv);
    std::string toAddr = crypto::canonicalWalletAddressFromPublicKey(pub);

    auto hybrid = freshHybrid();

    core::TransferManager mgr;
    core::Transaction tx = makeTx(priv, toAddr);
    bool ok = mgr.signTransaction(tx, priv, hybrid);
    assert(ok);
    assert(!tx.quantumSignature.empty());
    assert(quantum::isApplicationSignatureEnvelope(tx.quantumSignature));
    assert(tx.verify());

    auto wire = tx.serialize();
    core::Transaction parsed = core::Transaction::deserialize(wire);
    assert(parsed.txid == tx.txid);
    assert(parsed.quantumSignature == tx.quantumSignature);
    assert(parsed.verify());

    auto tampered = wire;
    tampered.back() ^= 0x01;
    core::Transaction bad = core::Transaction::deserialize(tampered);
    if (!bad.quantumSignature.empty()) {
        assert(!quantum::isApplicationSignatureEnvelope(bad.quantumSignature) || bad.quantumSignature != tx.quantumSignature);
    }
    std::cout << "[ok] transaction hybrid sign + serialize + tamper" << std::endl;
}

void testVoteRoundTrip() {
    crypto::PrivateKey priv{};
    auto rnd = crypto::randomBytes(32);
    std::memcpy(priv.data(), rnd.data(), 32);
    auto hybrid = freshHybrid();

    core::Vote v{};
    v.eventId = 42;
    v.type = core::VoteType::APPROVE;
    v.scoreGiven = 0.95;
    v.timestamp = 1735000100ULL;
    bool ok = core::Consensus::signVote(v, priv, hybrid);
    assert(ok);
    assert(!v.quantumSignature.empty());
    assert(v.verify());

    auto wire = v.serialize();
    core::Vote parsed = core::Vote::deserialize(wire);
    assert(parsed.eventId == v.eventId);
    assert(parsed.quantumSignature == v.quantumSignature);
    assert(parsed.verify());

    auto tampered = wire;
    if (tampered.size() > 8) tampered[8] ^= 0x01;
    core::Vote bad = core::Vote::deserialize(tampered);
    if (bad.eventId != 0 || !bad.quantumSignature.empty()) {
        assert(!bad.verify());
    }
    std::cout << "[ok] vote hybrid sign + serialize + tamper" << std::endl;
}

void testBlockV2RoundTrip() {
    core::Block block{};
    block.version = core::BLOCK_VERSION_PQ;
    block.height = 123;
    block.timestamp = 1735000200ULL;
    block.nonce = 7;
    block.difficulty = 1;
    block.totalWork = 1;
    block.merkleRoot = block.computeMerkleRoot();
    block.hash = block.computeHash();

    crypto::PrivateKey priv{};
    auto rnd = crypto::randomBytes(32);
    std::memcpy(priv.data(), rnd.data(), 32);
    block.producer = crypto::derivePublicKey(priv);
    block.producerSignature = crypto::sign(block.hash, priv);

    auto hybrid = freshHybrid();
    std::vector<uint8_t> payload(block.hash.begin(), block.hash.end());
    std::vector<uint8_t> binding(block.producer.begin(), block.producer.end());
    block.producerQuantumSignature = quantum::signApplicationPayload(
        "core.block.producer", payload, binding, hybrid);
    assert(!block.producerQuantumSignature.empty());

    auto wire = block.serialize();
    core::Block parsed = core::Block::deserialize(wire);
    assert(parsed.version == core::BLOCK_VERSION_PQ);
    assert(parsed.height == 123);
    assert(parsed.producerSignature == block.producerSignature);
    assert(parsed.producerQuantumSignature == block.producerQuantumSignature);
    assert(quantum::isApplicationSignatureEnvelope(parsed.producerQuantumSignature));
    std::cout << "[ok] block v2 serialize + producer classical+PQ sig" << std::endl;
}

void testLegacyBlockStillParses() {
    core::Block block{};
    block.version = core::BLOCK_VERSION_LEGACY;
    block.height = 0;
    block.timestamp = 1735000000ULL;
    block.nonce = 0;
    block.difficulty = 1;
    block.totalWork = 1;
    block.merkleRoot = block.computeMerkleRoot();
    block.hash = block.computeHash();

    auto wire = block.serialize();
    core::Block parsed = core::Block::deserialize(wire);
    assert(parsed.version == core::BLOCK_VERSION_LEGACY);
    assert(parsed.height == 0);
    assert(parsed.producerQuantumSignature.empty());
    std::cout << "[ok] legacy block wire format unchanged" << std::endl;
}

void testIdentityRegistryTofu() {
    auto hybridA = freshHybrid();
    auto hybridB = freshHybrid();

    crypto::PrivateKey priv{};
    auto rnd = crypto::randomBytes(32);
    std::memcpy(priv.data(), rnd.data(), 32);
    crypto::PublicKey pub = crypto::derivePublicKey(priv);
    std::string address = crypto::canonicalWalletAddressFromPublicKey(pub);

    auto& registry = quantum::IdentityRegistry::instance();
    registry.clear();

    std::vector<uint8_t> payload = {0x00};
    std::vector<uint8_t> binding(pub.begin(), pub.end());
    auto envA = quantum::signApplicationPayload("test.domain", payload, binding, hybridA);
    auto envB = quantum::signApplicationPayload("test.domain", payload, binding, hybridB);
    assert(!envA.empty() && !envB.empty());

    assert(registry.verifyBinding(address, envA));
    assert(registry.verifyBinding(address, envA));
    assert(!registry.verifyBinding(address, envB));

    registry.clear();
    std::cout << "[ok] identity registry TOFU accepts first, rejects rotated" << std::endl;
}

void testDeterministicHybridDerivation() {
    quantum::HybridSig signer;
    std::vector<uint8_t> seedA(64, 0x11);
    std::vector<uint8_t> seedB(64, 0x22);

    auto kpA1 = signer.generateKeyPairFromSeed(seedA);
    auto kpA2 = signer.generateKeyPairFromSeed(seedA);
    auto kpB  = signer.generateKeyPairFromSeed(seedB);

    assert(!kpA1.classicPublicKey.empty());
    assert(!kpA1.pqcPublicKey.empty());

    assert(kpA1.classicPublicKey == kpA2.classicPublicKey);
    assert(kpA1.classicSecretKey == kpA2.classicSecretKey);
    assert(kpA1.pqcPublicKey == kpA2.pqcPublicKey);
    assert(kpA1.pqcSecretKey == kpA2.pqcSecretKey);

    assert(kpA1.classicPublicKey != kpB.classicPublicKey);
    assert(kpA1.pqcPublicKey != kpB.pqcPublicKey);
    std::cout << "[ok] hybrid keypair deterministic for fixed seed, distinct for different seeds" << std::endl;
}

void testValidationVoteV1PqRoundTrip() {
    auto classical = crypto::generateKeyPair();
    quantum::HybridSig signer;
    std::vector<uint8_t> seed(classical.privateKey.begin(), classical.privateKey.end());
    auto hybrid = signer.generateKeyPairFromSeed(seed);
    assert(!hybrid.pqcSecretKey.empty());

    core::poe_v1::ValidationVoteV1 vote;
    vote.submitId.fill(0x42);
    vote.prevBlockHash.fill(0x24);
    vote.flags = 0x1;
    vote.scores = {100, 80, 90};

    assert(core::poe_v1::signValidationVoteV1(vote, classical.privateKey, hybrid));
    assert(!vote.quantumSignature.empty());
    assert(quantum::isApplicationSignatureEnvelope(vote.quantumSignature));

    auto serialized = vote.serialize();
    auto parsed = core::poe_v1::ValidationVoteV1::deserialize(serialized);
    assert(parsed.has_value());
    assert(parsed->quantumSignature == vote.quantumSignature);
    assert(parsed->signature == vote.signature);
    assert(parsed->verifySignature());

    core::poe_v1::ValidationVoteV1 classicalOnly;
    classicalOnly.submitId.fill(0x11);
    classicalOnly.prevBlockHash.fill(0x22);
    assert(core::poe_v1::signValidationVoteV1(classicalOnly, classical.privateKey));
    assert(classicalOnly.quantumSignature.empty());
    auto legacy = classicalOnly.serialize();
    auto legacyParsed = core::poe_v1::ValidationVoteV1::deserialize(legacy);
    assert(legacyParsed.has_value());
    assert(legacyParsed->quantumSignature.empty());
    assert(legacyParsed->verifySignature());

    auto corrupt = serialized;
    corrupt[corrupt.size() - 1] ^= 0x01;
    auto corruptParsed = core::poe_v1::ValidationVoteV1::deserialize(corrupt);
    if (corruptParsed.has_value()) {
        std::string reason;
        bool ok = corruptParsed->verifySignature(&reason);
        assert(!ok);
    }
    std::cout << "[ok] ValidationVoteV1 hybrid sign/serialize/verify round-trip" << std::endl;
}

}

int main() {
    testTransactionRoundTrip();
    testVoteRoundTrip();
    testBlockV2RoundTrip();
    testLegacyBlockStillParses();
    testIdentityRegistryTofu();
    testDeterministicHybridDerivation();
    testValidationVoteV1PqRoundTrip();
    std::cout << "all pq-integration tests passed" << std::endl;
    return 0;
}
