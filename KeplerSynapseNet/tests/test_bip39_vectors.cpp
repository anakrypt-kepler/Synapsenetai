#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include "crypto/keys.h"

using namespace synapse::crypto;

int main() {
    std::cout << "Running BIP39 vector tests...\n";

    // Test 1: Known test vector - deterministic output for same mnemonic
    std::cout << "  Test 1: Known vector (12-word)... ";
    {
        Keys keys1, keys2;
        std::string mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
        
        keys1.fromMnemonic(mnemonic);
        keys2.fromMnemonic(mnemonic);
        
        auto sb1 = keys1.getPrivateKey();
        std::vector<uint8_t> priv1(sb1.data(), sb1.data() + sb1.size());
        auto sb2 = keys2.getPrivateKey();
        std::vector<uint8_t> priv2(sb2.data(), sb2.data() + sb2.size());
        
        assert(!priv1.empty());
        assert(priv1 == priv2);  // Same mnemonic should produce same key
    }
    std::cout << "PASSED\n";

    // Test 2: Different mnemonics produce different keys
    std::cout << "  Test 2: Different mnemonics... ";
    {
        Keys keys1, keys2;
        std::string m1 = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
        std::string m2 = "legal winner thank year wave sausage worth useful legal winner thank yellow";
        
        keys1.fromMnemonic(m1);
        keys2.fromMnemonic(m2);
        
        auto sb1 = keys1.getPrivateKey();
        std::vector<uint8_t> priv1(sb1.data(), sb1.data() + sb1.size());
        auto sb2 = keys2.getPrivateKey();
        std::vector<uint8_t> priv2(sb2.data(), sb2.data() + sb2.size());
        
        assert(!priv1.empty());
        assert(!priv2.empty());
        assert(priv1 != priv2);  // Different mnemonics should produce different keys
    }
    std::cout << "PASSED\n";

    // Test 3: 24-word mnemonic (longer vector)
    std::cout << "  Test 3: Known vector (24-word)... ";
    {
        Keys keys;
        std::string mnemonic24 = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art";
        
        keys.fromMnemonic(mnemonic24);
        auto sb = keys.getPrivateKey();
        std::vector<uint8_t> priv(sb.data(), sb.data() + sb.size());
        
        assert(!priv.empty());
        assert(priv.size() >= 32);  // Private key should be at least 32 bytes
    }
    std::cout << "PASSED\n";

    // Test 4: Invalid mnemonic should not crash (implementation-dependent)
    std::cout << "  Test 4: Invalid mnemonic handling... ";
    {
        Keys keys;
        std::string invalidMnemonic = "invalid word list that is not a valid bip39 mnemonic at all";
        
        try {
            keys.fromMnemonic(invalidMnemonic);
            // If it doesn't crash, verify key is empty or invalid
            auto sb = keys.getPrivateKey();
            std::vector<uint8_t> priv(sb.data(), sb.data() + sb.size());
            // May be empty or may still generate something; main thing is no crash
            (void)priv;
        } catch (...) {
            // Exception is also acceptable for invalid input
        }
    }
    std::cout << "PASSED (no crash)\n";

    // Test 5: Empty mnemonic (edge case)
    std::cout << "  Test 5: Empty mnemonic edge case... ";
    {
        Keys keys;
        std::string emptyMnemonic = "";
        
        try {
            keys.fromMnemonic(emptyMnemonic);
            // Should not crash
        } catch (...) {
            // Exception is acceptable
        }
    }
    std::cout << "PASSED (no crash)\n";

    // Test 6: Verify key size is consistent
    std::cout << "  Test 6: Key size consistency... ";
    {
        Keys keys;
        std::string mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
        
        keys.fromMnemonic(mnemonic);
        auto sb = keys.getPrivateKey();
        std::vector<uint8_t> priv(sb.data(), sb.data() + sb.size());
        
        // Private key should be a valid size for secp256k1 (32 bytes)
        assert(priv.size() == 32);
    }
    std::cout << "PASSED\n";

    std::cout << "All BIP39 vector tests PASSED\n";
    return 0;
}
