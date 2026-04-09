#include "quantum/quantum_security.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <vector>

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

static bool testPqcBackendStatusSurface() {
    const auto pqc = getPQCBackendStatus();
#ifdef USE_LIBOQS
    if (!pqc.kyberReal) return false;
    if (!pqc.dilithiumReal) return false;
    if (!pqc.sphincsReal) return false;
#else
    if (pqc.kyberReal) return false;
    if (pqc.dilithiumReal) return false;
    if (pqc.sphincsReal) return false;
#endif

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
    return 0;
}
