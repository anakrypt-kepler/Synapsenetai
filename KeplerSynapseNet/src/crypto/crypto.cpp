#include "crypto/crypto.h"
#include <cstring>
#include <random>
#include <algorithm>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

#ifdef SYNAPSE_USE_SECP256K1
#include <secp256k1.h>
#include <mutex>
#else
#error "SYNAPSE_USE_SECP256K1 is required for key derivation/signature operations"
#endif

namespace synapse {
namespace crypto {

static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static inline uint32_t sig0(uint32_t x) { return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22); }
static inline uint32_t sig1(uint32_t x) { return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25); }
static inline uint32_t ep0(uint32_t x) { return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3); }
static inline uint32_t ep1(uint32_t x) { return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10); }

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256Transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (block[i*4] << 24) | (block[i*4+1] << 16) | (block[i*4+2] << 8) | block[i*4+3];
    }
    for (int i = 16; i < 64; i++) {
        w[i] = ep1(w[i-2]) + w[i-7] + ep0(w[i-15]) + w[i-16];
    }
    
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + sig1(e) + ch(e, f, g) + K256[i] + w[i];
        uint32_t t2 = sig0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

Hash256 sha256(const uint8_t* data, size_t len) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    
    uint8_t block[64];
    size_t i = 0;
    
    while (i + 64 <= len) {
        sha256Transform(state, data + i);
        i += 64;
    }
    
    size_t rem = len - i;
    std::memcpy(block, data + i, rem);
    block[rem++] = 0x80;
    
    if (rem > 56) {
        std::memset(block + rem, 0, 64 - rem);
        sha256Transform(state, block);
        rem = 0;
    }
    
    std::memset(block + rem, 0, 56 - rem);
    uint64_t bits = len * 8;
    for (int j = 0; j < 8; j++) {
        block[56 + j] = (bits >> (56 - j * 8)) & 0xff;
    }
    sha256Transform(state, block);
    
    Hash256 hash;
    for (int j = 0; j < 8; j++) {
        hash[j*4] = (state[j] >> 24) & 0xff;
        hash[j*4+1] = (state[j] >> 16) & 0xff;
        hash[j*4+2] = (state[j] >> 8) & 0xff;
        hash[j*4+3] = state[j] & 0xff;
    }
    return hash;
}

Hash256 sha256(const std::vector<uint8_t>& data) {
    return sha256(data.data(), data.size());
}

Hash256 sha256(const std::string& data) {
    return sha256(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

Hash256 doubleSha256(const uint8_t* data, size_t len) {
    Hash256 first = sha256(data, len);
    return sha256(first.data(), first.size());
}

std::string sha256Hex(const std::string& data) {
    Hash256 hash = sha256(data);
    return toHex(hash.data(), hash.size());
}

Hash256 ripemd160(const uint8_t* data, size_t len) {
    Hash256 hash{};
    // Use OpenSSL EVP API to compute RIPEMD-160 digest (20 bytes)
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return hash;
    if (1 != EVP_DigestInit_ex(ctx, EVP_ripemd160(), NULL)) {
        EVP_MD_CTX_free(ctx);
        return hash;
    }
    if (len > 0 && data != nullptr) {
        if (1 != EVP_DigestUpdate(ctx, data, len)) {
            EVP_MD_CTX_free(ctx);
            return hash;
        }
    }
    if (1 != EVP_DigestFinal_ex(ctx, digest, &digestLen)) {
        EVP_MD_CTX_free(ctx);
        return hash;
    }
    EVP_MD_CTX_free(ctx);

    // RIPEMD-160 produces 20 bytes
    if (digestLen > 20) digestLen = 20;
    std::memcpy(hash.data(), digest, digestLen);
    // remaining bytes in Hash256 are already zero-initialized
    return hash;
}

Hash256 hash160(const uint8_t* data, size_t len) {
    Hash256 sha = sha256(data, len);
    return ripemd160(sha.data(), 32);
}

Hash256 hash160Func(const uint8_t* data, size_t len) {
    return hash160(data, len);
}

Hash512 sha512(const uint8_t* data, size_t len) {
    Hash512 out{};
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return out;
    if (1 != EVP_DigestInit_ex(ctx, EVP_sha512(), NULL)) {
        EVP_MD_CTX_free(ctx);
        return out;
    }
    if (len > 0 && data != nullptr) {
        if (1 != EVP_DigestUpdate(ctx, data, len)) {
            EVP_MD_CTX_free(ctx);
            return out;
        }
    }
    if (1 != EVP_DigestFinal_ex(ctx, digest, &digestLen)) {
        EVP_MD_CTX_free(ctx);
        return out;
    }
    EVP_MD_CTX_free(ctx);
    if (digestLen > SHA512_SIZE) digestLen = SHA512_SIZE;
    std::memcpy(out.data(), digest, digestLen);
    return out;
}

KeyPair generateKeyPair() {
    KeyPair kp;
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    static secp256k1_context* ctx = [] {
        return secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }();

    // Use OpenSSL RAND_bytes for cryptographically secure private key material
    for (;;) {
        if (1 != RAND_bytes(kp.privateKey.data(), PRIVATE_KEY_SIZE)) {
            kp.privateKey.fill(0);
            break;
        }
        if (secp256k1_ec_seckey_verify(ctx, kp.privateKey.data())) break;
    }

    kp.publicKey = derivePublicKey(kp.privateKey);
    return kp;
}

KeyPair keyPairFromSeed(const Hash256& seed) {
    KeyPair kp;
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    static secp256k1_context* ctx = [] {
        return secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }();

    Hash256 cur = seed;
    for (int i = 0; i < 1000; ++i) {
        std::memcpy(kp.privateKey.data(), cur.data(), PRIVATE_KEY_SIZE);
        if (secp256k1_ec_seckey_verify(ctx, kp.privateKey.data())) break;
        cur = sha256(cur.data(), cur.size());
    }
    kp.publicKey = derivePublicKey(kp.privateKey);
    return kp;
}

PublicKey derivePublicKey(const PrivateKey& privateKey) {
    static secp256k1_context* ctx = [] {
        return secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }();
    PublicKey out{};
    secp256k1_pubkey pub{};
    if (!secp256k1_ec_pubkey_create(ctx, &pub, privateKey.data())) {
        out.fill(0);
        return out;
    }
    size_t outLen = out.size();
    if (!secp256k1_ec_pubkey_serialize(ctx, out.data(), &outLen, &pub, SECP256K1_EC_COMPRESSED) || outLen != out.size()) {
        out.fill(0);
        return out;
    }
    return out;
}

Signature sign(const Hash256& hash, const PrivateKey& privateKey) {
    static secp256k1_context* ctx = [] {
        return secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }();
    Signature out{};
    if (!secp256k1_ec_seckey_verify(ctx, privateKey.data())) {
        out.fill(0);
        return out;
    }
    secp256k1_ecdsa_signature sig{};
    if (!secp256k1_ecdsa_sign(ctx, &sig, hash.data(), privateKey.data(), secp256k1_nonce_function_rfc6979, nullptr)) {
        out.fill(0);
        return out;
    }
    secp256k1_ecdsa_signature_normalize(ctx, &sig, &sig);
    if (!secp256k1_ecdsa_signature_serialize_compact(ctx, out.data(), &sig)) {
        out.fill(0);
        return out;
    }
    return out;
}

bool verify(const Hash256& hash, const Signature& signature, const PublicKey& publicKey) {
    static secp256k1_context* ctx = [] {
        return secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }();
    secp256k1_pubkey pub{};
    if (!secp256k1_ec_pubkey_parse(ctx, &pub, publicKey.data(), publicKey.size())) return false;
    secp256k1_ecdsa_signature sig{};
    if (!secp256k1_ecdsa_signature_parse_compact(ctx, &sig, signature.data())) return false;
    secp256k1_ecdsa_signature_normalize(ctx, &sig, &sig);
    return secp256k1_ecdsa_verify(ctx, &sig, hash.data(), &pub) == 1;
}

std::vector<uint8_t> encryptAES(const std::vector<uint8_t>& data, const std::array<uint8_t, AES_KEY_SIZE>& key) {
    // AES-256-GCM authenticated encryption
    const size_t GCM_IV_SIZE = 12;
    const size_t TAG_SIZE = 16;

    std::vector<uint8_t> iv(GCM_IV_SIZE);
    if (1 != RAND_bytes(iv.data(), (int)GCM_IV_SIZE)) return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    int len = 0;
    std::vector<uint8_t> ciphertext(data.size());

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key.data(), iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &len, data.data(), (int)data.size())) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    int ciphertext_len = len;

    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertext_len += len;
    ciphertext.resize(ciphertext_len);

    std::vector<uint8_t> tag(TAG_SIZE);
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, (int)TAG_SIZE, tag.data())) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);

    std::vector<uint8_t> result;
    result.reserve(iv.size() + ciphertext.size() + tag.size());
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());
    result.insert(result.end(), tag.begin(), tag.end());
    return result;
}

std::vector<uint8_t> decryptAES(const std::vector<uint8_t>& data, const std::array<uint8_t, AES_KEY_SIZE>& key) {
    const size_t GCM_IV_SIZE = 12;
    const size_t TAG_SIZE = 16;
    if (data.size() < GCM_IV_SIZE + TAG_SIZE) return {};

    const uint8_t* iv = data.data();
    const uint8_t* tag = data.data() + data.size() - TAG_SIZE;
    const uint8_t* ciphertext = data.data() + GCM_IV_SIZE;
    size_t ciphertext_len = data.size() - GCM_IV_SIZE - TAG_SIZE;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (1 != EVP_DecryptInit_ex(ctx, NULL, NULL, key.data(), iv)) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    std::vector<uint8_t> plaintext(ciphertext_len);
    int len = 0;
    if (ciphertext_len > 0) {
        if (1 != EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext, (int)ciphertext_len)) {
            EVP_CIPHER_CTX_free(ctx);
            return {};
        }
    }
    int plaintext_len = len;

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, (int)TAG_SIZE, (void*)tag)) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);
    if (ret <= 0) {
        // authentication failed
        return {};
    }
    plaintext_len += len;
    plaintext.resize(plaintext_len);
    return plaintext;
}

std::vector<uint8_t> randomBytes(size_t count) {
    std::vector<uint8_t> bytes(count);
    if (count == 0) return bytes;
    if (1 != RAND_bytes(bytes.data(), (int)count)) return {};
    return bytes;
}

void secureZero(void* ptr, size_t len) {
    OPENSSL_cleanse(ptr, len);
}

std::string toHex(const uint8_t* data, size_t len) {
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        result += hex[data[i] >> 4];
        result += hex[data[i] & 0x0f];
    }
    return result;
}

std::string toHex(const std::vector<uint8_t>& data) {
    return toHex(data.data(), data.size());
}

std::vector<uint8_t> fromHex(const std::string& hex) {
    if (hex.size() % 2 != 0) return {};

    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = nibble(hex[i]);
        int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        result.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return result;
}

std::array<uint8_t, AES_KEY_SIZE> deriveKey(const std::string& password, const std::vector<uint8_t>& salt) {
    std::array<uint8_t, AES_KEY_SIZE> key{};
    // Use PBKDF2-HMAC-SHA256 with at least 100,000 iterations per spec
    const int iterations = 100000;
    if (salt.empty()) return key;
    if (1 != PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                               salt.data(), static_cast<int>(salt.size()),
                               iterations, EVP_sha256(), AES_KEY_SIZE, key.data())) {
        // on failure return zeroed key
        key.fill(0);
    }
    return key;
}

std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ipad(64, 0x36);
    std::vector<uint8_t> opad(64, 0x5c);
    
    std::vector<uint8_t> keyPad = key;
    if (keyPad.size() > 64) {
        Hash256 h = sha256(keyPad.data(), keyPad.size());
        keyPad.assign(h.begin(), h.end());
    }
    keyPad.resize(64, 0);
    
    for (size_t i = 0; i < 64; i++) {
        ipad[i] ^= keyPad[i];
        opad[i] ^= keyPad[i];
    }
    
    std::vector<uint8_t> inner = ipad;
    inner.insert(inner.end(), data.begin(), data.end());
    Hash256 innerHash = sha256(inner.data(), inner.size());
    
    std::vector<uint8_t> outer = opad;
    outer.insert(outer.end(), innerHash.begin(), innerHash.end());
    Hash256 result = sha256(outer.data(), outer.size());
    
    return std::vector<uint8_t>(result.begin(), result.end());
}

std::vector<uint8_t> hmacSha512(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(SHA512_SIZE);
    unsigned int outLen = 0;
    unsigned char* res = HMAC(EVP_sha512(), key.empty() ? nullptr : key.data(), static_cast<int>(key.size()),
                              data.empty() ? nullptr : data.data(), static_cast<int>(data.size()), out.data(), &outLen);
    if (!res || outLen == 0) return {};
    out.resize(outLen);
    return out;
}

bool constantTimeCompare(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result |= a[i] ^ b[i];
    }
    return result == 0;
}

std::vector<uint8_t> base64Encode(const std::vector<uint8_t>& data) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> result;
    result.reserve((data.size() + 2) / 3 * 4);
    
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = data[i] << 16;
        if (i + 1 < data.size()) n |= data[i + 1] << 8;
        if (i + 2 < data.size()) n |= data[i + 2];
        
        result.push_back(table[(n >> 18) & 0x3f]);
        result.push_back(table[(n >> 12) & 0x3f]);
        result.push_back(i + 1 < data.size() ? table[(n >> 6) & 0x3f] : '=');
        result.push_back(i + 2 < data.size() ? table[n & 0x3f] : '=');
    }
    return result;
}

std::vector<uint8_t> base64Decode(const std::vector<uint8_t>& data) {
    static const int table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    
    std::vector<uint8_t> result;
    result.reserve(data.size() * 3 / 4);
    
    uint32_t n = 0;
    int bits = 0;
    for (uint8_t c : data) {
        if (c == '=') break;
        int val = table[c];
        if (val < 0) continue;
        n = (n << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back((n >> bits) & 0xff);
        }
    }
    return result;
}

std::string base58Encode(const std::vector<uint8_t>& data) {
    static const char alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    
    std::vector<uint8_t> digits(data.size() * 138 / 100 + 1, 0);
    size_t digitsLen = 1;
    
    for (size_t i = 0; i < data.size(); i++) {
        uint32_t carry = data[i];
        for (size_t j = 0; j < digitsLen; j++) {
            carry += static_cast<uint32_t>(digits[j]) << 8;
            digits[j] = carry % 58;
            carry /= 58;
        }
        while (carry > 0) {
            digits[digitsLen++] = carry % 58;
            carry /= 58;
        }
    }
    
    std::string result;
    for (size_t i = 0; i < data.size() && data[i] == 0; i++) {
        result += alphabet[0];
    }
    for (size_t i = digitsLen; i-- > 0; ) {
        result += alphabet[digits[i]];
    }
    return result;
}

std::vector<uint8_t> base58Decode(const std::string& str) {
    static const int8_t table[] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8,-1,-1,-1,-1,-1,-1,
        -1, 9,10,11,12,13,14,15,16,-1,17,18,19,20,21,-1,
        22,23,24,25,26,27,28,29,30,31,32,-1,-1,-1,-1,-1,
        -1,33,34,35,36,37,38,39,40,41,42,43,-1,44,45,46,
        47,48,49,50,51,52,53,54,55,56,57,-1,-1,-1,-1,-1
    };
    
    std::vector<uint8_t> bytes(str.size() * 733 / 1000 + 1, 0);
    size_t bytesLen = 1;
    
    for (char c : str) {
        uint8_t uc = static_cast<uint8_t>(c);
        int8_t val = table[uc];
        if (val < 0) return {};
        
        uint32_t carry = val;
        for (size_t i = 0; i < bytesLen; i++) {
            carry += bytes[i] * 58;
            bytes[i] = carry & 0xff;
            carry >>= 8;
        }
        while (carry > 0) {
            bytes[bytesLen++] = carry & 0xff;
            carry >>= 8;
        }
    }
    
    std::vector<uint8_t> result;
    for (char c : str) {
        if (c != '1') break;
        result.push_back(0);
    }
    for (size_t i = bytesLen; i-- > 0; ) {
        result.push_back(bytes[i]);
    }
    return result;
}

}
}
