#include "quantum/wallet_security.h"
#include "crypto/crypto.h"
#include "oqs_rng_scope.h"
#include "pqc_backend_oqs.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <string>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse {
namespace quantum {

namespace {

constexpr const char* kWalletV4KyberSeedInfo = "synapsenet-wallet-v4-kyber-seed";

// Short-key envelope for encryptSeed / wrapKey only (not wallet file v3/v4):
//   magic[4] = 'S''N''W''S' | salt[16] | AES-256-GCM blob
// Keys of AES-256 length stay on the raw crypto::encryptAES layout.
constexpr uint8_t kStretchMagic[4] = {0x53, 0x4e, 0x57, 0x53};
constexpr size_t kStretchSaltLen = 16;
constexpr size_t kGcmIvLen = 12;
constexpr size_t kGcmTagLen = 16;

bool keyFitsAes(const std::vector<uint8_t>& key) {
    return key.size() >= crypto::AES_KEY_SIZE;
}

std::array<uint8_t, crypto::AES_KEY_SIZE> aesKeyPrefix(const std::vector<uint8_t>& key) {
    std::array<uint8_t, crypto::AES_KEY_SIZE> out{};
    std::copy_n(key.begin(), out.size(), out.begin());
    return out;
}

// crypto::deriveKey is PBKDF2-HMAC-SHA256. Its iteration count stays inside
// that helper. Wallet-file PBKDF2 (wallet.cpp) is a different call.
bool stretchToAesKey(const std::vector<uint8_t>& key,
                     const std::vector<uint8_t>& salt,
                     std::array<uint8_t, crypto::AES_KEY_SIZE>& out) {
    out.fill(0);
    if (salt.size() != kStretchSaltLen) return false;
    std::string password(key.begin(), key.end());
    auto derived = crypto::deriveKey(password, salt);
    if (!password.empty()) {
        OPENSSL_cleanse(password.data(), password.size());
    }
    uint8_t acc = 0;
    for (uint8_t b : derived) acc = static_cast<uint8_t>(acc | b);
    if (acc == 0) {
        OPENSSL_cleanse(derived.data(), derived.size());
        return false;
    }
    out = derived;
    OPENSSL_cleanse(derived.data(), derived.size());
    return true;
}

std::vector<uint8_t> encryptWithSuppliedKey(const std::vector<uint8_t>& plaintext,
                                            const std::vector<uint8_t>& key) {
    if (plaintext.empty()) return {};
    if (keyFitsAes(key)) {
        auto aes = aesKeyPrefix(key);
        auto sealed = crypto::encryptAES(plaintext, aes);
        OPENSSL_cleanse(aes.data(), aes.size());
        return sealed;
    }

    auto salt = crypto::randomBytes(kStretchSaltLen);
    if (salt.size() != kStretchSaltLen) return {};
    std::array<uint8_t, crypto::AES_KEY_SIZE> aes{};
    if (!stretchToAesKey(key, salt, aes)) {
        OPENSSL_cleanse(salt.data(), salt.size());
        return {};
    }
    auto sealed = crypto::encryptAES(plaintext, aes);
    OPENSSL_cleanse(aes.data(), aes.size());
    if (sealed.empty()) {
        OPENSSL_cleanse(salt.data(), salt.size());
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve(sizeof(kStretchMagic) + salt.size() + sealed.size());
    out.insert(out.end(), kStretchMagic, kStretchMagic + sizeof(kStretchMagic));
    out.insert(out.end(), salt.begin(), salt.end());
    out.insert(out.end(), sealed.begin(), sealed.end());
    OPENSSL_cleanse(salt.data(), salt.size());
    return out;
}

std::vector<uint8_t> decryptWithSuppliedKey(const std::vector<uint8_t>& data,
                                            const std::vector<uint8_t>& key) {
    if (data.empty()) return {};
    if (keyFitsAes(key)) {
        auto aes = aesKeyPrefix(key);
        auto opened = crypto::decryptAES(data, aes);
        OPENSSL_cleanse(aes.data(), aes.size());
        return opened;
    }

    const size_t header = sizeof(kStretchMagic) + kStretchSaltLen;
    if (data.size() < header + kGcmIvLen + kGcmTagLen) return {};
    if (std::memcmp(data.data(), kStretchMagic, sizeof(kStretchMagic)) != 0) {
        // A short key has no single-hash fallback. Missing salt means failure.
        return {};
    }
    std::vector<uint8_t> salt(data.begin() + sizeof(kStretchMagic),
                              data.begin() + static_cast<std::ptrdiff_t>(header));
    std::array<uint8_t, crypto::AES_KEY_SIZE> aes{};
    if (!stretchToAesKey(key, salt, aes)) {
        OPENSSL_cleanse(salt.data(), salt.size());
        return {};
    }
    OPENSSL_cleanse(salt.data(), salt.size());
    std::vector<uint8_t> sealed(data.begin() + static_cast<std::ptrdiff_t>(header), data.end());
    auto opened = crypto::decryptAES(sealed, aes);
    OPENSSL_cleanse(aes.data(), aes.size());
    return opened;
}

#ifdef USE_LIBOQS

// This liboqs has no OQS_KEM_keypair_derand. Seed Kyber the same way
// HybridSig seeds Dilithium: HKDF → SHAKE stream → temporary OQS RNG.
std::vector<uint8_t> shake256Expand(const std::vector<uint8_t>& seed, size_t outputLen) {
    crypto::containForeignCryptoModules();
    std::vector<uint8_t> out(outputLen);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    bool ok = true;
    if (1 != EVP_DigestInit_ex(ctx, EVP_shake256(), nullptr)) ok = false;
    if (ok && 1 != EVP_DigestUpdate(ctx, seed.data(), seed.size())) ok = false;
    if (ok && 1 != EVP_DigestFinalXOF(ctx, out.data(), outputLen)) ok = false;
    EVP_MD_CTX_free(ctx);
    if (!ok) {
        OPENSSL_cleanse(out.data(), out.size());
        out.clear();
        return {};
    }
    return out;
}

// ML-KEM-768 and Kyber768 keygen read 64 coins (d || z) in one draw.
constexpr size_t kDeterministicKeygenCoins = 64;

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

    // The supplied key is what decryptSeed receives. Seal with that key so
    // the blob round-trips. hybridKEM_ is stored for callers, and a keypair
    // it generates is not retained for decrypt.
    (void)hybridKEM_;
    return encryptWithSuppliedKey(seed, key);
}

std::vector<uint8_t> WalletSecurity::decryptSeed(const std::vector<uint8_t>& data,
                                                 const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};

    if (level_ == SecurityLevel::QUANTUM_READY && qm_) {
        return qm_->decryptQuantumSafe(data);
    }

    return decryptWithSuppliedKey(data, key);
}

std::vector<uint8_t> WalletSecurity::wrapKey(const std::vector<uint8_t>& privateKey,
                                             const std::vector<uint8_t>& wrappingKey) const {
    if (privateKey.empty()) return {};
    // Same sealing rule as encryptSeed. The wrapping key is the unwrap secret.
    (void)hybridKEM_;
    return encryptWithSuppliedKey(privateKey, wrappingKey);
}

std::vector<uint8_t> WalletSecurity::unwrapKey(const std::vector<uint8_t>& data,
                                               const std::vector<uint8_t>& wrappingKey) const {
    if (data.empty()) return {};
    return decryptWithSuppliedKey(data, wrappingKey);
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
    if (seed.size() != 32) {
        if (!seed.empty()) OPENSSL_cleanse(seed.data(), seed.size());
        return false;
    }

    const size_t streamLen = 64 * 1024;
    auto stream = shake256Expand(seed, streamLen);
    OPENSSL_cleanse(seed.data(), seed.size());
    if (stream.size() != streamLen) {
        if (!stream.empty()) OPENSSL_cleanse(stream.data(), stream.size());
        return false;
    }

    detail::OqsDeterministicRngGuard rng(std::move(stream));
    Kyber kyber;
    out = kyber.generateKeyPair();
    // Coins must come from this thread's stream. A keypair drawn while the
    // hook was exhausted, or never consulted, is not reproducible.
    if (!rng.accepted(kDeterministicKeygenCoins)) {
        OPENSSL_cleanse(out.publicKey.data(), out.publicKey.size());
        OPENSSL_cleanse(out.secretKey.data(), out.secretKey.size());
        out = KyberKeyPair{};
        return false;
    }

    const bool ok = kyber.validatePublicKey(out.publicKey) && kyber.validateSecretKey(out.secretKey);
    if (!ok) {
        OPENSSL_cleanse(out.secretKey.data(), out.secretKey.size());
        out = KyberKeyPair{};
        return false;
    }
    return true;
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
