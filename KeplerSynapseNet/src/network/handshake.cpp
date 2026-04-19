#include "network/network.h"
#include "crypto/crypto.h"
#include "crypto/keys.h"
#include "quantum/quantum_security.h"
#include <cstring>
#include <mutex>
#include <random>
#include <openssl/rand.h>

namespace synapse {
namespace network {

static const uint32_t HANDSHAKE_MAGIC = 0x534E4554;
static constexpr size_t HANDSHAKE_NONCE_SIZE = 32;
static constexpr uint32_t HANDSHAKE_CAP_HYBRID_KEM = 0x1u;

static bool handshakePqStrictMode() {
    const char* env = std::getenv("SYNAPSE_HANDSHAKE_PQ_STRICT");
    if (!env || !*env) return false;
    return std::string(env) != "0";
}

static std::vector<uint8_t> deriveHandshakeSessionKey(const std::vector<uint8_t>& classicalSharedSecret,
                                                      const std::vector<uint8_t>& pqcSharedSecret,
                                                      const std::vector<uint8_t>& localPublicKey,
                                                      const std::vector<uint8_t>& remotePublicKey,
                                                      const std::vector<uint8_t>& localNonce,
                                                      const std::vector<uint8_t>& remoteNonce) {
    if (classicalSharedSecret.size() != crypto::SHA256_SIZE) return {};
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
    const char kLabel[] = "synapse-handshake-session-key-v2";
    info.insert(info.end(), kLabel, kLabel + sizeof(kLabel) - 1);
    info.insert(info.end(), firstPub.begin(), firstPub.end());
    info.insert(info.end(), secondPub.begin(), secondPub.end());
    const uint32_t pqcLen = static_cast<uint32_t>(pqcSharedSecret.size());
    info.push_back(static_cast<uint8_t>(pqcLen & 0xff));
    info.push_back(static_cast<uint8_t>((pqcLen >> 8) & 0xff));
    info.push_back(static_cast<uint8_t>((pqcLen >> 16) & 0xff));
    info.push_back(static_cast<uint8_t>((pqcLen >> 24) & 0xff));
    info.insert(info.end(), pqcSharedSecret.begin(), pqcSharedSecret.end());
    info.push_back(0x01);

    std::vector<uint8_t> ikm;
    ikm.reserve(classicalSharedSecret.size() + pqcSharedSecret.size());
    ikm.insert(ikm.end(), classicalSharedSecret.begin(), classicalSharedSecret.end());
    ikm.insert(ikm.end(), pqcSharedSecret.begin(), pqcSharedSecret.end());

    auto prk = crypto::hmacSha256(salt, ikm);
    return crypto::hmacSha256(prk, info);
}

struct Handshake::Impl {
    std::vector<uint8_t> nodeId;
    std::vector<uint8_t> publicKey;
    std::vector<uint8_t> privateKey;
    std::vector<uint8_t> sessionKey;
    std::vector<uint8_t> localNonce;
    std::vector<uint8_t> remoteNonce;
    std::vector<uint8_t> localKyberPublicKey;
    std::vector<uint8_t> localKyberSecretKey;
    std::vector<uint8_t> remoteKyberPublicKey;
    std::vector<uint8_t> kyberCiphertextForRemote;
    std::vector<uint8_t> kyberSharedSecret;
    std::vector<uint8_t> pendingClassicalSharedSecret;
    std::vector<uint8_t> pendingRemotePublicKey;
    uint32_t capabilities = HANDSHAKE_CAP_HYBRID_KEM;
    uint32_t remoteCapabilities = 0;
    bool complete = false;
    mutable std::mutex mtx;

    std::vector<uint8_t> sign(const std::vector<uint8_t>& data);
    bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature,
                const std::vector<uint8_t>& pubKey);
    void generateKyberKeyPair();
    bool computeClassicalSharedSecret(const std::vector<uint8_t>& remotePublicKey,
                                      std::vector<uint8_t>& sharedOut);
    void zeroKyberSecret();
    std::vector<uint8_t> deriveSessionKeyLocked();
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

void Handshake::Impl::generateKyberKeyPair() {
    quantum::Kyber kyber;
    auto kp = kyber.generateKeyPair();
    localKyberPublicKey.assign(kp.publicKey.begin(), kp.publicKey.end());
    localKyberSecretKey.assign(kp.secretKey.begin(), kp.secretKey.end());
}

void Handshake::Impl::zeroKyberSecret() {
    if (!localKyberSecretKey.empty()) {
        crypto::secureZero(localKyberSecretKey.data(), localKyberSecretKey.size());
        localKyberSecretKey.clear();
    }
    if (!kyberSharedSecret.empty()) {
        crypto::secureZero(kyberSharedSecret.data(), kyberSharedSecret.size());
        kyberSharedSecret.clear();
    }
    if (!pendingClassicalSharedSecret.empty()) {
        crypto::secureZero(pendingClassicalSharedSecret.data(), pendingClassicalSharedSecret.size());
        pendingClassicalSharedSecret.clear();
    }
}

bool Handshake::Impl::computeClassicalSharedSecret(const std::vector<uint8_t>& remotePublicKey,
                                                   std::vector<uint8_t>& sharedOut) {
    crypto::Keys localKeys;
    if (!localKeys.fromPrivateKey(privateKey)) {
        return false;
    }
    sharedOut = localKeys.sharedSecret(remotePublicKey);
    if (sharedOut.size() != crypto::SHA256_SIZE) {
        if (!sharedOut.empty()) crypto::secureZero(sharedOut.data(), sharedOut.size());
        sharedOut.clear();
        return false;
    }
    return true;
}

std::vector<uint8_t> Handshake::Impl::deriveSessionKeyLocked() {
    if (pendingClassicalSharedSecret.empty() || pendingRemotePublicKey.empty()) {
        return {};
    }
    return deriveHandshakeSessionKey(
        pendingClassicalSharedSecret, kyberSharedSecret,
        publicKey, pendingRemotePublicKey,
        localNonce, remoteNonce);
}

Handshake::Handshake() : impl_(std::make_unique<Impl>()) {}
Handshake::~Handshake() {
    if (impl_) {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->zeroKyberSecret();
    }
}

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

    impl_->generateKyberKeyPair();

    return true;
}

void Handshake::setCapabilities(uint32_t caps) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->capabilities = caps | HANDSHAKE_CAP_HYBRID_KEM;
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

    writeU32(static_cast<uint32_t>(impl_->localKyberPublicKey.size()));
    msg.insert(msg.end(), impl_->localKyberPublicKey.begin(), impl_->localKyberPublicKey.end());

    writeU32(static_cast<uint32_t>(impl_->kyberCiphertextForRemote.size()));
    msg.insert(msg.end(), impl_->kyberCiphertextForRemote.begin(), impl_->kyberCiphertextForRemote.end());

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
    impl_->remoteCapabilities = result.remoteCapabilities;

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

    if (pos + 4 <= data.size()) {
        uint32_t remoteKyberPkLen = readU32(pos);
        if (remoteKyberPkLen > 0) {
            if (pos + remoteKyberPkLen > data.size()) {
                result.error = "Invalid Kyber public key length";
                return result;
            }
            if (remoteKyberPkLen == quantum::KYBER_PUBLIC_KEY_SIZE) {
                impl_->remoteKyberPublicKey.assign(data.begin() + pos, data.begin() + pos + remoteKyberPkLen);
            }
            pos += remoteKyberPkLen;
        }
    }

    if (pos + 4 <= data.size()) {
        uint32_t remoteCtLen = readU32(pos);
        if (remoteCtLen > 0) {
            if (pos + remoteCtLen > data.size()) {
                result.error = "Invalid Kyber ciphertext length";
                return result;
            }
            if (remoteCtLen == quantum::KYBER_CIPHERTEXT_SIZE &&
                impl_->localKyberSecretKey.size() == quantum::KYBER_SECRET_KEY_SIZE) {
                quantum::KyberCiphertext ct{};
                std::memcpy(ct.data(), data.data() + pos, remoteCtLen);
                quantum::KyberSecretKey sk{};
                std::memcpy(sk.data(), impl_->localKyberSecretKey.data(), sk.size());
                quantum::Kyber kyber;
                auto ss = kyber.decapsulate(ct, sk);
                crypto::secureZero(sk.data(), sk.size());
                if (ss.size() == quantum::KYBER_SHARED_SECRET_SIZE) {
                    impl_->kyberSharedSecret = std::move(ss);
                }
            }
            pos += remoteCtLen;
        }
    }

    std::vector<uint8_t> classicalShared;
    if (!impl_->computeClassicalSharedSecret(result.remotePublicKey, classicalShared)) {
        result.error = "Failed to derive classical shared secret";
        return result;
    }

    impl_->pendingClassicalSharedSecret = classicalShared;
    impl_->pendingRemotePublicKey = result.remotePublicKey;

    if (impl_->remoteKyberPublicKey.size() == quantum::KYBER_PUBLIC_KEY_SIZE &&
        impl_->kyberCiphertextForRemote.empty()) {
        quantum::KyberPublicKey remotePk{};
        std::memcpy(remotePk.data(), impl_->remoteKyberPublicKey.data(), remotePk.size());
        quantum::Kyber kyber;
        auto enc = kyber.encapsulate(remotePk);
        if (enc.success &&
            enc.ciphertext.size() == quantum::KYBER_CIPHERTEXT_SIZE &&
            enc.sharedSecret.size() == quantum::KYBER_SHARED_SECRET_SIZE) {
            impl_->kyberCiphertextForRemote = std::move(enc.ciphertext);
            if (impl_->kyberSharedSecret.empty()) {
                impl_->kyberSharedSecret = std::move(enc.sharedSecret);
            } else {
                crypto::secureZero(enc.sharedSecret.data(), enc.sharedSecret.size());
            }
        }
    }

    if (handshakePqStrictMode()
        && impl_->kyberSharedSecret.size() != quantum::KYBER_SHARED_SECRET_SIZE) {
        crypto::secureZero(classicalShared.data(), classicalShared.size());
        result.error = "Hybrid KEM strict mode: peer did not contribute a Kyber shared secret";
        return result;
    }

    auto sessionKey = impl_->deriveSessionKeyLocked();
    crypto::secureZero(classicalShared.data(), classicalShared.size());
    if (sessionKey.empty()) {
        result.error = "Failed to derive session key";
        return result;
    }
    impl_->sessionKey = sessionKey;
    result.sessionKey = std::move(sessionKey);

    result.success = true;
    return result;
}

std::vector<uint8_t> Handshake::createResponseMessage(const HandshakeResult& initResult) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto msg = createInitMessage();
    std::vector<uint8_t> ackInput = initResult.remoteNodeId;
    ackInput.insert(ackInput.end(), impl_->remoteNonce.begin(), impl_->remoteNonce.end());
    auto ack = crypto::sha256(ackInput.data(), ackInput.size());
    uint32_t len = static_cast<uint32_t>(ack.size());
    for (int i = 0; i < 4; i++) msg.push_back((len >> (i * 8)) & 0xFF);
    msg.insert(msg.end(), ack.begin(), ack.end());
    return msg;
}

HandshakeResult Handshake::processResponseMessage(const std::vector<uint8_t>& data) {
    auto result = processInitMessage(data);
    if (!result.success) return result;

    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->complete = true;
    }

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
