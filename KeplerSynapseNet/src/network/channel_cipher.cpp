#include "network/channel_cipher.h"
#include "crypto/crypto.h"
#include <cstring>
#include <openssl/evp.h>
#include <mutex>

namespace synapse {
namespace network {

struct ChannelCipher::Impl {
    std::array<uint8_t, KEY_SIZE> sendKey{};
    std::array<uint8_t, KEY_SIZE> recvKey{};
    uint64_t sendNonce = 0;
    uint64_t recvNonce = 0;
    uint32_t messageCount = 0;
    bool ready = false;
    mutable std::mutex mtx;

    std::array<uint8_t, NONCE_SIZE> buildNonce(uint64_t counter) {
        std::array<uint8_t, NONCE_SIZE> nonce{};
        for (int i = 0; i < 8; i++) {
            nonce[4 + i] = static_cast<uint8_t>((counter >> (i * 8)) & 0xFF);
        }
        return nonce;
    }

    std::vector<uint8_t> encryptChaCha20Poly1305(
        const uint8_t* key,
        const uint8_t* nonce,
        const uint8_t* plaintext,
        size_t plaintextLen) {

        std::vector<uint8_t> output(plaintextLen + TAG_SIZE);
        int outLen = 0;
        int finalLen = 0;

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return {};

        bool ok = true;
        ok = ok && (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1);
        ok = ok && (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, NONCE_SIZE, nullptr) == 1);
        ok = ok && (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) == 1);
        ok = ok && (EVP_EncryptUpdate(ctx, output.data(), &outLen, plaintext, static_cast<int>(plaintextLen)) == 1);
        ok = ok && (EVP_EncryptFinal_ex(ctx, output.data() + outLen, &finalLen) == 1);

        if (ok) {
            ok = (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, TAG_SIZE, output.data() + plaintextLen) == 1);
        }

        EVP_CIPHER_CTX_free(ctx);

        if (!ok) return {};
        output.resize(plaintextLen + TAG_SIZE);
        return output;
    }

    std::vector<uint8_t> decryptChaCha20Poly1305(
        const uint8_t* key,
        const uint8_t* nonce,
        const uint8_t* ciphertext,
        size_t ciphertextLen) {

        if (ciphertextLen < TAG_SIZE) return {};

        size_t dataLen = ciphertextLen - TAG_SIZE;
        std::vector<uint8_t> output(dataLen);
        int outLen = 0;
        int finalLen = 0;

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return {};

        bool ok = true;
        ok = ok && (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1);
        ok = ok && (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, NONCE_SIZE, nullptr) == 1);
        ok = ok && (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) == 1);
        ok = ok && (EVP_DecryptUpdate(ctx, output.data(), &outLen, ciphertext, static_cast<int>(dataLen)) == 1);
        ok = ok && (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, TAG_SIZE,
                    const_cast<uint8_t*>(ciphertext + dataLen)) == 1);
        ok = ok && (EVP_DecryptFinal_ex(ctx, output.data() + outLen, &finalLen) == 1);

        EVP_CIPHER_CTX_free(ctx);

        if (!ok) return {};
        output.resize(static_cast<size_t>(outLen + finalLen));
        return output;
    }
};

ChannelCipher::ChannelCipher() : impl_(std::make_unique<Impl>()) {}
ChannelCipher::~ChannelCipher() {
    if (impl_) {
        crypto::secureZero(impl_->sendKey.data(), impl_->sendKey.size());
        crypto::secureZero(impl_->recvKey.data(), impl_->recvKey.size());
    }
}

bool ChannelCipher::init(const std::vector<uint8_t>& sessionKey, bool isInitiator) {
    if (sessionKey.size() < KEY_SIZE) return false;

    std::lock_guard<std::mutex> lock(impl_->mtx);

    std::vector<uint8_t> labelSend(19);
    std::memcpy(labelSend.data(), "channel-send-key-1", 18);
    labelSend[18] = isInitiator ? 0x01 : 0x02;

    std::vector<uint8_t> labelRecv(19);
    std::memcpy(labelRecv.data(), "channel-recv-key-1", 18);
    labelRecv[18] = isInitiator ? 0x02 : 0x01;

    auto sendDerived = crypto::hmacSha256(sessionKey, labelSend);
    auto recvDerived = crypto::hmacSha256(sessionKey, labelRecv);

    if (sendDerived.size() < KEY_SIZE || recvDerived.size() < KEY_SIZE) return false;

    std::memcpy(impl_->sendKey.data(), sendDerived.data(), KEY_SIZE);
    std::memcpy(impl_->recvKey.data(), recvDerived.data(), KEY_SIZE);

    crypto::secureZero(sendDerived.data(), sendDerived.size());
    crypto::secureZero(recvDerived.data(), recvDerived.size());

    impl_->sendNonce = 0;
    impl_->recvNonce = 0;
    impl_->messageCount = 0;
    impl_->ready = true;
    return true;
}

std::vector<uint8_t> ChannelCipher::encrypt(const std::vector<uint8_t>& plaintext) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->ready) return {};

    auto nonce = impl_->buildNonce(impl_->sendNonce);
    auto ct = impl_->encryptChaCha20Poly1305(
        impl_->sendKey.data(), nonce.data(),
        plaintext.data(), plaintext.size());

    if (ct.empty()) return {};

    impl_->sendNonce++;
    impl_->messageCount++;
    return ct;
}

std::vector<uint8_t> ChannelCipher::decrypt(const std::vector<uint8_t>& ciphertext) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->ready) return {};

    auto nonce = impl_->buildNonce(impl_->recvNonce);
    auto pt = impl_->decryptChaCha20Poly1305(
        impl_->recvKey.data(), nonce.data(),
        ciphertext.data(), ciphertext.size());

    if (pt.empty()) return {};

    impl_->recvNonce++;
    return pt;
}

bool ChannelCipher::isReady() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->ready;
}

void ChannelCipher::rotateKeys() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->ready) return;

    std::vector<uint8_t> oldSend(impl_->sendKey.begin(), impl_->sendKey.end());
    std::vector<uint8_t> oldRecv(impl_->recvKey.begin(), impl_->recvKey.end());

    std::vector<uint8_t> rotateLabel(10);
    std::memcpy(rotateLabel.data(), "rotate-key", 10);

    auto newSend = crypto::hmacSha256(oldSend, rotateLabel);
    auto newRecv = crypto::hmacSha256(oldRecv, rotateLabel);

    std::memcpy(impl_->sendKey.data(), newSend.data(), KEY_SIZE);
    std::memcpy(impl_->recvKey.data(), newRecv.data(), KEY_SIZE);

    crypto::secureZero(oldSend.data(), oldSend.size());
    crypto::secureZero(oldRecv.data(), oldRecv.size());
    crypto::secureZero(newSend.data(), newSend.size());
    crypto::secureZero(newRecv.data(), newRecv.size());

    impl_->sendNonce = 0;
    impl_->recvNonce = 0;
}

}
}
