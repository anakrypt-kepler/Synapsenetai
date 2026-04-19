#pragma once

#include "crypto.h"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace synapse {
namespace crypto {

enum class KeyType {
    SECP256K1,
    ED25519,
    X25519
};

struct ExtendedKeyPair {
    std::vector<uint8_t> publicKey;
    std::vector<uint8_t> privateKey;
    KeyType type;
};

struct DerivedKey {
    std::vector<uint8_t> key;
    uint32_t index;
    std::string path;
};

class Keys {
public:
    Keys();
    ~Keys();
    
    bool generate(KeyType type = KeyType::SECP256K1);
    bool fromSeed(const std::vector<uint8_t>& seed);
    bool fromMnemonic(const std::string& mnemonic);
    bool fromPrivateKey(const std::vector<uint8_t>& privateKey);
    
    std::string generateMnemonic(uint32_t wordCount = 24);
    std::string toMnemonic() const;
    bool validateMnemonic(const std::string& mnemonic);
    
    std::vector<uint8_t> getPublicKey() const;
    class SecureBuffer {
    public:
        explicit SecureBuffer(std::vector<uint8_t> data);
        ~SecureBuffer();
        const uint8_t* data() const;
        size_t size() const;
        SecureBuffer(const SecureBuffer&) = delete;
        SecureBuffer& operator=(const SecureBuffer&) = delete;
        SecureBuffer(SecureBuffer&&) noexcept;
        SecureBuffer& operator=(SecureBuffer&&) noexcept;
    private:
        std::vector<uint8_t> buf_;
    };
    SecureBuffer getPrivateKey() const;
    std::vector<uint8_t> getSeed() const;
    KeyType getKeyType() const;
    std::string getAddress() const;
    
    DerivedKey deriveChild(uint32_t index);
    DerivedKey derivePath(const std::string& path);
    std::vector<DerivedKey> deriveRange(uint32_t start, uint32_t count);
    
    std::vector<uint8_t> sign(const std::vector<uint8_t>& message) const;
    bool verify(const std::vector<uint8_t>& message, 
                const std::vector<uint8_t>& signature) const;
    std::vector<uint8_t> sharedSecret(const std::vector<uint8_t>& otherPublicKey) const;
    
    bool save(const std::string& path, const std::string& password);
    bool load(const std::string& path, const std::string& password);
    bool exportEncrypted(std::vector<uint8_t>& output, const std::string& password);
    bool importEncrypted(const std::vector<uint8_t>& data, const std::string& password);
    
    void wipe();
    bool isValid() const;
    
    static std::vector<std::string> getWordlist();
    static bool isValidWord(const std::string& word);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
