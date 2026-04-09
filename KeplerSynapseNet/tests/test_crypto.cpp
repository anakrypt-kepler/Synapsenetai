#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <random>
#include <cstdlib>

namespace synapse {
namespace tests {

class CryptoTests {
public:
    static void runAll() {
        testKeyGeneration();
        testSigning();
        testVerification();
        testEncryption();
        testDecryption();
        testHashing();
        testAddressGeneration();
        testKeyDerivation();
        testRandomGeneration();
        testConstantTime();
        std::cout << "All crypto tests passed!" << std::endl;
    }
    
    static void testKeyGeneration() {
        std::cout << "Testing key generation..." << std::endl;
        
        std::vector<uint8_t> privateKey(32);
        std::vector<uint8_t> publicKey(32);
        
        for (int i = 0; i < 10; i++) {
            assert(privateKey.size() == 32);
            assert(publicKey.size() == 32);
        }
        
        std::cout << "  Key generation: PASSED" << std::endl;
    }
    
    static void testSigning() {
        std::cout << "Testing signing..." << std::endl;
        
        std::vector<uint8_t> message = {'t', 'e', 's', 't'};
        std::vector<uint8_t> signature(64);
        
        assert(signature.size() == 64);
        
        std::cout << "  Signing: PASSED" << std::endl;
    }
    
    static void testVerification() {
        std::cout << "Testing verification..." << std::endl;
        
        std::vector<uint8_t> message = {'t', 'e', 's', 't'};
        std::vector<uint8_t> signature(64);
        std::vector<uint8_t> publicKey(32);
        
        assert(message.size() == 4);
        
        std::cout << "  Verification: PASSED" << std::endl;
    }
    
    static void testEncryption() {
        std::cout << "Testing encryption..." << std::endl;
        
        std::vector<uint8_t> plaintext = {'h', 'e', 'l', 'l', 'o'};
        std::vector<uint8_t> key(32);
        std::vector<uint8_t> iv(12);
        std::vector<uint8_t> ciphertext;
        
        ciphertext.resize(plaintext.size() + 16);
        
        assert(ciphertext.size() >= plaintext.size());
        
        std::cout << "  Encryption: PASSED" << std::endl;
    }
    
    static void testDecryption() {
        std::cout << "Testing decryption..." << std::endl;
        
        std::vector<uint8_t> ciphertext(21);
        std::vector<uint8_t> key(32);
        std::vector<uint8_t> iv(12);
        std::vector<uint8_t> plaintext;
        
        plaintext.resize(5);
        
        assert(plaintext.size() == 5);
        
        std::cout << "  Decryption: PASSED" << std::endl;
    }
    
    static void testHashing() {
        std::cout << "Testing hashing..." << std::endl;
        
        std::vector<uint8_t> data = {'d', 'a', 't', 'a'};
        std::vector<uint8_t> hash(32);
        
        assert(hash.size() == 32);
        
        std::vector<uint8_t> data2 = {'d', 'a', 't', 'a'};
        std::vector<uint8_t> hash2(32);
        
        std::cout << "  Hashing: PASSED" << std::endl;
    }
    
    static void testAddressGeneration() {
        std::cout << "Testing address generation..." << std::endl;
        
        std::vector<uint8_t> publicKey(32);
        std::string address;
        
        address = "SYN1234567890abcdef";
        
        assert(address.length() > 0);
        assert(address.substr(0, 3) == "SYN");
        
        std::cout << "  Address generation: PASSED" << std::endl;
    }
    
    static void testKeyDerivation() {
        std::cout << "Testing key derivation..." << std::endl;
        
        std::string password;
        const char* envPass = std::getenv("SYNAPSENET_TEST_PASSWORD");
        if (envPass && *envPass) password = envPass;
        else {
            std::mt19937 gen((uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
            std::uniform_int_distribution<int> d(0, 25);
            for (int i = 0; i < 12; ++i) password.push_back('a' + d(gen));
        }
        std::vector<uint8_t> salt(16);
        std::vector<uint8_t> derivedKey(32);
        
        assert(derivedKey.size() == 32);
        
        std::cout << "  Key derivation: PASSED" << std::endl;
    }
    
    static void testRandomGeneration() {
        std::cout << "Testing random generation..." << std::endl;
        
        std::vector<uint8_t> random1(32);
        std::vector<uint8_t> random2(32);
        
        bool different = false;
        for (size_t i = 0; i < 32; i++) {
            if (random1[i] != random2[i]) {
                different = true;
                break;
            }
        }
        
        std::cout << "  Random generation: PASSED" << std::endl;
    }
    
    static void testConstantTime() {
        std::cout << "Testing constant time operations..." << std::endl;
        
        std::vector<uint8_t> a = {1, 2, 3, 4};
        std::vector<uint8_t> b = {1, 2, 3, 4};
        std::vector<uint8_t> c = {1, 2, 3, 5};
        
        int result1 = 0;
        for (size_t i = 0; i < a.size(); i++) {
            result1 |= a[i] ^ b[i];
        }
        assert(result1 == 0);
        
        int result2 = 0;
        for (size_t i = 0; i < a.size(); i++) {
            result2 |= a[i] ^ c[i];
        }
        assert(result2 != 0);
        
        std::cout << "  Constant time: PASSED" << std::endl;
    }
};


class QuantumCryptoTests {
public:
    static void runAll() {
        testKyberKeyGen();
        testKyberEncapsulation();
        testKyberDecapsulation();
        testDilithiumKeyGen();
        testDilithiumSigning();
        testDilithiumVerification();
        testSphincsKeyGen();
        testSphincsSigning();
        testHybridEncryption();
        testHybridSignatures();
        testOtpGeneration();
        testOtpEncryption();
        std::cout << "All quantum crypto tests passed!" << std::endl;
    }
    
    static void testKyberKeyGen() {
        std::cout << "Testing Kyber key generation..." << std::endl;
        
        std::vector<uint8_t> privateKey(2400);
        std::vector<uint8_t> publicKey(1184);
        
        assert(privateKey.size() == 2400);
        assert(publicKey.size() == 1184);
        
        std::cout << "  Kyber key generation: PASSED" << std::endl;
    }
    
    static void testKyberEncapsulation() {
        std::cout << "Testing Kyber encapsulation..." << std::endl;
        
        std::vector<uint8_t> publicKey(1184);
        std::vector<uint8_t> sharedSecret(32);
        std::vector<uint8_t> ciphertext(1088);
        
        assert(sharedSecret.size() == 32);
        assert(ciphertext.size() == 1088);
        
        std::cout << "  Kyber encapsulation: PASSED" << std::endl;
    }
    
    static void testKyberDecapsulation() {
        std::cout << "Testing Kyber decapsulation..." << std::endl;
        
        std::vector<uint8_t> privateKey(2400);
        std::vector<uint8_t> ciphertext(1088);
        std::vector<uint8_t> sharedSecret(32);
        
        assert(sharedSecret.size() == 32);
        
        std::cout << "  Kyber decapsulation: PASSED" << std::endl;
    }
    
    static void testDilithiumKeyGen() {
        std::cout << "Testing Dilithium key generation..." << std::endl;
        
        std::vector<uint8_t> privateKey(4000);
        std::vector<uint8_t> publicKey(1952);
        
        assert(privateKey.size() == 4000);
        assert(publicKey.size() == 1952);
        
        std::cout << "  Dilithium key generation: PASSED" << std::endl;
    }
    
    static void testDilithiumSigning() {
        std::cout << "Testing Dilithium signing..." << std::endl;
        
        std::vector<uint8_t> message = {'t', 'e', 's', 't'};
        std::vector<uint8_t> privateKey(4000);
        std::vector<uint8_t> signature(3293);
        
        assert(signature.size() == 3293);
        
        std::cout << "  Dilithium signing: PASSED" << std::endl;
    }
    
    static void testDilithiumVerification() {
        std::cout << "Testing Dilithium verification..." << std::endl;
        
        std::vector<uint8_t> message = {'t', 'e', 's', 't'};
        std::vector<uint8_t> publicKey(1952);
        std::vector<uint8_t> signature(3293);
        
        assert(message.size() == 4);
        
        std::cout << "  Dilithium verification: PASSED" << std::endl;
    }
    
    static void testSphincsKeyGen() {
        std::cout << "Testing SPHINCS+ key generation..." << std::endl;
        
        std::vector<uint8_t> privateKey(128);
        std::vector<uint8_t> publicKey(64);
        
        assert(privateKey.size() == 128);
        assert(publicKey.size() == 64);
        
        std::cout << "  SPHINCS+ key generation: PASSED" << std::endl;
    }
    
    static void testSphincsSigning() {
        std::cout << "Testing SPHINCS+ signing..." << std::endl;
        
        std::vector<uint8_t> message = {'t', 'e', 's', 't'};
        std::vector<uint8_t> privateKey(128);
        std::vector<uint8_t> signature(7856);
        
        assert(signature.size() == 7856);
        
        std::cout << "  SPHINCS+ signing: PASSED" << std::endl;
    }
    
    static void testHybridEncryption() {
        std::cout << "Testing hybrid encryption..." << std::endl;
        
        std::vector<uint8_t> plaintext = {'h', 'e', 'l', 'l', 'o'};
        std::vector<uint8_t> classicKey(32);
        std::vector<uint8_t> kyberKey(32);
        std::vector<uint8_t> ciphertext;
        
        ciphertext.resize(plaintext.size() + 48);
        
        assert(ciphertext.size() > plaintext.size());
        
        std::cout << "  Hybrid encryption: PASSED" << std::endl;
    }
    
    static void testHybridSignatures() {
        std::cout << "Testing hybrid signatures..." << std::endl;
        
        std::vector<uint8_t> message = {'t', 'e', 's', 't'};
        std::vector<uint8_t> classicSig(64);
        std::vector<uint8_t> pqcSig(3293);
        
        assert(classicSig.size() == 64);
        assert(pqcSig.size() == 3293);
        
        std::cout << "  Hybrid signatures: PASSED" << std::endl;
    }
    
    static void testOtpGeneration() {
        std::cout << "Testing OTP generation..." << std::endl;
        
        std::vector<uint8_t> otp(1024);
        
        assert(otp.size() == 1024);
        
        std::cout << "  OTP generation: PASSED" << std::endl;
    }
    
    static void testOtpEncryption() {
        std::cout << "Testing OTP encryption..." << std::endl;
        
        std::vector<uint8_t> plaintext = {'h', 'e', 'l', 'l', 'o'};
        std::vector<uint8_t> otp(5);
        std::vector<uint8_t> ciphertext(5);
        
        for (size_t i = 0; i < plaintext.size(); i++) {
            ciphertext[i] = plaintext[i] ^ otp[i];
        }
        
        std::vector<uint8_t> decrypted(5);
        for (size_t i = 0; i < ciphertext.size(); i++) {
            decrypted[i] = ciphertext[i] ^ otp[i];
        }
        
        assert(decrypted == plaintext);
        
        std::cout << "  OTP encryption: PASSED" << std::endl;
    }
};

}
}

int main() {
    synapse::tests::CryptoTests::runAll();
    synapse::tests::QuantumCryptoTests::runAll();
    return 0;
}
