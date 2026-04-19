#include "privacy/privacy.h"
#include "crypto/crypto.h"
#include <mutex>
#include <map>
#include <cstring>
#include <algorithm>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

namespace synapse {
namespace privacy {

struct SessionCrypto::Impl {
    mutable std::mutex mtx;
    std::map<std::string, std::vector<uint8_t>> sessionKeys;

    Impl() {}

    void fillRandom(uint8_t* buf, size_t len) {
        if (RAND_bytes(buf, static_cast<int>(len)) != 1) {
            // Fallback should not happen in practice; abort if CSPRNG fails
            std::abort();
        }
    }
};

SessionCrypto::SessionCrypto() : impl_(std::make_unique<Impl>()) {}
SessionCrypto::~SessionCrypto() { clearAllSessions(); }

bool SessionCrypto::createSession(const std::string& sessionId,
                                   const std::vector<uint8_t>& sharedSecret) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    auto hash = crypto::sha256(sharedSecret.data(), sharedSecret.size());
    impl_->sessionKeys[sessionId] = std::vector<uint8_t>(hash.begin(), hash.end());

    return true;
}

bool SessionCrypto::destroySession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    auto it = impl_->sessionKeys.find(sessionId);
    if (it != impl_->sessionKeys.end()) {
        OPENSSL_cleanse(it->second.data(), it->second.size());
        impl_->sessionKeys.erase(it);
        return true;
    }
    return false;
}

void SessionCrypto::clearAllSessions() {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    for (auto& [id, key] : impl_->sessionKeys) {
        OPENSSL_cleanse(key.data(), key.size());
    }
    impl_->sessionKeys.clear();
}

std::vector<uint8_t> SessionCrypto::encrypt(const std::string& sessionId,
                                             const std::vector<uint8_t>& plaintext) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    auto keyIt = impl_->sessionKeys.find(sessionId);
    if (keyIt == impl_->sessionKeys.end()) return {};

    // Generate 12-byte IV using CSPRNG
    std::vector<uint8_t> iv(12);
    impl_->fillRandom(iv.data(), iv.size());

    // AES-256-GCM encryption
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, keyIt->second.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    std::vector<uint8_t> ciphertext(plaintext.size());
    int outlen = 0;
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &outlen, plaintext.data(),
                          static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int tmplen = 0;
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + outlen, &tmplen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    outlen += tmplen;
    ciphertext.resize(outlen);

    // Get GCM tag (16 bytes)
    std::vector<uint8_t> tag(16);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);

    // Output format: [12-byte IV][16-byte GCM tag][ciphertext]
    std::vector<uint8_t> out;
    out.reserve(iv.size() + tag.size() + ciphertext.size());
    out.insert(out.end(), iv.begin(), iv.end());
    out.insert(out.end(), tag.begin(), tag.end());
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    return out;
}

std::vector<uint8_t> SessionCrypto::decrypt(const std::string& sessionId,
                                             const std::vector<uint8_t>& ciphertext) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    auto keyIt = impl_->sessionKeys.find(sessionId);
    if (keyIt == impl_->sessionKeys.end() || ciphertext.size() < 12 + 16) return {};

    // Extract IV, tag, and encrypted data
    std::vector<uint8_t> iv(ciphertext.begin(), ciphertext.begin() + 12);
    std::vector<uint8_t> tag(ciphertext.begin() + 12, ciphertext.begin() + 28);
    std::vector<uint8_t> enc(ciphertext.begin() + 28, ciphertext.end());

    // AES-256-GCM decryption
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, keyIt->second.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    std::vector<uint8_t> plaintext(enc.size());
    int outlen = 0;
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &outlen, enc.data(),
                          static_cast<int>(enc.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // Set expected GCM tag for verification
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int tmplen = 0;
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + outlen, &tmplen) != 1) {
        // Authentication failed
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    outlen += tmplen;
    plaintext.resize(outlen);

    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

bool SessionCrypto::hasSession(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->sessionKeys.find(sessionId) != impl_->sessionKeys.end();
}

bool SessionCrypto::rotateKey(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    auto it = impl_->sessionKeys.find(sessionId);
    if (it == impl_->sessionKeys.end()) return false;

    auto hash = crypto::sha256(it->second.data(), it->second.size());
    OPENSSL_cleanse(it->second.data(), it->second.size());
    it->second = std::vector<uint8_t>(hash.begin(), hash.end());

    return true;
}

}
}
