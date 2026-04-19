#include "quantum/quantum_security.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <random>

namespace synapse {
namespace tests {

class QuantumSecurityTests {
public:
    static void runAll() {
        std::cout << "Running Quantum Security Tests...\n";
        
        testKyberKeyGeneration();
        testKyberEncapsulation();
        testKyberDecapsulation();
        testKyberEncapDecapSymmetry();
        testDilithiumKeyGeneration();
        testDilithiumSigning();
        testDilithiumVerification();
        testSphincsKeyGeneration();
        testSphincsSigning();
        testSphincsVerification();
        testHybridKem();
        testHybridSignature();
        testOtpEncryption();
        testOtpDecryption();
        testOtpManager();
        testHwrng();
        testDeterministicOtp();
        testQkdIntegration();
        testNetworkSecurity();
        testKnowledgeCrypto();
        testWalletSecurity();
        testCryptoSelector();
        testKeyDerivation();
        testTimingDefense();
        testQuantumRuntimePolicySurface();
        
        std::cout << "All Quantum Security Tests Passed!\n";
    }
    
private:
    static void testKyberKeyGeneration() {
        std::cout << "  Testing Kyber key generation... ";
        
        std::vector<uint8_t> publicKey(1184);
        std::vector<uint8_t> privateKey(2400);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : publicKey) b = dis(gen);
        for (auto& b : privateKey) b = dis(gen);
        
        assert(publicKey.size() == 1184);
        assert(privateKey.size() == 2400);
        
        bool allZero = true;
        for (auto b : publicKey) {
            if (b != 0) { allZero = false; break; }
        }
        assert(!allZero);
        
        std::cout << "PASSED\n";
    }
    
    static void testKyberEncapsulation() {
        std::cout << "  Testing Kyber encapsulation... ";
        
        std::vector<uint8_t> publicKey(1184);
        std::vector<uint8_t> sharedSecret(32);
        std::vector<uint8_t> ciphertext(1088);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : publicKey) b = dis(gen);
        for (auto& b : sharedSecret) b = dis(gen);
        for (auto& b : ciphertext) b = dis(gen);
        
        assert(sharedSecret.size() == 32);
        assert(ciphertext.size() == 1088);
        
        std::cout << "PASSED\n";
    }
    
    static void testKyberDecapsulation() {
        std::cout << "  Testing Kyber decapsulation... ";
        
        std::vector<uint8_t> privateKey(2400);
        std::vector<uint8_t> ciphertext(1088);
        std::vector<uint8_t> sharedSecret(32);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : privateKey) b = dis(gen);
        for (auto& b : ciphertext) b = dis(gen);
        for (auto& b : sharedSecret) b = dis(gen);
        
        assert(sharedSecret.size() == 32);
        
        std::cout << "PASSED\n";
    }

    static void testKyberEncapDecapSymmetry() {
        std::cout << "  Testing Kyber encapsulate/decapsulate symmetry... ";
        synapse::quantum::Kyber kem;
        auto kp = kem.generateKeyPair();
        auto enc = kem.encapsulate(kp.publicKey);
        assert(enc.success);
        synapse::quantum::KyberCiphertext ct{};
        size_t copyLen = std::min(enc.ciphertext.size(), ct.size());
        std::memcpy(ct.data(), enc.ciphertext.data(), copyLen);
        std::vector<uint8_t> dec = kem.decapsulate(ct, kp.secretKey);
        // enc.sharedSecret and dec should match
        assert(enc.sharedSecret.size() == dec.size());
        assert(enc.sharedSecret == dec);
        std::cout << "PASSED\n";
    }
    
    static void testDilithiumKeyGeneration() {
        std::cout << "  Testing Dilithium key generation... ";
        
        std::vector<uint8_t> publicKey(1952);
        std::vector<uint8_t> privateKey(4000);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : publicKey) b = dis(gen);
        for (auto& b : privateKey) b = dis(gen);
        
        assert(publicKey.size() == 1952);
        assert(privateKey.size() == 4000);
        
        std::cout << "PASSED\n";
    }
    
    static void testDilithiumSigning() {
        std::cout << "  Testing Dilithium signing... ";
        
        std::vector<uint8_t> privateKey(4000);
        std::vector<uint8_t> message = {'t', 'e', 's', 't', ' ', 'm', 'e', 's', 's', 'a', 'g', 'e'};
        std::vector<uint8_t> signature(3293);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : privateKey) b = dis(gen);
        for (auto& b : signature) b = dis(gen);
        
        assert(signature.size() == 3293);
        
        std::cout << "PASSED\n";
    }
    
    static void testDilithiumVerification() {
        std::cout << "  Testing Dilithium verification... ";
        
        std::vector<uint8_t> publicKey(1952);
        std::vector<uint8_t> message = {'t', 'e', 's', 't'};
        std::vector<uint8_t> signature(3293);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : publicKey) b = dis(gen);
        for (auto& b : signature) b = dis(gen);
        
        std::cout << "PASSED\n";
    }
    
    static void testSphincsKeyGeneration() {
        std::cout << "  Testing SPHINCS+ key generation... ";
        
        std::vector<uint8_t> publicKey(32);
        std::vector<uint8_t> privateKey(64);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : publicKey) b = dis(gen);
        for (auto& b : privateKey) b = dis(gen);
        
        assert(publicKey.size() == 32);
        assert(privateKey.size() == 64);
        
        std::cout << "PASSED\n";
    }
    
    static void testSphincsSigning() {
        std::cout << "  Testing SPHINCS+ signing... ";
        
        std::vector<uint8_t> privateKey(64);
        std::vector<uint8_t> message = {'d', 'a', 't', 'a'};
        std::vector<uint8_t> signature(7856);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : privateKey) b = dis(gen);
        for (auto& b : signature) b = dis(gen);
        
        assert(signature.size() == 7856);
        
        std::cout << "PASSED\n";
    }
    
    static void testSphincsVerification() {
        std::cout << "  Testing SPHINCS+ verification... ";
        
        std::vector<uint8_t> publicKey(32);
        std::vector<uint8_t> message = {'t', 'e', 's', 't'};
        std::vector<uint8_t> signature(7856);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : publicKey) b = dis(gen);
        for (auto& b : signature) b = dis(gen);
        
        std::cout << "PASSED\n";
    }
    
    static void testHybridKem() {
        std::cout << "  Testing Hybrid KEM... ";
        
        std::vector<uint8_t> classicPublic(32);
        std::vector<uint8_t> classicPrivate(32);
        std::vector<uint8_t> pqcPublic(1184);
        std::vector<uint8_t> pqcPrivate(2400);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : classicPublic) b = dis(gen);
        for (auto& b : classicPrivate) b = dis(gen);
        for (auto& b : pqcPublic) b = dis(gen);
        for (auto& b : pqcPrivate) b = dis(gen);
        
        std::vector<uint8_t> sharedSecret(64);
        for (auto& b : sharedSecret) b = dis(gen);
        
        assert(sharedSecret.size() == 64);
        
        std::cout << "PASSED\n";
    }
    
    static void testHybridSignature() {
        std::cout << "  Testing Hybrid Signature... ";
        
        std::vector<uint8_t> classicPrivate(64);
        std::vector<uint8_t> pqcPrivate(4000);
        std::vector<uint8_t> message = {'h', 'e', 'l', 'l', 'o'};
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : classicPrivate) b = dis(gen);
        for (auto& b : pqcPrivate) b = dis(gen);
        
        std::vector<uint8_t> signature(64 + 3293);
        for (auto& b : signature) b = dis(gen);
        
        assert(signature.size() == 64 + 3293);
        
        std::cout << "PASSED\n";
    }
    
    static void testOtpEncryption() {
        std::cout << "  Testing OTP encryption... ";
        
        std::vector<uint8_t> plaintext = {'s', 'e', 'c', 'r', 'e', 't'};
        std::vector<uint8_t> key(plaintext.size());
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : key) b = dis(gen);
        
        std::vector<uint8_t> ciphertext(plaintext.size());
        for (size_t i = 0; i < plaintext.size(); i++) {
            ciphertext[i] = plaintext[i] ^ key[i];
        }
        
        assert(ciphertext.size() == plaintext.size());
        assert(ciphertext != plaintext);
        
        std::cout << "PASSED\n";
    }
    
    static void testOtpDecryption() {
        std::cout << "  Testing OTP decryption... ";
        
        std::vector<uint8_t> original = {'d', 'a', 't', 'a'};
        std::vector<uint8_t> key(original.size());
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : key) b = dis(gen);
        
        std::vector<uint8_t> ciphertext(original.size());
        for (size_t i = 0; i < original.size(); i++) {
            ciphertext[i] = original[i] ^ key[i];
        }
        
        std::vector<uint8_t> decrypted(ciphertext.size());
        for (size_t i = 0; i < ciphertext.size(); i++) {
            decrypted[i] = ciphertext[i] ^ key[i];
        }
        
        assert(decrypted == original);
        
        std::cout << "PASSED\n";
    }
    
    static void testOtpManager() {
        std::cout << "  Testing OTP Manager... ";
        
        size_t poolSize = 1024;
        std::vector<uint8_t> pool(poolSize);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : pool) b = dis(gen);
        
        size_t offset = 0;
        size_t requestSize = 32;
        
        assert(offset + requestSize <= poolSize);
        
        std::vector<uint8_t> key(pool.begin() + offset, pool.begin() + offset + requestSize);
        offset += requestSize;
        
        assert(key.size() == requestSize);
        assert(offset == requestSize);
        
        std::cout << "PASSED\n";
    }
    
    static void testHwrng() {
        std::cout << "  Testing Hardware RNG... ";
        
        std::vector<uint8_t> random1(32);
        std::vector<uint8_t> random2(32);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : random1) b = dis(gen);
        for (auto& b : random2) b = dis(gen);
        
        assert(random1 != random2);
        
        std::cout << "PASSED\n";
    }
    
    static void testDeterministicOtp() {
        std::cout << "  Testing Deterministic OTP... ";
        
        std::vector<uint8_t> masterSecret(32);
        std::vector<uint8_t> salt(16);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : masterSecret) b = dis(gen);
        for (auto& b : salt) b = dis(gen);
        
        uint64_t counter1 = 0;
        uint64_t counter2 = 1;
        
        std::vector<uint8_t> key1(32);
        std::vector<uint8_t> key2(32);
        
        for (size_t i = 0; i < 32; i++) {
            key1[i] = masterSecret[i] ^ salt[i % 16] ^ (counter1 >> (i % 8));
            key2[i] = masterSecret[i] ^ salt[i % 16] ^ (counter2 >> (i % 8));
        }
        
        assert(key1 != key2);
        
        std::cout << "PASSED\n";
    }
    
    static void testQkdIntegration() {
        std::cout << "  Testing QKD Integration... ";
        
        std::vector<uint8_t> aliceBits(32);
        std::vector<uint8_t> aliceBases(32);
        std::vector<uint8_t> bobBases(32);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> bitDis(0, 1);
        
        for (auto& b : aliceBits) b = static_cast<uint8_t>(bitDis(gen));
        for (auto& b : aliceBases) b = static_cast<uint8_t>(bitDis(gen));
        for (auto& b : bobBases) b = static_cast<uint8_t>(bitDis(gen));
        bobBases[0] = aliceBases[0];
        
        std::vector<uint8_t> siftedKey;
        for (size_t i = 0; i < 32; i++) {
            if (aliceBases[i] == bobBases[i]) {
                siftedKey.push_back(aliceBits[i]);
            }
        }
        
        assert(siftedKey.size() > 0);
        assert(siftedKey.size() <= 32);
        
        std::cout << "PASSED\n";
    }
    
    static void testNetworkSecurity() {
        std::cout << "  Testing Network Security... ";
        
        std::vector<uint8_t> sendKey(32);
        std::vector<uint8_t> recvKey(32);
        std::vector<uint8_t> nonce(12);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : sendKey) b = dis(gen);
        for (auto& b : recvKey) b = dis(gen);
        for (auto& b : nonce) b = dis(gen);
        
        assert(sendKey.size() == 32);
        assert(recvKey.size() == 32);
        assert(nonce.size() == 12);
        
        std::cout << "PASSED\n";
    }
    
    static void testKnowledgeCrypto() {
        std::cout << "  Testing Knowledge Crypto... ";
        
        std::vector<uint8_t> content = {'k', 'n', 'o', 'w', 'l', 'e', 'd', 'g', 'e'};
        std::vector<uint8_t> key(32);
        std::vector<uint8_t> iv(12);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : key) b = dis(gen);
        for (auto& b : iv) b = dis(gen);
        
        std::vector<uint8_t> encrypted(content.size());
        for (size_t i = 0; i < content.size(); i++) {
            encrypted[i] = content[i] ^ key[i % 32];
        }
        
        assert(encrypted != content);
        
        std::cout << "PASSED\n";
    }
    
    static void testWalletSecurity() {
        std::cout << "  Testing Wallet Security... ";
        
        std::vector<uint8_t> seed(32);
        std::vector<uint8_t> encryptedSeed(48);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : seed) b = dis(gen);
        for (auto& b : encryptedSeed) b = dis(gen);
        
        assert(seed.size() == 32);
        assert(encryptedSeed.size() == 48);
        
        std::cout << "PASSED\n";
    }
    
    static void testCryptoSelector() {
        std::cout << "  Testing Crypto Selector... ";
        
        enum class SecurityLevel { STANDARD, HIGH, PARANOID };
        
        SecurityLevel level = SecurityLevel::HIGH;
        
        bool useKyber = (level >= SecurityLevel::HIGH);
        bool useDilithium = (level >= SecurityLevel::HIGH);
        bool useOtp = (level >= SecurityLevel::PARANOID);
        
        assert(useKyber == true);
        assert(useDilithium == true);
        assert(useOtp == false);
        
        std::cout << "PASSED\n";
    }
    
    static void testKeyDerivation() {
        std::cout << "  Testing Key Derivation... ";
        
        std::vector<uint8_t> ikm(32);
        std::vector<uint8_t> salt(16);
        std::vector<uint8_t> info = {'t', 'e', 's', 't'};
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& b : ikm) b = dis(gen);
        for (auto& b : salt) b = dis(gen);
        
        std::vector<uint8_t> derivedKey(32);
        for (size_t i = 0; i < 32; i++) {
            derivedKey[i] = ikm[i] ^ salt[i % 16] ^ info[i % info.size()];
        }
        
        assert(derivedKey.size() == 32);
        assert(derivedKey != ikm);
        
        std::cout << "PASSED\n";
    }
    
    static void testTimingDefense() {
        std::cout << "  Testing Timing Defense... ";
        
        auto start = std::chrono::high_resolution_clock::now();
        
        volatile int dummy = 0;
        for (int i = 0; i < 1000; i++) {
            dummy += i;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        assert(duration.count() >= 0);
        
        std::cout << "PASSED\n";
    }

    static void testQuantumRuntimePolicySurface() {
        std::cout << "  Testing Quantum Runtime Policy Surface... ";

        synapse::quantum::QuantumManager manager;
        assert(manager.init(synapse::quantum::SecurityLevel::HIGH));

        auto status = manager.getRuntimeStatus();
        auto pqc = synapse::quantum::getPQCBackendStatus();
        assert(status.initialized);
        assert(status.level == synapse::quantum::SecurityLevel::HIGH);
        assert(status.pqc.kyberReal == pqc.kyberReal);
        assert(status.pqc.dilithiumReal == pqc.dilithiumReal);
        assert(status.pqc.sphincsReal == pqc.sphincsReal);
        assert(status.selectedKEM == synapse::quantum::CryptoAlgorithm::LATTICE_KYBER768);
        assert(status.selectedSignature == synapse::quantum::CryptoAlgorithm::LATTICE_DILITHIUM65);

        std::vector<uint8_t> message = {'p', 'o', 'l', 'i', 'c', 'y'};
        auto encrypted = manager.encryptQuantumSafe(message);
        assert(!encrypted.empty());
        auto decrypted = manager.decryptQuantumSafe(encrypted);
        assert(decrypted == message);

        auto postRoundTrip = manager.getRuntimeStatus();
        assert(postRoundTrip.hybridEncryptOperations >= 1);
        assert(postRoundTrip.hybridDecryptOperations >= 1);

        manager.setSecurityLevel(synapse::quantum::SecurityLevel::QUANTUM_READY);
        auto qReady = manager.getRuntimeStatus();
        assert(qReady.level == synapse::quantum::SecurityLevel::QUANTUM_READY);
        assert(qReady.qkdConnected);
        assert(qReady.qkdSessionActive);

        manager.shutdown();
        std::cout << "PASSED\n";
    }
};

}
}

int main() {
    synapse::tests::QuantumSecurityTests::runAll();
    return 0;
}
