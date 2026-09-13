#include "quantum/wallet_security.h"
#include "crypto/crypto.h"
#include "pqc_backend_oqs.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse {
namespace quantum {

namespace {

constexpr const char* kWalletV4KyberSeedInfo = "synapsenet-wallet-v4-kyber-seed";

std::array<uint8_t, crypto::AES_KEY_SIZE> toAesKey(const std::vector<uint8_t>& key) {
    std::array<uint8_t, crypto::AES_KEY_SIZE> out{};
    if (key.size() >= out.size()) {
        std::copy_n(key.begin(), out.size(), out.begin());
        return out;
    }
    auto hash = crypto::sha256(key.data(), key.size());
    std::copy_n(hash.begin(), out.size(), out.begin());
    return out;
}

#ifdef USE_LIBOQS

// This liboqs has no OQS_KEM_keypair_derand. Seed Kyber the same way
// HybridSig seeds Dilithium: HKDF → SHAKE stream → temporary OQS RNG.
std::vector<uint8_t> shake256Expand(const std::vector<uint8_t>& seed, size_t outputLen) {
    std::vector<uint8_t> out(outputLen);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    bool ok = true;
    if (1 != EVP_DigestInit_ex(ctx, EVP_shake256(), nullptr)) ok = false;
    if (ok && 1 != EVP_DigestUpdate(ctx, seed.data(), seed.size())) ok = false;
    if (ok && 1 != EVP_DigestFinalXOF(ctx, out.data(), outputLen)) ok = false;
    EVP_MD_CTX_free(ctx);
    if (!ok) {
        out.clear();
        return {};
    }
    return out;
}

std::mutex& fileKyberRngMutex() {
    static std::mutex m;
    return m;
}

thread_local std::vector<uint8_t> tl_file_kyber_stream;
thread_local size_t tl_file_kyber_pos = 0;

void fileKyberRngCallback(uint8_t* out, size_t n) {
    while (n > 0) {
        if (tl_file_kyber_pos >= tl_file_kyber_stream.size()) {
            std::memset(out, 0, n);
            return;
        }
        const size_t take = std::min(n, tl_file_kyber_stream.size() - tl_file_kyber_pos);
        std::memcpy(out, tl_file_kyber_stream.data() + tl_file_kyber_pos, take);
        tl_file_kyber_pos += take;
        out += take;
        n -= take;
    }
}

#endif // USE_LIBOQS

} // namespace

WalletSecurity::WalletSecurity() = default;

void WalletSecurity::setSecurityLevel(SecurityLevel level) {
    level_ = level;
}

SecurityLevel WalletSecurity::getSecurityLevel() const {
    return level_;
}

void WalletSecurity::setQuantumManager(QuantumManager* qm) {
    qm_ = qm;
}

void WalletSecurity::setHybridKEM(HybridKEM* kem) {
    hybridKEM_ = kem;
}

std::vector<uint8_t> WalletSecurity::encryptSeed(const std::vector<uint8_t>& seed,
                                                 const std::vector<uint8_t>& key) const {
    if (seed.empty()) return {};

    if (level_ == SecurityLevel::QUANTUM_READY && qm_) {
        return qm_->encryptQuantumSafe(seed);
    }

    if (level_ >= SecurityLevel::HIGH && hybridKEM_) {
        HybridKeyPair kemKp = hybridKEM_->generateKeyPair();
        auto kemResult = hybridKEM_->encapsulate(kemKp);
        if (kemResult.success && kemResult.sharedSecret.size() >= crypto::AES_KEY_SIZE) {
            std::array<uint8_t, crypto::AES_KEY_SIZE> derivedKey{};
            std::copy_n(kemResult.sharedSecret.begin(), crypto::AES_KEY_SIZE, derivedKey.begin());
            auto encrypted = crypto::encryptAES(seed, derivedKey);

            std::vector<uint8_t> envelope;
            uint32_t ctLen = static_cast<uint32_t>(kemResult.ciphertext.size());
            envelope.push_back(static_cast<uint8_t>((ctLen >> 24) & 0xFF));
            envelope.push_back(static_cast<uint8_t>((ctLen >> 16) & 0xFF));
            envelope.push_back(static_cast<uint8_t>((ctLen >> 8) & 0xFF));
            envelope.push_back(static_cast<uint8_t>(ctLen & 0xFF));
            envelope.insert(envelope.end(), kemResult.ciphertext.begin(), kemResult.ciphertext.end());
            envelope.insert(envelope.end(), encrypted.begin(), encrypted.end());
            return envelope;
        }
    }

    return crypto::encryptAES(seed, toAesKey(key));
}

std::vector<uint8_t> WalletSecurity::decryptSeed(const std::vector<uint8_t>& data,
                                                 const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};

    if (level_ == SecurityLevel::QUANTUM_READY && qm_) {
        return qm_->decryptQuantumSafe(data);
    }

    return crypto::decryptAES(data, toAesKey(key));
}

std::vector<uint8_t> WalletSecurity::wrapKey(const std::vector<uint8_t>& privateKey,
                                             const std::vector<uint8_t>& wrappingKey) const {
    if (privateKey.empty()) return {};

    if (level_ >= SecurityLevel::HIGH && hybridKEM_) {
        HybridKeyPair kemKp = hybridKEM_->generateKeyPair();
        auto kemResult = hybridKEM_->encapsulate(kemKp);
        if (kemResult.success && kemResult.sharedSecret.size() >= crypto::AES_KEY_SIZE) {
            std::array<uint8_t, crypto::AES_KEY_SIZE> derivedKey{};
            std::copy_n(kemResult.sharedSecret.begin(), crypto::AES_KEY_SIZE, derivedKey.begin());
            return crypto::encryptAES(privateKey, derivedKey);
        }
    }

    return crypto::encryptAES(privateKey, toAesKey(wrappingKey));
}

std::vector<uint8_t> WalletSecurity::unwrapKey(const std::vector<uint8_t>& data,
                                               const std::vector<uint8_t>& wrappingKey) const {
    if (data.empty()) return {};
    return crypto::decryptAES(data, toAesKey(wrappingKey));
}

bool WalletSecurity::fileKyberWrapAvailable() {
#ifdef USE_LIBOQS
    return getPQCBackendStatus().kyberReal;
#else
    return false;
#endif
}

bool WalletSecurity::deriveFileKyberKeyPair(const std::vector<uint8_t>& pbkdf2Key32, KyberKeyPair& out) {
    out = KyberKeyPair{};
#ifdef USE_LIBOQS
    if (!fileKyberWrapAvailable()) return false;
    if (pbkdf2Key32.size() != 32) return false;

    KeyDerivation kdf;
    const std::vector<uint8_t> hkdfSalt(32, 0x00);
    const std::vector<uint8_t> info(kWalletV4KyberSeedInfo,
                                    kWalletV4KyberSeedInfo + std::strlen(kWalletV4KyberSeedInfo));
    auto seed = kdf.hkdf(hkdfSalt, pbkdf2Key32, info, 32);
    if (seed.size() != 32) return false;

    const size_t streamLen = 64 * 1024;
    auto stream = shake256Expand(seed, streamLen);
    OPENSSL_cleanse(seed.data(), seed.size());
    if (stream.size() != streamLen) return false;

    std::lock_guard<std::mutex> rngLock(fileKyberRngMutex());
    auto prevStream = std::move(tl_file_kyber_stream);
    const size_t prevPos = tl_file_kyber_pos;
    tl_file_kyber_stream = std::move(stream);
    tl_file_kyber_pos = 0;

    OQS_randombytes_custom_algorithm(fileKyberRngCallback);
    Kyber kyber;
    out = kyber.generateKeyPair();
    OQS_randombytes_switch_algorithm(OQS_RAND_alg_system);

    OPENSSL_cleanse(tl_file_kyber_stream.data(), tl_file_kyber_stream.size());
    tl_file_kyber_stream = std::move(prevStream);
    tl_file_kyber_pos = prevPos;

    return kyber.validatePublicKey(out.publicKey) && kyber.validateSecretKey(out.secretKey);
#else
    (void)pbkdf2Key32;
    return false;
#endif
}

bool WalletSecurity::encapsulateFileDek(const KyberPublicKey& pk,
                                        std::vector<uint8_t>& kemCtOut,
                                        std::vector<uint8_t>& dekOut) {
    kemCtOut.clear();
    dekOut.clear();
    if (!fileKyberWrapAvailable()) return false;

    Kyber kyber;
    auto enc = kyber.encapsulate(pk);
    if (!enc.success
        || enc.ciphertext.size() != KYBER_CIPHERTEXT_SIZE
        || enc.sharedSecret.size() != KYBER_SHARED_SECRET_SIZE) {
        if (!enc.sharedSecret.empty()) {
            OPENSSL_cleanse(enc.sharedSecret.data(), enc.sharedSecret.size());
        }
        return false;
    }
    kemCtOut = std::move(enc.ciphertext);
    dekOut = std::move(enc.sharedSecret);
    return true;
}

bool WalletSecurity::decapsulateFileDek(const std::vector<uint8_t>& kemCt,
                                        const KyberSecretKey& sk,
                                        std::vector<uint8_t>& dekOut) {
    dekOut.clear();
    if (!fileKyberWrapAvailable()) return false;
    if (kemCt.size() != KYBER_CIPHERTEXT_SIZE) return false;

    Kyber kyber;
    KyberCiphertext ct{};
    std::memcpy(ct.data(), kemCt.data(), KYBER_CIPHERTEXT_SIZE);
    auto ss = kyber.decapsulate(ct, sk);
    OPENSSL_cleanse(ct.data(), ct.size());
    if (ss.size() != KYBER_SHARED_SECRET_SIZE) {
        if (!ss.empty()) OPENSSL_cleanse(ss.data(), ss.size());
        return false;
    }
    dekOut = std::move(ss);
    return true;
}

}
}
