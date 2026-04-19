#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace synapse {
namespace crypto {

constexpr size_t SHA256_SIZE = 32;
constexpr size_t SHA512_SIZE = 64;
constexpr size_t PRIVATE_KEY_SIZE = 32;
constexpr size_t PUBLIC_KEY_SIZE = 33;
constexpr size_t SIGNATURE_SIZE = 64;
constexpr size_t AES_KEY_SIZE = 32;
constexpr size_t AES_IV_SIZE = 16;

using Hash256 = std::array<uint8_t, SHA256_SIZE>;
using Hash512 = std::array<uint8_t, SHA512_SIZE>;
using PrivateKey = std::array<uint8_t, PRIVATE_KEY_SIZE>;
using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;

struct KeyPair {
    PublicKey publicKey;
    PrivateKey privateKey;
};

Hash256 sha256(const uint8_t* data, size_t len);
Hash256 sha256(const std::vector<uint8_t>& data);
Hash256 sha256(const std::string& data);
Hash256 doubleSha256(const uint8_t* data, size_t len);
std::string sha256Hex(const std::string& data);

Hash256 ripemd160(const uint8_t* data, size_t len);
Hash256 hash160(const uint8_t* data, size_t len);

KeyPair generateKeyPair();
KeyPair keyPairFromSeed(const Hash256& seed);
PublicKey derivePublicKey(const PrivateKey& privateKey);

Signature sign(const Hash256& hash, const PrivateKey& privateKey);
bool verify(const Hash256& hash, const Signature& signature, const PublicKey& publicKey);

std::vector<uint8_t> encryptAES(const std::vector<uint8_t>& data, const std::array<uint8_t, AES_KEY_SIZE>& key);
std::vector<uint8_t> decryptAES(const std::vector<uint8_t>& data, const std::array<uint8_t, AES_KEY_SIZE>& key);

std::vector<uint8_t> randomBytes(size_t count);
void secureZero(void* ptr, size_t len);

std::string toHex(const uint8_t* data, size_t len);
std::string toHex(const std::vector<uint8_t>& data);
template<size_t N>
std::string toHex(const std::array<uint8_t, N>& data) {
    return toHex(data.data(), N);
}
std::vector<uint8_t> fromHex(const std::string& hex);

Hash256 hash160Func(const uint8_t* data, size_t len);
Hash512 sha512(const uint8_t* data, size_t len);

std::array<uint8_t, AES_KEY_SIZE> deriveKey(const std::string& password, const std::vector<uint8_t>& salt);
std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);
std::vector<uint8_t> hmacSha512(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);
bool constantTimeCompare(const uint8_t* a, const uint8_t* b, size_t len);

std::vector<uint8_t> base64Encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64Decode(const std::vector<uint8_t>& data);
std::string base58Encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base58Decode(const std::string& str);

}
}
