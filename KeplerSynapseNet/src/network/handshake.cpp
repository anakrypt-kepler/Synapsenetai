// Peer handshake: X25519 + optional ML-KEM-768, then HKDF session key.
// Magic 0x534E4554 = "SNET". The ML-KEM public key and ciphertext are inside
// the signed transcript. A peer KEM key requires a full-size ML-KEM share.
// With no KEM key from the peer, the classical handshake still completes.
// SYNAPSE_HANDSHAKE_PQ_STRICT=1 refuses that classical-only result.
// This is a hybrid KEM (RFC 10024 family: X25519 + ML-KEM-768), not Dilithium.
// Signatures stay on KQAS application envelopes. Do not put HybridSig here.

#include "network/network.h"
#include "crypto/crypto.h"
#include "crypto/keys.h"
#include "quantum/quantum_security.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
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

static void wipeSecret(std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return;
    crypto::secureZero(bytes.data(), bytes.size());
    bytes.clear();
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
    bool initMessageSent = false;
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
    std::vector<uint8_t> buildInitMessageLocked();
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
    wipeSecret(localKyberSecretKey);
    wipeSecret(kyberSharedSecret);
    wipeSecret(pendingClassicalSharedSecret);
}

bool Handshake::Impl::computeClassicalSharedSecret(const std::vector<uint8_t>& remotePublicKey,
                                                   std::vector<uint8_t>& sharedOut) {
    crypto::Keys localKeys;
    if (!localKeys.fromPrivateKey(privateKey)) {
        return false;
    }
    sharedOut = localKeys.sharedSecret(remotePublicKey);
    if (sharedOut.size() != crypto::SHA256_SIZE) {
        wipeSecret(sharedOut);
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

std::vector<uint8_t> Handshake::Impl::buildInitMessageLocked() {
    std::vector<uint8_t> msg;

    auto writeU32 = [&msg](uint32_t val) {
        for (int i = 0; i < 4; i++) msg.push_back((val >> (i * 8)) & 0xff);
    };

    writeU32(HANDSHAKE_MAGIC);
    writeU32(PROTOCOL_VERSION);
    writeU32(capabilities);

    writeU32(static_cast<uint32_t>(nodeId.size()));
    msg.insert(msg.end(), nodeId.begin(), nodeId.end());

    writeU32(static_cast<uint32_t>(publicKey.size()));
    msg.insert(msg.end(), publicKey.begin(), publicKey.end());

    writeU32(static_cast<uint32_t>(localNonce.size()));
    msg.insert(msg.end(), localNonce.begin(), localNonce.end());

    writeU32(static_cast<uint32_t>(localKyberPublicKey.size()));
    msg.insert(msg.end(), localKyberPublicKey.begin(), localKyberPublicKey.end());

    writeU32(static_cast<uint32_t>(kyberCiphertextForRemote.size()));
    msg.insert(msg.end(), kyberCiphertextForRemote.begin(), kyberCiphertextForRemote.end());

    auto sig = sign(msg);
    writeU32(static_cast<uint32_t>(sig.size()));
    msg.insert(msg.end(), sig.begin(), sig.end());

    initMessageSent = true;
    return msg;
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
    impl_->zeroKyberSecret();
    impl_->kyberCiphertextForRemote.clear();
    impl_->remoteKyberPublicKey.clear();
    impl_->pendingRemotePublicKey.clear();
    impl_->sessionKey.clear();
    impl_->remoteNonce.clear();
    impl_->remoteCapabilities = 0;
    impl_->initMessageSent = false;
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
    return impl_->buildInitMessageLocked();
}

HandshakeResult Handshake::processInitMessage(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    HandshakeResult result{};
    result.success = false;

    wipeSecret(impl_->kyberSharedSecret);
    impl_->kyberCiphertextForRemote.clear();
    impl_->remoteKyberPublicKey.clear();
    wipeSecret(impl_->pendingClassicalSharedSecret);
    impl_->pendingRemotePublicKey.clear();
    impl_->sessionKey.clear();

    if (data.size() < 16) {
        result.error = "Message too short";
        return result;
    }

    auto readU32 = [&data](size_t& pos, uint32_t& out) -> bool {
        if (pos > data.size() || data.size() - pos < 4) return false;
        uint32_t val = 0;
        for (int i = 0; i < 4; i++) val |= static_cast<uint32_t>(data[pos + static_cast<size_t>(i)]) << (i * 8);
        pos += 4;
        out = val;
        return true;
    };

    auto readBytes = [&data](size_t& pos, uint32_t len, std::vector<uint8_t>& out) -> bool {
        if (pos > data.size() || static_cast<size_t>(len) > data.size() - pos) return false;
        out.assign(data.begin() + pos, data.begin() + pos + len);
        pos += len;
        return true;
    };

    size_t pos = 0;
    uint32_t magic = 0;
    if (!readU32(pos, magic) || magic != HANDSHAKE_MAGIC) {
        result.error = "Invalid magic";
        return result;
    }

    uint32_t remoteVersion = 0;
    uint32_t remoteCaps = 0;
    if (!readU32(pos, remoteVersion) || !readU32(pos, remoteCaps)) {
        result.error = "Message too short";
        return result;
    }
    result.remoteVersion = remoteVersion;
    result.remoteCapabilities = remoteCaps;

    uint32_t nodeIdLen = 0;
    if (!readU32(pos, nodeIdLen) || !readBytes(pos, nodeIdLen, result.remoteNodeId)) {
        result.error = "Invalid node ID length";
        return result;
    }

    uint32_t pubKeyLen = 0;
    if (!readU32(pos, pubKeyLen) || !readBytes(pos, pubKeyLen, result.remotePublicKey)) {
        result.error = "Invalid public key length";
        return result;
    }

    uint32_t nonceLen = 0;
    std::vector<uint8_t> remoteNonce;
    if (!readU32(pos, nonceLen) || !readBytes(pos, nonceLen, remoteNonce)) {
        result.error = "Invalid nonce length";
        return result;
    }

    uint32_t remoteKemPkLen = 0;
    std::vector<uint8_t> remoteKemPk;
    if (!readU32(pos, remoteKemPkLen) || !readBytes(pos, remoteKemPkLen, remoteKemPk)) {
        result.error = "Invalid Kyber public key length";
        return result;
    }

    uint32_t remoteCtLen = 0;
    std::vector<uint8_t> remoteCt;
    if (!readU32(pos, remoteCtLen) || !readBytes(pos, remoteCtLen, remoteCt)) {
        result.error = "Invalid Kyber ciphertext length";
        return result;
    }

    const size_t signedLen = pos;
    uint32_t sigLen = 0;
    std::vector<uint8_t> signature;
    if (!readU32(pos, sigLen) || sigLen == 0 || !readBytes(pos, sigLen, signature)) {
        result.error = "Invalid signature length";
        return result;
    }

    std::vector<uint8_t> signedData(data.begin(), data.begin() + signedLen);
    if (!impl_->verify(signedData, signature, result.remotePublicKey)) {
        result.error = "Invalid signature";
        return result;
    }

    impl_->remoteCapabilities = result.remoteCapabilities;
    impl_->remoteNonce = std::move(remoteNonce);

    auto fail = [&](const char* message) -> HandshakeResult {
        wipeSecret(impl_->kyberSharedSecret);
        impl_->remoteKyberPublicKey.clear();
        impl_->kyberCiphertextForRemote.clear();
        result.success = false;
        result.sessionKey.clear();
        result.error = message;
        return result;
    };

    const bool peerSentKemKey = remoteKemPkLen > 0;
    if (peerSentKemKey && remoteKemPkLen != quantum::KYBER_PUBLIC_KEY_SIZE) {
        return fail("Invalid Kyber public key length");
    }
    if (remoteCtLen > 0 && remoteCtLen != quantum::KYBER_CIPHERTEXT_SIZE) {
        return fail("Invalid Kyber ciphertext length");
    }
    // The first flight carries the peer KEM key and an empty ciphertext.
    // A handshake message sent after ours must include their ciphertext share.
    if (impl_->initMessageSent && peerSentKemKey && remoteCtLen != quantum::KYBER_CIPHERTEXT_SIZE) {
        return fail("Invalid Kyber shared secret");
    }

    if (remoteKemPkLen == quantum::KYBER_PUBLIC_KEY_SIZE) {
        impl_->remoteKyberPublicKey = std::move(remoteKemPk);
    }

    if (remoteCtLen == quantum::KYBER_CIPHERTEXT_SIZE) {
        if (impl_->localKyberSecretKey.size() != quantum::KYBER_SECRET_KEY_SIZE) {
            return fail("Invalid Kyber shared secret");
        }
        quantum::KyberCiphertext ct{};
        std::memcpy(ct.data(), remoteCt.data(), remoteCt.size());
        quantum::KyberSecretKey sk{};
        std::memcpy(sk.data(), impl_->localKyberSecretKey.data(), sk.size());
        quantum::Kyber kyber;
        auto ss = kyber.decapsulate(ct, sk);
        crypto::secureZero(sk.data(), sk.size());
        if (ss.size() != quantum::KYBER_SHARED_SECRET_SIZE) {
            wipeSecret(ss);
            return fail("Invalid Kyber shared secret");
        }
        impl_->kyberSharedSecret = std::move(ss);
    }

    if (impl_->kyberSharedSecret.empty()
        && impl_->remoteKyberPublicKey.size() == quantum::KYBER_PUBLIC_KEY_SIZE
        && impl_->kyberCiphertextForRemote.empty()) {
        quantum::KyberPublicKey remotePk{};
        std::memcpy(remotePk.data(), impl_->remoteKyberPublicKey.data(), remotePk.size());
        quantum::Kyber kyber;
        auto enc = kyber.encapsulate(remotePk);
        const bool shareOk = enc.success
            && enc.ciphertext.size() == quantum::KYBER_CIPHERTEXT_SIZE
            && enc.sharedSecret.size() == quantum::KYBER_SHARED_SECRET_SIZE;
        if (!shareOk) {
            wipeSecret(enc.sharedSecret);
            return fail("Invalid Kyber shared secret");
        }
        impl_->kyberCiphertextForRemote = std::move(enc.ciphertext);
        impl_->kyberSharedSecret = std::move(enc.sharedSecret);
    }

    if (peerSentKemKey && impl_->kyberSharedSecret.size() != quantum::KYBER_SHARED_SECRET_SIZE) {
        return fail("Invalid Kyber shared secret");
    }

    if (handshakePqStrictMode()
        && impl_->kyberSharedSecret.size() != quantum::KYBER_SHARED_SECRET_SIZE) {
        return fail("Hybrid KEM strict mode: peer did not contribute a Kyber shared secret");
    }

    std::vector<uint8_t> classicalShared;
    if (!impl_->computeClassicalSharedSecret(result.remotePublicKey, classicalShared)) {
        wipeSecret(impl_->kyberSharedSecret);
        impl_->kyberCiphertextForRemote.clear();
        result.error = "Failed to derive classical shared secret";
        return result;
    }

    impl_->pendingClassicalSharedSecret = classicalShared;
    impl_->pendingRemotePublicKey = result.remotePublicKey;

    auto sessionKey = impl_->deriveSessionKeyLocked();
    wipeSecret(classicalShared);
    if (sessionKey.empty()) {
        wipeSecret(impl_->pendingClassicalSharedSecret);
        wipeSecret(impl_->kyberSharedSecret);
        impl_->pendingRemotePublicKey.clear();
        impl_->kyberCiphertextForRemote.clear();
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
    auto msg = impl_->buildInitMessageLocked();
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
