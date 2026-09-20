#include "quantum/quantum_security.h"
#include "quantum/application_signature.h"
#include "quantum/identity_registry.h"
#include "core/consensus.h"
#include "core/ledger.h"
#include "core/poe_v1_objects.h"
#include "crypto/crypto.h"
#include "crypto/address.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

using namespace synapse;
using namespace synapse::quantum;

template <size_t N>
static std::array<uint8_t, N> toArray(const std::vector<uint8_t>& in) {
    std::array<uint8_t, N> out{};
    const size_t n = std::min(N, in.size());
    std::copy_n(in.begin(), n, out.begin());
    return out;
}

static bool testKyberRoundTrip() {
    Kyber kyber;
    auto kp = kyber.generateKeyPair();
    auto encapsulated = kyber.encapsulate(kp.publicKey);
    if (!encapsulated.success) return false;
    if (encapsulated.sharedSecret.size() != KYBER_SHARED_SECRET_SIZE) return false;

    auto ct = toArray<KYBER_CIPHERTEXT_SIZE>(encapsulated.ciphertext);
    auto decapsulated = kyber.decapsulate(ct, kp.secretKey);
    if (decapsulated.size() != KYBER_SHARED_SECRET_SIZE) return false;
    return decapsulated == encapsulated.sharedSecret;
}

static bool testDilithiumRoundTripAndTamper() {
    Dilithium dilithium;
    const std::vector<uint8_t> message = {'q', 'u', 'a', 'n', 't', 'u', 'm'};
    auto kp = dilithium.generateKeyPair();

    auto sig = dilithium.sign(message, kp.secretKey);
    if (!sig.success) return false;
    if (sig.signature.size() != DILITHIUM_SIGNATURE_SIZE) return false;

    auto sigArr = toArray<DILITHIUM_SIGNATURE_SIZE>(sig.signature);
    if (!dilithium.verify(message, sigArr, kp.publicKey)) return false;

    auto tamperedMessage = message;
    tamperedMessage[0] ^= 0x01;
    if (dilithium.verify(tamperedMessage, sigArr, kp.publicKey)) return false;

    auto tamperedSig = sigArr;
    tamperedSig[5] ^= 0x80;
    if (dilithium.verify(message, tamperedSig, kp.publicKey)) return false;

    auto other = dilithium.generateKeyPair();
    if (dilithium.verify(message, sigArr, other.publicKey)) return false;
    return true;
}

static bool testDilithiumSignVerifyRoundTrip() {
    Dilithium dilithium;
    const std::vector<uint8_t> message = {'r','o','u','n','d','t','r','i','p'};
    auto kp = dilithium.generateKeyPair();
    auto sig = dilithium.sign(message, kp.secretKey);
    if (!sig.success) return false;
    // convert signature vector to fixed-size array for verify
    auto sigArr = toArray<DILITHIUM_SIGNATURE_SIZE>(sig.signature);
    if (!dilithium.verify(message, sigArr, kp.publicKey)) return false;
    return true;
}

static bool testSphincsRoundTripAndTamper() {
    Sphincs sphincs;
    const std::vector<uint8_t> message = {'s', 'p', 'h', 'i', 'n', 'c', 's'};
    auto kp = sphincs.generateKeyPair();

    auto sig = sphincs.sign(message, kp.secretKey);
    if (!sig.success) return false;
    if (sig.signature.size() != SPHINCS_SIGNATURE_SIZE) return false;

    auto sigArr = toArray<SPHINCS_SIGNATURE_SIZE>(sig.signature);
    if (!sphincs.verify(message, sigArr, kp.publicKey)) return false;

    auto tamperedMessage = message;
    tamperedMessage[1] ^= 0x01;
    if (sphincs.verify(tamperedMessage, sigArr, kp.publicKey)) return false;

    auto tamperedSig = sigArr;
    tamperedSig[11] ^= 0x40;
    if (sphincs.verify(message, tamperedSig, kp.publicKey)) return false;

    auto other = sphincs.generateKeyPair();
    if (sphincs.verify(message, sigArr, other.publicKey)) return false;
    return true;
}

static bool testCryptoSelectorQuantumReadyPriority() {
    CryptoSelector selector;
    selector.setSecurityLevel(SecurityLevel::QUANTUM_READY);
    if (selector.selectKEM() != CryptoAlgorithm::QKD_BB84) return false;
    if (selector.selectSignature() != CryptoAlgorithm::HYBRID_SIG) return false;
    if (selector.selectEncryption() != CryptoAlgorithm::OTP_VERNAM) return false;

    selector.disableAlgorithm(CryptoAlgorithm::QKD_BB84);
    if (selector.selectKEM() != CryptoAlgorithm::HYBRID_KEM) return false;
    selector.disableAlgorithm(CryptoAlgorithm::HYBRID_KEM);
    if (selector.selectKEM() != CryptoAlgorithm::LATTICE_KYBER768) return false;
    selector.disableAlgorithm(CryptoAlgorithm::LATTICE_KYBER768);
    return selector.selectKEM() == CryptoAlgorithm::CLASSIC_X25519;
}

static bool testQuantumManagerRoundTrip(SecurityLevel level) {
    QuantumManager manager;
    if (!manager.init(level)) return false;
    if (!manager.isQuantumSafe()) return false;

    const std::vector<uint8_t> message = {'n', 'e', 't', '-', 's', 'e', 'c', 'u', 'r', 'e'};
    auto encrypted = manager.encryptQuantumSafe(message);
    if (encrypted.empty()) return false;
    auto decrypted = manager.decryptQuantumSafe(encrypted);
    if (decrypted != message) return false;

    auto signature = manager.signQuantumSafe(message);
    if (signature.empty()) return false;
    if (!manager.verifyQuantumSafe(message, signature)) return false;

    auto tamperedMessage = message;
    tamperedMessage[0] ^= 0x01;
    if (manager.verifyQuantumSafe(tamperedMessage, signature)) return false;

    auto tamperedSignature = signature;
    if (tamperedSignature.empty()) return false;
    tamperedSignature[0] ^= 0x01;
    if (manager.verifyQuantumSafe(message, tamperedSignature)) return false;

    auto key = manager.generateQuantumSafeKey(64);
    if (key.size() != 64) return false;

    manager.shutdown();
    return true;
}

static bool testQuantumRuntimeStatusAndDowngradeBoundary() {
    QuantumManager manager;
    if (!manager.init(SecurityLevel::QUANTUM_READY)) return false;

    const auto pqc = getPQCBackendStatus();
    auto initial = manager.getRuntimeStatus();
    if (!initial.initialized) return false;
    if (initial.level != SecurityLevel::QUANTUM_READY) return false;
    if (initial.pqc.kyberReal != pqc.kyberReal) return false;
    if (initial.pqc.dilithiumReal != pqc.dilithiumReal) return false;
    if (initial.pqc.sphincsReal != pqc.sphincsReal) return false;
    if (!initial.qkdConnected) return false;
    if (!initial.qkdSessionActive) return false;
    if (initial.selectedKEM != CryptoAlgorithm::QKD_BB84) return false;
    if (initial.selectedSignature != CryptoAlgorithm::HYBRID_SIG) return false;

    const std::vector<uint8_t> message = {'q', 'k', 'd', '-', 'b', 'o', 'u', 'n', 'd', 'a', 'r', 'y'};
    auto qkdEncrypted = manager.encryptQuantumSafe(message);
    if (qkdEncrypted.empty()) return false;
    if (manager.decryptQuantumSafe(qkdEncrypted) != message) return false;

    manager.setSecurityLevel(SecurityLevel::HIGH);
    auto downgraded = manager.getRuntimeStatus();
    if (downgraded.level != SecurityLevel::HIGH) return false;
    if (downgraded.qkdConnected) return false;
    if (downgraded.qkdSessionActive) return false;
    if (downgraded.selectedKEM != CryptoAlgorithm::LATTICE_KYBER768) return false;
    if (downgraded.selectedSignature != CryptoAlgorithm::LATTICE_DILITHIUM65) return false;

    auto downgradedDecrypt = manager.decryptQuantumSafe(qkdEncrypted);
    if (!downgradedDecrypt.empty()) return false;
    auto afterBoundary = manager.getRuntimeStatus();
    if (afterBoundary.qkdFallbackDecryptOperations < 1) return false;

    auto highEncrypted = manager.encryptQuantumSafe(message);
    if (highEncrypted.empty()) return false;
    if (manager.decryptQuantumSafe(highEncrypted) != message) return false;
    auto highStatus = manager.getRuntimeStatus();
    if (highStatus.hybridEncryptOperations < 1) return false;
    if (highStatus.hybridDecryptOperations < 1) return false;

    manager.setSecurityLevel(SecurityLevel::QUANTUM_READY);
    auto upgraded = manager.getRuntimeStatus();
    if (upgraded.level != SecurityLevel::QUANTUM_READY) return false;
    if (!upgraded.qkdConnected) return false;
    if (!upgraded.qkdSessionActive) return false;

    manager.shutdown();
    return true;
}

// Real AND-mode HybridSig (Ed25519 + ML-DSA-65). Do not size-assert random
// bytes the way test_quantum.cpp does.
static bool testHybridSigRoundTrip() {
    const auto pqc = getPQCBackendStatus();
    if (!pqc.dilithiumReal) {
        std::cerr << "SKIP/FAIL: testHybridSigRoundTrip needs real Dilithium/ML-DSA-65 "
                     "(dilithiumReal=false); not faking a pass\n";
        return false;
    }

    HybridSig hs;
    auto kp = hs.generateKeyPair();
    if (kp.classicPublicKey.empty() || kp.classicSecretKey.empty()
        || kp.pqcPublicKey.empty() || kp.pqcSecretKey.empty()) {
        std::cerr << "testHybridSigRoundTrip: generateKeyPair missing a half\n";
        return false;
    }
    if (kp.pqcAlgo != CryptoAlgorithm::LATTICE_DILITHIUM65) {
        std::cerr << "testHybridSigRoundTrip: parameter set is not ML-DSA-65\n";
        return false;
    }
    if (kp.pqcPublicKey.size() != DILITHIUM_PUBLIC_KEY_SIZE
        || kp.pqcSecretKey.size() != DILITHIUM_SECRET_KEY_SIZE) {
        std::cerr << "testHybridSigRoundTrip: PQC key sizes are not ML-DSA-65\n";
        return false;
    }

    const std::vector<uint8_t> message = {'h', 'y', 'b', 'r', 'i', 'd', '-', 's', 'i', 'g'};
    auto signedMsg = hs.sign(message, kp);
    if (!signedMsg.success) {
        std::cerr << "testHybridSigRoundTrip: sign failed\n";
        return false;
    }
    if (signedMsg.signature.size() < 64) {
        std::cerr << "testHybridSigRoundTrip: truncated classic prefix\n";
        return false;
    }
    if (signedMsg.signature.size() == 64) {
        std::cerr << "testHybridSigRoundTrip: missing PQC half\n";
        return false;
    }
    if (!hs.verify(message, signedMsg.signature, kp)) {
        std::cerr << "testHybridSigRoundTrip: verify failed on honest signature\n";
        return false;
    }

    auto flipClassic = signedMsg.signature;
    flipClassic[0] ^= 0x01;
    if (hs.verify(message, flipClassic, kp)) {
        std::cerr << "testHybridSigRoundTrip: classic-byte flip still verified\n";
        return false;
    }

    auto flipPqc = signedMsg.signature;
    flipPqc[64] ^= 0x01;
    if (hs.verify(message, flipPqc, kp)) {
        std::cerr << "testHybridSigRoundTrip: pqc-byte flip still verified\n";
        return false;
    }

    HybridKeyPair emptyPqcPk = kp;
    emptyPqcPk.pqcPublicKey.clear();
    if (hs.verify(message, signedMsg.signature, emptyPqcPk)) {
        std::cerr << "testHybridSigRoundTrip: empty pqc public key verified\n";
        return false;
    }

    HybridKeyPair emptyPqcSk = kp;
    emptyPqcSk.pqcSecretKey.clear();
    auto emptySign = hs.sign(message, emptyPqcSk);
    if (emptySign.success) {
        std::cerr << "testHybridSigRoundTrip: empty pqc secret key signed\n";
        return false;
    }

    return true;
}

// KQAS envelope: real HybridSig over domain-separated transcript, not a size assert.
static bool testApplicationSignatureAndMode() {
    const auto pqc = getPQCBackendStatus();
    if (!pqc.dilithiumReal) {
        std::cerr << "testApplicationSignatureAndMode needs real ML-DSA-65\n";
        return false;
    }

    HybridSig hs;
    auto kp = hs.generateKeyPair();
    if (kp.pqcPublicKey.size() != DILITHIUM_PUBLIC_KEY_SIZE) {
        std::cerr << "testApplicationSignatureAndMode: not ML-DSA-65 PK size\n";
        return false;
    }

    const std::vector<uint8_t> payload = {'v', 'o', 't', 'e'};
    const std::vector<uint8_t> binding = {0x01, 0x02, 0x03};
    auto env = signApplicationPayload("core.consensus.vote", payload, binding, kp);
    if (env.empty()) {
        std::cerr << "testApplicationSignatureAndMode: sign failed\n";
        return false;
    }
    if (!isApplicationSignatureEnvelope(env)) {
        std::cerr << "testApplicationSignatureAndMode: envelope parse failed\n";
        return false;
    }
    if (!verifyApplicationPayload("core.consensus.vote", payload, binding, env)) {
        std::cerr << "testApplicationSignatureAndMode: AND-verify failed\n";
        return false;
    }
    if (verifyApplicationPayload("core.block.producer", payload, binding, env)) {
        std::cerr << "testApplicationSignatureAndMode: domain replay accepted\n";
        return false;
    }

    auto flipped = env;
    flipped.back() ^= 0x01;
    if (verifyApplicationPayload("core.consensus.vote", payload, binding, flipped)) {
        std::cerr << "testApplicationSignatureAndMode: PQC-byte flip still verified\n";
        return false;
    }
    return true;
}

static bool testVoteTrailerHybridSig() {
    const auto pqc = getPQCBackendStatus();
    if (!pqc.dilithiumReal) {
        std::cerr << "testVoteTrailerHybridSig needs real ML-DSA-65\n";
        return false;
    }

    crypto::PrivateKey priv{};
    auto rnd = crypto::randomBytes(priv.size());
    if (rnd.size() != priv.size()) return false;
    std::memcpy(priv.data(), rnd.data(), priv.size());

    HybridSig hs;
    auto hybrid = hs.generateKeyPair();

    synapse::core::Vote v{};
    v.eventId = 7;
    v.type = synapse::core::VoteType::APPROVE;
    v.scoreGiven = 1.0;
    v.timestamp = 1735000000ULL;
    if (!synapse::core::Consensus::signVote(v, priv, hybrid)) {
        std::cerr << "testVoteTrailerHybridSig: signVote failed\n";
        return false;
    }
    if (v.quantumSignature.empty()) {
        std::cerr << "testVoteTrailerHybridSig: missing KQAS trailer\n";
        return false;
    }
    if (!v.verify()) {
        std::cerr << "testVoteTrailerHybridSig: honest vote rejected\n";
        return false;
    }

    auto flipped = v;
    flipped.quantumSignature.back() ^= 0x01;
    if (flipped.verify()) {
        std::cerr << "testVoteTrailerHybridSig: size-assert fake still accepted\n";
        return false;
    }

    synapse::core::Vote classical{};
    classical.eventId = 8;
    classical.type = synapse::core::VoteType::APPROVE;
    classical.timestamp = 1735000001ULL;
    if (!synapse::core::Consensus::signVote(classical, priv)) return false;
    if (!classical.quantumSignature.empty()) return false;
    if (!classical.verify()) {
        std::cerr << "testVoteTrailerHybridSig: classical-only vote rejected\n";
        return false;
    }
    return true;
}

static bool testPoeVoteTrailerHybridSig() {
    const auto pqc = getPQCBackendStatus();
    if (!pqc.dilithiumReal) {
        std::cerr << "testPoeVoteTrailerHybridSig needs real ML-DSA-65\n";
        return false;
    }

    auto classical = crypto::generateKeyPair();
    HybridSig hs;
    auto hybrid = hs.generateKeyPair();

    synapse::core::poe_v1::ValidationVoteV1 vote;
    vote.submitId.fill(0x42);
    vote.prevBlockHash.fill(0x24);
    vote.flags = 0x1;
    vote.scores = {100, 80, 90};
    if (!synapse::core::poe_v1::signValidationVoteV1(vote, classical.privateKey, hybrid)) {
        std::cerr << "testPoeVoteTrailerHybridSig: sign failed\n";
        return false;
    }
    if (!vote.verifySignature()) {
        std::cerr << "testPoeVoteTrailerHybridSig: honest vote rejected\n";
        return false;
    }

    auto flipped = vote;
    flipped.quantumSignature.back() ^= 0x01;
    std::string reason;
    if (flipped.verifySignature(&reason)) {
        std::cerr << "testPoeVoteTrailerHybridSig: size-assert fake still accepted\n";
        return false;
    }
    return true;
}

static bool testIdentityRegistryHybridSig() {
    const auto pqc = getPQCBackendStatus();
    if (!pqc.dilithiumReal) {
        std::cerr << "testIdentityRegistryHybridSig needs real ML-DSA-65\n";
        return false;
    }

    auto classical = crypto::generateKeyPair();
    const std::string address = crypto::canonicalWalletAddressFromPublicKey(classical.publicKey);
    if (address.empty()) return false;

    HybridSig hs;
    auto kp = hs.generateKeyPair();
    const std::vector<uint8_t> payload = {'i', 'd'};
    std::vector<uint8_t> binding(classical.publicKey.begin(), classical.publicKey.end());
    auto env = signApplicationPayload("test.domain", payload, binding, kp);
    if (env.empty()) return false;

    auto& registry = IdentityRegistry::instance();
    registry.clear();
    if (!registry.verifyBinding(address, "test.domain", payload, binding, env)) {
        std::cerr << "testIdentityRegistryHybridSig: honest bind rejected\n";
        return false;
    }

    auto flipped = env;
    flipped.back() ^= 0x01;
    registry.clear();
    if (registry.verifyBinding(address, "test.domain", payload, binding, flipped)) {
        std::cerr << "testIdentityRegistryHybridSig: forged envelope TOFU-bound\n";
        return false;
    }
    registry.clear();
    return true;
}

static bool testProducerTrailerHybridSig() {
    const auto pqc = getPQCBackendStatus();
    if (!pqc.dilithiumReal) {
        std::cerr << "testProducerTrailerHybridSig needs real ML-DSA-65\n";
        return false;
    }

    synapse::core::Block block{};
    block.version = synapse::core::BLOCK_VERSION_PQ;
    block.height = 1;
    block.timestamp = 1735000200ULL;
    block.nonce = 7;
    block.difficulty = 1;
    block.totalWork = 1;
    block.merkleRoot = block.computeMerkleRoot();
    block.hash = block.computeHash();

    auto classical = crypto::generateKeyPair();
    block.producer = classical.publicKey;
    block.producerSignature = crypto::sign(block.hash, classical.privateKey);

    HybridSig hs;
    auto hybrid = hs.generateKeyPair();
    std::vector<uint8_t> payload(block.hash.begin(), block.hash.end());
    std::vector<uint8_t> binding(block.producer.begin(), block.producer.end());
    block.producerQuantumSignature = signApplicationPayload(
        "core.block.producer", payload, binding, hybrid);
    if (block.producerQuantumSignature.empty()) {
        std::cerr << "testProducerTrailerHybridSig: sign failed\n";
        return false;
    }

    synapse::core::Ledger ledger;
    if (!ledger.verifyBlock(block)) {
        std::cerr << "testProducerTrailerHybridSig: honest producer trailer rejected\n";
        return false;
    }

    auto flipped = block;
    flipped.producerQuantumSignature.back() ^= 0x01;
    if (ledger.verifyBlock(flipped)) {
        std::cerr << "testProducerTrailerHybridSig: parse-only fake accepted\n";
        return false;
    }
    return true;
}

static bool testPqcBackendStatusSurface() {
    const auto pqc = getPQCBackendStatus();
    if (!pqc.kyberReal) return false;
    if (!pqc.dilithiumReal) return false;
    if (!pqc.sphincsReal) return false;

    QuantumManager manager;
    if (!manager.init(SecurityLevel::HIGH)) return false;
    const auto status = manager.getRuntimeStatus();
    if (status.pqc.kyberReal != pqc.kyberReal) return false;
    if (status.pqc.dilithiumReal != pqc.dilithiumReal) return false;
    if (status.pqc.sphincsReal != pqc.sphincsReal) return false;
    manager.shutdown();
    return true;
}

int main() {
#ifndef USE_LIBOQS
    std::cerr << "SKIP: liboqs not available, PQC runtime tests skipped\n";
    return 0;
#else
    if (!testKyberRoundTrip()) {
        std::cerr << "testKyberRoundTrip failed\n";
        return 1;
    }
    if (!testDilithiumRoundTripAndTamper()) {
        std::cerr << "testDilithiumRoundTripAndTamper failed\n";
        return 1;
    }
    if (!testDilithiumSignVerifyRoundTrip()) {
        std::cerr << "testDilithiumSignVerifyRoundTrip failed\n";
        return 1;
    }
    if (!testSphincsRoundTripAndTamper()) {
        std::cerr << "testSphincsRoundTripAndTamper failed\n";
        return 1;
    }
    if (!testCryptoSelectorQuantumReadyPriority()) {
        std::cerr << "testCryptoSelectorQuantumReadyPriority failed\n";
        return 1;
    }
    if (!testQuantumManagerRoundTrip(SecurityLevel::HIGH)) {
        std::cerr << "testQuantumManagerRoundTrip(HIGH) failed\n";
        return 1;
    }
    if (!testQuantumManagerRoundTrip(SecurityLevel::QUANTUM_READY)) {
        std::cerr << "testQuantumManagerRoundTrip(QUANTUM_READY) failed\n";
        return 1;
    }
    if (!testQuantumRuntimeStatusAndDowngradeBoundary()) {
        std::cerr << "testQuantumRuntimeStatusAndDowngradeBoundary failed\n";
        return 1;
    }
    if (!testPqcBackendStatusSurface()) {
        std::cerr << "testPqcBackendStatusSurface failed\n";
        return 1;
    }
    if (!testHybridSigRoundTrip()) {
        std::cerr << "testHybridSigRoundTrip failed\n";
        return 1;
    }
    if (!testApplicationSignatureAndMode()) {
        std::cerr << "testApplicationSignatureAndMode failed\n";
        return 1;
    }
    if (!testVoteTrailerHybridSig()) {
        std::cerr << "testVoteTrailerHybridSig failed\n";
        return 1;
    }
    if (!testPoeVoteTrailerHybridSig()) {
        std::cerr << "testPoeVoteTrailerHybridSig failed\n";
        return 1;
    }
    if (!testIdentityRegistryHybridSig()) {
        std::cerr << "testIdentityRegistryHybridSig failed\n";
        return 1;
    }
    if (!testProducerTrailerHybridSig()) {
        std::cerr << "testProducerTrailerHybridSig failed\n";
        return 1;
    }
    return 0;
#endif
}
