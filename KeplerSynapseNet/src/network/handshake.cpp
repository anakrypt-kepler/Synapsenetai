#include "network/network.h"
#include "crypto/crypto.h"
#include "crypto/keys.h"
#include <cstring>
#include <mutex>
#include <random>
#include <openssl/rand.h>

namespace synapse {
namespace network {

static const uint32_t HANDSHAKE_MAGIC = 0x534E4554;
static constexpr size_t HANDSHAKE_NONCE_SIZE = 32;

static std::vector<uint8_t> deriveHandshakeSessionKey(const std::vector<uint8_t>& sharedSecret,
                                                      const std::vector<uint8_t>& localPublicKey,
                                                      const std::vector<uint8_t>& remotePublicKey,
                                                      const std::vector<uint8_t>& localNonce,
                                                      const std::vector<uint8_t>& remoteNonce) {
    if (sharedSecret.size() != crypto::SHA256_SIZE) return {};
    if (localPublicKey.empty() || remotePublicKey.empty()) return {};
    if (localNonce.size() != HANDSHAKE_NONCE_SIZE || remoteNonce.size() != HANDSHAKE_NONCE_SIZE) return {};

    const bool localNonceFirst = localNonce < remoteNonce;
    const auto& firstNonce = localNonceFirst ? localNonce : remoteNonce;
    const auto& secondNonce = localNonceFirst ? remoteNonce : localNonce;

    std::vector<uint8_t> salt;
    salt.reserve(firstNonce.size() + secondNonce.size());
    salt.insert(salt.end(), firstNonce.begin(), firstNonce.end());
    salt.insert(salt.end(), secondNonce.begin(), secondNonce.end());

    const bool localPubFirst = localPublicKey < remotePublicKey;
    const auto& firstPub = localPubFirst ? localPublicKey : remotePublicKey;
    const auto& secondPub = localPubFirst ? remotePublicKey : localPublicKey;

    std::vector<uint8_t> info;
    static constexpr char kLabel[] = "synapse-handshake-session-key-v1";
    info.insert(info.end(), kLabel, kLabel + sizeof(kLabel) - 1);
    info.insert(info.end(), firstPub.begin(), firstPub.end());
    info.insert(info.end(), secondPub.begin(), secondPub.end());
    info.push_back(0x01);

    auto prk = crypto::hmacSha256(salt, sharedSecret);
    return crypto::hmacSha256(prk, info);
}

struct HandshakeMessage {
    uint32_t magic;
    uint32_t version;
    uint32_t capabilities;
    std::vector<uint8_t> nodeId;
    std::vector<uint8_t> publicKey;
    std::vector<uint8_t> nonce;
    std::vector<uint8_t> signature;
};

struct Handshake::Impl {
    std::vector<uint8_t> nodeId;
    std::vector<uint8_t> publicKey;
    std::vector<uint8_t> privateKey;
    std::vector<uint8_t> sessionKey;
    std::vector<uint8_t> localNonce;
    std::vector<uint8_t> remoteNonce;
    uint32_t capabilities = 0;
    bool complete = false;
    mutable std::mutex mtx;
    
    std::vector<uint8_t> sign(const std::vector<uint8_t>& data);
    bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature,
                const std::vector<uint8_t>& pubKey);
    std::vector<uint8_t> deriveSessionKey(const std::vector<uint8_t>& remotePublicKey,
                                           const std::vector<uint8_t>& localNonce,
                                           const std::vector<uint8_t>& remoteNonce);
};

std::vector<uint8_t> Handshake::Impl::sign(const std::vector<uint8_t>& data) {
    if (privateKey.size() < 32) return {};
    
    auto hash = crypto::sha256(data.data(), data.size());
    crypto::Hash256 h;
    std::memcpy(h.data(), hash.data(), std::min(hash.size(), h.size()));
    
    crypto::PrivateKey priv;
    std::memcpy(priv.data(), privateKey.data(), std::min(privateKey.size(), priv.size()));
    
    auto sig = crypto::sign(h, priv);
    return std::vector<uint8_t>(sig.begin(), sig.end());
}

bool Handshake::Impl::verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature,
                              const std::vector<uint8_t>& pubKey) {
    if (signature.size() != crypto::SIGNATURE_SIZE || pubKey.size() != crypto::PUBLIC_KEY_SIZE) return false;
    
    auto hash = crypto::sha256(data.data(), data.size());
    crypto::Hash256 h;
    std::memcpy(h.data(), hash.data(), std::min(hash.size(), h.size()));
    
    crypto::Signature sig;
    std::memcpy(sig.data(), signature.data(), sig.size());
    
    crypto::PublicKey pub;
    std::memcpy(pub.data(), pubKey.data(), pub.size());
    
    return crypto::verify(h, sig, pub);
}

std::vector<uint8_t> Handshake::Impl::deriveSessionKey(const std::vector<uint8_t>& remotePublicKey,
                                                        const std::vector<uint8_t>& localNonce,
                                                        const std::vector<uint8_t>& remoteNonce) {
    crypto::Keys localKeys;
    if (!localKeys.fromPrivateKey(privateKey)) {
        return {};
    }

    auto sharedSecret = localKeys.sharedSecret(remotePublicKey);
    if (sharedSecret.size() != crypto::SHA256_SIZE) {
        if (!sharedSecret.empty()) {
            crypto::secureZero(sharedSecret.data(), sharedSecret.size());
        }
        return {};
    }

    const auto localPublicKey = localKeys.getPublicKey();
    auto sessionKey = deriveHandshakeSessionKey(
        sharedSecret, localPublicKey, remotePublicKey, localNonce, remoteNonce);
    crypto::secureZero(sharedSecret.data(), sharedSecret.size());
    return sessionKey;
}

Handshake::Handshake() : impl_(std::make_unique<Impl>()) {}
Handshake::~Handshake() = default;

bool Handshake::init(const std::vector<uint8_t>& nodeId, const std::vector<uint8_t>& publicKey,
                      const std::vector<uint8_t>& privateKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->nodeId = nodeId;
    impl_->publicKey = publicKey;
    impl_->privateKey = privateKey;
    impl_->complete = false;
    
    impl_->localNonce.resize(HANDSHAKE_NONCE_SIZE);
    if (RAND_bytes(impl_->localNonce.data(), static_cast<int>(impl_->localNonce.size())) != 1) {
        std::random_device rd;
        for (size_t i = 0; i < impl_->localNonce.size(); i++) impl_->localNonce[i] = static_cast<uint8_t>(rd() & 0xFF);
    }
    
    return true;
}

void Handshake::setCapabilities(uint32_t caps) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->capabilities = caps;
}

std::vector<uint8_t> Handshake::createInitMessage() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> msg;
    
    auto writeU32 = [&msg](uint32_t val) {
        for (int i = 0; i < 4; i++) msg.push_back((val >> (i * 8)) & 0xff);
    };
    
    writeU32(HANDSHAKE_MAGIC);
    writeU32(PROTOCOL_VERSION);
    writeU32(impl_->capabilities);
    
    writeU32(impl_->nodeId.size());
    msg.insert(msg.end(), impl_->nodeId.begin(), impl_->nodeId.end());
    
    writeU32(impl_->publicKey.size());
    msg.insert(msg.end(), impl_->publicKey.begin(), impl_->publicKey.end());
    
    writeU32(impl_->localNonce.size());
    msg.insert(msg.end(), impl_->localNonce.begin(), impl_->localNonce.end());
    
    auto sig = impl_->sign(msg);
    writeU32(sig.size());
    msg.insert(msg.end(), sig.begin(), sig.end());
    
    return msg;
}

HandshakeResult Handshake::processInitMessage(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    HandshakeResult result{};
    result.success = false;
    
    if (data.size() < 16) {
        result.error = "Message too short";
        return result;
    }
    
    auto readU32 = [&data](size_t& pos) -> uint32_t {
        if (pos + 4 > data.size()) return 0;
        uint32_t val = 0;
        for (int i = 0; i < 4; i++) val |= static_cast<uint32_t>(data[pos + i]) << (i * 8);
        pos += 4;
        return val;
    };
    
    size_t pos = 0;
    uint32_t magic = readU32(pos);
    if (magic != HANDSHAKE_MAGIC) {
        result.error = "Invalid magic";
        return result;
    }
    
    result.remoteVersion = readU32(pos);
    result.remoteCapabilities = readU32(pos);
    
    uint32_t nodeIdLen = readU32(pos);
    if (pos + nodeIdLen > data.size()) {
        result.error = "Invalid node ID length";
        return result;
    }
    result.remoteNodeId.assign(data.begin() + pos, data.begin() + pos + nodeIdLen);
    pos += nodeIdLen;
    
    uint32_t pubKeyLen = readU32(pos);
    if (pos + pubKeyLen > data.size()) {
        result.error = "Invalid public key length";
        return result;
    }
    result.remotePublicKey.assign(data.begin() + pos, data.begin() + pos + pubKeyLen);
    pos += pubKeyLen;
    
    uint32_t nonceLen = readU32(pos);
    if (pos + nonceLen > data.size()) {
        result.error = "Invalid nonce length";
        return result;
    }
    impl_->remoteNonce.assign(data.begin() + pos, data.begin() + pos + nonceLen);
    pos += nonceLen;

    size_t signedLen = pos;
    uint32_t sigLen = readU32(pos);
    if (sigLen == 0 || pos + sigLen > data.size()) {
        result.error = "Invalid signature length";
        return result;
    }
    std::vector<uint8_t> signature(data.begin() + pos, data.begin() + pos + sigLen);
    pos += sigLen;
    
    std::vector<uint8_t> signedData(data.begin(), data.begin() + signedLen);
    if (!impl_->verify(signedData, signature, result.remotePublicKey)) {
        result.error = "Invalid signature";
        return result;
    }
    
    result.sessionKey = impl_->deriveSessionKey(result.remotePublicKey, impl_->localNonce, impl_->remoteNonce);
    if (result.sessionKey.empty()) {
        result.error = "Failed to derive session key";
        return result;
    }
    impl_->sessionKey = result.sessionKey;
    
    result.success = true;
    return result;
}

std::vector<uint8_t> Handshake::createResponseMessage(const HandshakeResult& initResult) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto msg = createInitMessage();
    // compute ack hash = sha256(initResult.remoteNodeId || impl_->remoteNonce)
    std::vector<uint8_t> ackInput = initResult.remoteNodeId;
    ackInput.insert(ackInput.end(), impl_->remoteNonce.begin(), impl_->remoteNonce.end());
    auto ack = crypto::sha256(ackInput.data(), ackInput.size());
    // append length and bytes
    uint32_t len = static_cast<uint32_t>(ack.size());
    for (int i = 0; i < 4; i++) msg.push_back((len >> (i * 8)) & 0xFF);
    msg.insert(msg.end(), ack.begin(), ack.end());
    return msg;
}

HandshakeResult Handshake::processResponseMessage(const std::vector<uint8_t>& data) {
    auto result = processInitMessage(data);
    if (!result.success) return result;

    // parse optional ack field appended after the signature
    size_t pos = 0;
    auto readU32 = [&data](size_t& pos) -> uint32_t {
        if (pos + 4 > data.size()) return 0;
        uint32_t val = 0;
        for (int i = 0; i < 4; i++) val |= static_cast<uint32_t>(data[pos + i]) << (i * 8);
        pos += 4;
        return val;
    };

    uint32_t magic = readU32(pos);
    if (magic != HANDSHAKE_MAGIC) return result;
    // skip version, capabilities
    pos += 4 + 4;
    uint32_t nodeIdLen = readU32(pos);
    pos += nodeIdLen;
    uint32_t pubKeyLen = readU32(pos);
    pos += pubKeyLen;
    uint32_t nonceLen = readU32(pos);
    pos += nonceLen;
    uint32_t sigLen = readU32(pos);
    pos += sigLen;

    if (pos + 4 <= data.size()) {
        uint32_t ackLen = readU32(pos);
        if (ackLen > 0 && pos + ackLen <= data.size()) {
            std::vector<uint8_t> ack(data.begin() + pos, data.begin() + pos + ackLen);
            // We can verify ack optionally: expected = sha256(remoteNodeId || impl_->remoteNonce)
            std::vector<uint8_t> expectedInput = result.remoteNodeId;
            expectedInput.insert(expectedInput.end(), impl_->remoteNonce.begin(), impl_->remoteNonce.end());
            auto expected = crypto::sha256(expectedInput.data(), expectedInput.size());
            if (ack.size() == expected.size() && std::memcmp(ack.data(), expected.data(), ack.size()) == 0) {
                // ack verified
            }
        }
    }

    impl_->complete = true;
    return result;
}

std::vector<uint8_t> Handshake::getSessionKey() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->sessionKey;
}

bool Handshake::isComplete() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->complete;
}

}
}
