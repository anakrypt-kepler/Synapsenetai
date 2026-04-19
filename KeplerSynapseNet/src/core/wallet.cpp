#include "core/wallet.h"
#include "crypto/address.h"
#include "crypto/crypto.h"
#include "quantum/application_signature.h"
#include "tui/bip39_wordlist.h"
#include "utils/logger.h"
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <chrono>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>

namespace synapse {
namespace core {

static constexpr uint8_t WALLET_MAGIC = 0xA5;
static constexpr uint8_t WALLET_FORMAT_V1_MNEMONIC_ONLY = 1;
static constexpr uint8_t WALLET_FORMAT_V2_HYBRID = 2;
static constexpr uint8_t WALLET_FORMAT_V3_HYBRID_GCM = 3;
static constexpr uint32_t WALLET_MAX_PLAINTEXT_FIELD = 16 * 1024;
static constexpr size_t WALLET_CLASSIC_PK_SIZE = 32;
static constexpr size_t WALLET_CLASSIC_SK_SIZE = 64;
static constexpr int WALLET_PBKDF2_ITERATIONS_V3 = 600000;
static constexpr int WALLET_PBKDF2_ITERATIONS_LEGACY = 100000;
static constexpr size_t WALLET_GCM_IV_SIZE = 12;
static constexpr size_t WALLET_GCM_TAG_SIZE = 16;

static std::vector<uint8_t> generateRandom(size_t len) {
    std::vector<uint8_t> result(len);
    if (len > 0) {
        if (RAND_bytes(result.data(), static_cast<int>(len)) != 1) {
            std::random_device rd;
            for (size_t i = 0; i < len; ++i) result[i] = static_cast<uint8_t>(rd() & 0xFF);
        }
    }
    return result;
}

static std::vector<uint8_t> pbkdf2_hmac(const std::string& password, const std::vector<uint8_t>& salt, int iterations, size_t outLen) {
    std::vector<uint8_t> out(outLen);
    if (!PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()), iterations, EVP_sha256(), static_cast<int>(outLen), out.data())) {
        std::fill(out.begin(), out.end(), 0);
    }
    return out;
}

static void appendU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

static bool readU32LE(const uint8_t*& p, const uint8_t* end, uint32_t& out) {
    if (static_cast<size_t>(end - p) < 4) return false;
    out = static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
    p += 4;
    return true;
}

static bool readBytes(const uint8_t*& p, const uint8_t* end, uint32_t size, std::vector<uint8_t>& out) {
    if (size > WALLET_MAX_PLAINTEXT_FIELD) return false;
    if (static_cast<size_t>(end - p) < size) return false;
    out.assign(p, p + size);
    p += size;
    return true;
}

static std::vector<uint8_t> encodeV2Plaintext(const std::string& mnemonic,
                                              const quantum::HybridKeyPair& kp) {
    std::vector<uint8_t> out;
    out.reserve(mnemonic.size() + kp.classicPublicKey.size() + kp.classicSecretKey.size()
                + kp.pqcPublicKey.size() + kp.pqcSecretKey.size() + 32);
    appendU32LE(out, static_cast<uint32_t>(mnemonic.size()));
    out.insert(out.end(), mnemonic.begin(), mnemonic.end());
    appendU32LE(out, static_cast<uint32_t>(kp.classicPublicKey.size()));
    out.insert(out.end(), kp.classicPublicKey.begin(), kp.classicPublicKey.end());
    appendU32LE(out, static_cast<uint32_t>(kp.classicSecretKey.size()));
    out.insert(out.end(), kp.classicSecretKey.begin(), kp.classicSecretKey.end());
    appendU32LE(out, static_cast<uint32_t>(kp.pqcPublicKey.size()));
    out.insert(out.end(), kp.pqcPublicKey.begin(), kp.pqcPublicKey.end());
    appendU32LE(out, static_cast<uint32_t>(kp.pqcSecretKey.size()));
    out.insert(out.end(), kp.pqcSecretKey.begin(), kp.pqcSecretKey.end());
    return out;
}

static bool decodeV2Plaintext(const std::vector<uint8_t>& plaintext,
                              std::string& mnemonicOut,
                              quantum::HybridKeyPair& kpOut) {
    const uint8_t* p = plaintext.data();
    const uint8_t* end = plaintext.data() + plaintext.size();
    uint32_t mnemonicLen = 0;
    if (!readU32LE(p, end, mnemonicLen)) return false;
    if (mnemonicLen > WALLET_MAX_PLAINTEXT_FIELD) return false;
    if (static_cast<size_t>(end - p) < mnemonicLen) return false;
    mnemonicOut.assign(reinterpret_cast<const char*>(p), mnemonicLen);
    p += mnemonicLen;
    uint32_t primPubLen = 0, primSecLen = 0, secPubLen = 0, secSecLen = 0;
    if (!readU32LE(p, end, primPubLen) || !readBytes(p, end, primPubLen, kpOut.classicPublicKey)) return false;
    if (!readU32LE(p, end, primSecLen) || !readBytes(p, end, primSecLen, kpOut.classicSecretKey)) return false;
    if (!readU32LE(p, end, secPubLen) || !readBytes(p, end, secPubLen, kpOut.pqcPublicKey)) return false;
    if (!readU32LE(p, end, secSecLen) || !readBytes(p, end, secSecLen, kpOut.pqcSecretKey)) return false;
    if (p != end) return false;
    kpOut.classicAlgo = quantum::CryptoAlgorithm::CLASSIC_ED25519;
    kpOut.pqcAlgo = quantum::CryptoAlgorithm::LATTICE_DILITHIUM65;
    return kpOut.classicPublicKey.size() == WALLET_CLASSIC_PK_SIZE
        && kpOut.classicSecretKey.size() == WALLET_CLASSIC_SK_SIZE
        && kpOut.pqcPublicKey.size() == quantum::DILITHIUM_PUBLIC_KEY_SIZE
        && kpOut.pqcSecretKey.size() == quantum::DILITHIUM_SECRET_KEY_SIZE;
}

static bool encryptAndWriteV3(const std::string& path,
                              const std::vector<uint8_t>& plaintext,
                              const std::string& password) {
    auto salt = generateRandom(16);
    auto aesKey = pbkdf2_hmac(password, salt, WALLET_PBKDF2_ITERATIONS_V3, 32);
    auto iv = generateRandom(WALLET_GCM_IV_SIZE);

    std::vector<uint8_t> aad;
    aad.reserve(1 + 1 + salt.size() + iv.size());
    aad.push_back(WALLET_MAGIC);
    aad.push_back(WALLET_FORMAT_V3_HYBRID_GCM);
    aad.insert(aad.end(), salt.begin(), salt.end());
    aad.insert(aad.end(), iv.begin(), iv.end());

    std::vector<uint8_t> ciphertext(plaintext.size());
    std::vector<uint8_t> tag(WALLET_GCM_TAG_SIZE);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    int outlen = 0;
    int tmplen = 0;
    bool ok = true;
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)) ok = false;
    if (ok && 1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr)) ok = false;
    if (ok && 1 != EVP_EncryptInit_ex(ctx, nullptr, nullptr, aesKey.data(), iv.data())) ok = false;
    int aadLen = 0;
    if (ok && 1 != EVP_EncryptUpdate(ctx, nullptr, &aadLen, aad.data(), static_cast<int>(aad.size()))) ok = false;
    if (ok && 1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &outlen,
                                     plaintext.data(), static_cast<int>(plaintext.size()))) ok = false;
    if (ok && 1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + outlen, &tmplen)) ok = false;
    if (ok) outlen += tmplen;
    if (ok && 1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data())) ok = false;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return false;
    ciphertext.resize(outlen);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.put(static_cast<char>(WALLET_MAGIC));
    file.put(static_cast<char>(WALLET_FORMAT_V3_HYBRID_GCM));
    file.write(reinterpret_cast<char*>(salt.data()), salt.size());
    file.write(reinterpret_cast<char*>(iv.data()), iv.size());
    file.write(reinterpret_cast<char*>(ciphertext.data()), ciphertext.size());
    file.write(reinterpret_cast<char*>(tag.data()), tag.size());
    return file.good();
}

[[maybe_unused]] static bool encryptAndWriteV2(const std::string& path,
                              const std::vector<uint8_t>& plaintext,
                              const std::string& password,
                              uint8_t formatVersion) {
    auto salt = generateRandom(16);
    auto derived = pbkdf2_hmac(password, salt, WALLET_PBKDF2_ITERATIONS_LEGACY, 64);
    std::vector<uint8_t> aesKey(derived.begin(), derived.begin() + 32);
    std::vector<uint8_t> hmacKey(derived.begin() + 32, derived.begin() + 64);
    auto iv = generateRandom(16);

    std::vector<uint8_t> ciphertext;
    ciphertext.resize(plaintext.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    int outlen = 0;
    int tmplen = 0;
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, aesKey.data(), iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    if (1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &outlen,
                               plaintext.data(), static_cast<int>(plaintext.size()))) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + outlen, &tmplen)) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    outlen += tmplen;
    ciphertext.resize(outlen);
    EVP_CIPHER_CTX_free(ctx);

    std::vector<uint8_t> hmacInput;
    hmacInput.reserve(1 + 1 + salt.size() + iv.size() + ciphertext.size());
    hmacInput.push_back(WALLET_MAGIC);
    hmacInput.push_back(formatVersion);
    hmacInput.insert(hmacInput.end(), salt.begin(), salt.end());
    hmacInput.insert(hmacInput.end(), iv.begin(), iv.end());
    hmacInput.insert(hmacInput.end(), ciphertext.begin(), ciphertext.end());

    unsigned int hmacLen = 0;
    unsigned char hmacOut[EVP_MAX_MD_SIZE];
    if (!HMAC(EVP_sha256(), hmacKey.data(), static_cast<int>(hmacKey.size()),
              hmacInput.data(), hmacInput.size(), hmacOut, &hmacLen)) {
        return false;
    }
    if (hmacLen != 32) return false;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.put(static_cast<char>(WALLET_MAGIC));
    file.put(static_cast<char>(formatVersion));
    file.write(reinterpret_cast<char*>(salt.data()), salt.size());
    file.write(reinterpret_cast<char*>(iv.data()), iv.size());
    file.write(reinterpret_cast<char*>(ciphertext.data()), ciphertext.size());
    file.write(reinterpret_cast<char*>(hmacOut), 32);
    return file.good();
}

static bool decryptFileV3(const std::vector<uint8_t>& encrypted,
                          const std::string& password,
                          std::vector<uint8_t>& plaintextOut) {
    const size_t minSize = 2 + 16 + WALLET_GCM_IV_SIZE + WALLET_GCM_TAG_SIZE;
    if (encrypted.size() < minSize) return false;
    size_t pos = 2;
    std::vector<uint8_t> salt(encrypted.begin() + pos, encrypted.begin() + pos + 16); pos += 16;
    std::vector<uint8_t> iv(encrypted.begin() + pos, encrypted.begin() + pos + WALLET_GCM_IV_SIZE); pos += WALLET_GCM_IV_SIZE;
    if (encrypted.size() < pos + WALLET_GCM_TAG_SIZE) return false;
    const size_t tagPos = encrypted.size() - WALLET_GCM_TAG_SIZE;
    if (tagPos < pos) return false;
    std::vector<uint8_t> ciphertext(encrypted.begin() + pos, encrypted.begin() + tagPos);
    std::vector<uint8_t> tag(encrypted.begin() + tagPos, encrypted.end());

    auto aesKey = pbkdf2_hmac(password, salt, WALLET_PBKDF2_ITERATIONS_V3, 32);
    std::vector<uint8_t> aad;
    aad.reserve(1 + 1 + salt.size() + iv.size());
    aad.push_back(WALLET_MAGIC);
    aad.push_back(WALLET_FORMAT_V3_HYBRID_GCM);
    aad.insert(aad.end(), salt.begin(), salt.end());
    aad.insert(aad.end(), iv.begin(), iv.end());

    plaintextOut.assign(ciphertext.size(), 0);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    int outlen = 0;
    int tmplen = 0;
    bool ok = true;
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)) ok = false;
    if (ok && 1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr)) ok = false;
    if (ok && 1 != EVP_DecryptInit_ex(ctx, nullptr, nullptr, aesKey.data(), iv.data())) ok = false;
    int aadLen = 0;
    if (ok && 1 != EVP_DecryptUpdate(ctx, nullptr, &aadLen, aad.data(), static_cast<int>(aad.size()))) ok = false;
    if (ok && 1 != EVP_DecryptUpdate(ctx, plaintextOut.data(), &outlen,
                                     ciphertext.data(), static_cast<int>(ciphertext.size()))) ok = false;
    if (ok && 1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()),
                                       const_cast<uint8_t*>(tag.data()))) ok = false;
    if (ok && 1 != EVP_DecryptFinal_ex(ctx, plaintextOut.data() + outlen, &tmplen)) ok = false;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) { plaintextOut.clear(); return false; }
    plaintextOut.resize(outlen + tmplen);
    return true;
}

static bool decryptFile(const std::vector<uint8_t>& encrypted,
                        const std::string& password,
                        uint8_t& formatVersionOut,
                        std::vector<uint8_t>& plaintextOut) {
    if (encrypted.size() < 2) return false;
    if (encrypted[0] != WALLET_MAGIC) return false;
    formatVersionOut = encrypted[1];
    if (formatVersionOut == WALLET_FORMAT_V3_HYBRID_GCM) {
        return decryptFileV3(encrypted, password, plaintextOut);
    }
    if (formatVersionOut != WALLET_FORMAT_V1_MNEMONIC_ONLY
        && formatVersionOut != WALLET_FORMAT_V2_HYBRID) {
        return false;
    }
    if (encrypted.size() < 2 + 16 + 16 + 32) return false;
    size_t pos = 2;
    std::vector<uint8_t> salt(encrypted.begin() + pos, encrypted.begin() + pos + 16); pos += 16;
    std::vector<uint8_t> iv(encrypted.begin() + pos, encrypted.begin() + pos + 16); pos += 16;
    if (encrypted.size() < pos + 32) return false;
    size_t hmacPos = encrypted.size() - 32;
    if (hmacPos <= pos) return false;
    std::vector<uint8_t> ciphertext(encrypted.begin() + pos, encrypted.begin() + hmacPos);
    std::vector<uint8_t> hmacStored(encrypted.begin() + hmacPos, encrypted.end());

    auto derived = pbkdf2_hmac(password, salt, 100000, 64);
    std::vector<uint8_t> aesKey(derived.begin(), derived.begin() + 32);
    std::vector<uint8_t> hmacKey(derived.begin() + 32, derived.begin() + 64);

    std::vector<uint8_t> hmacInput;
    hmacInput.reserve(1 + 1 + salt.size() + iv.size() + ciphertext.size());
    hmacInput.push_back(WALLET_MAGIC);
    hmacInput.push_back(formatVersionOut);
    hmacInput.insert(hmacInput.end(), salt.begin(), salt.end());
    hmacInput.insert(hmacInput.end(), iv.begin(), iv.end());
    hmacInput.insert(hmacInput.end(), ciphertext.begin(), ciphertext.end());

    unsigned int hmacLen = 0;
    unsigned char hmacOut[EVP_MAX_MD_SIZE];
    if (!HMAC(EVP_sha256(), hmacKey.data(), static_cast<int>(hmacKey.size()),
              hmacInput.data(), hmacInput.size(), hmacOut, &hmacLen)) {
        return false;
    }
    if (hmacLen != 32) return false;
    if (formatVersionOut == WALLET_FORMAT_V1_MNEMONIC_ONLY) {
        std::vector<uint8_t> legacyInput;
        legacyInput.reserve(salt.size() + iv.size() + ciphertext.size());
        legacyInput.insert(legacyInput.end(), salt.begin(), salt.end());
        legacyInput.insert(legacyInput.end(), iv.begin(), iv.end());
        legacyInput.insert(legacyInput.end(), ciphertext.begin(), ciphertext.end());
        unsigned int legacyLen = 0;
        unsigned char legacyOut[EVP_MAX_MD_SIZE];
        if (!HMAC(EVP_sha256(), hmacKey.data(), static_cast<int>(hmacKey.size()),
                  legacyInput.data(), legacyInput.size(), legacyOut, &legacyLen)) {
            return false;
        }
        if (legacyLen != 32) return false;
        if (CRYPTO_memcmp(hmacOut, hmacStored.data(), 32) != 0
            && CRYPTO_memcmp(legacyOut, hmacStored.data(), 32) != 0) {
            return false;
        }
    } else {
        if (CRYPTO_memcmp(hmacOut, hmacStored.data(), 32) != 0) return false;
    }

    plaintextOut.assign(ciphertext.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()), 0);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outlen = 0;
    int tmplen = 0;
    if (!ctx) return false;
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, aesKey.data(), iv.data())) { EVP_CIPHER_CTX_free(ctx); return false; }
    if (1 != EVP_DecryptUpdate(ctx, plaintextOut.data(), &outlen, ciphertext.data(), static_cast<int>(ciphertext.size()))) { EVP_CIPHER_CTX_free(ctx); return false; }
    if (1 != EVP_DecryptFinal_ex(ctx, plaintextOut.data() + outlen, &tmplen)) { EVP_CIPHER_CTX_free(ctx); return false; }
    outlen += tmplen;
    plaintextOut.resize(outlen);
    EVP_CIPHER_CTX_free(ctx);
    return true;
}

struct Wallet::Impl {
    std::vector<std::string> seedWords;
    std::vector<uint8_t> masterSeed;
    crypto::PrivateKey privateKey;
    crypto::PublicKey publicKey;
    std::string address;
    quantum::HybridKeyPair hybridKeyPair;
    AmountAtoms balance = 0;
    AmountAtoms pendingBalance = 0;
    AmountAtoms stakedBalance = 0;
    bool locked = true;
    std::string walletPath;
    mutable std::mutex mtx;

    void deriveMasterSeed();
    void deriveKeys();
    std::string deriveAddress();
    void ensureHybridKeyPair();
    void zeroHybridSecret();
};

void Wallet::Impl::deriveMasterSeed() {
    std::string mnemonic;
    for (const auto& word : seedWords) {
        if (!mnemonic.empty()) mnemonic += " ";
        mnemonic += word;
    }

    std::vector<uint8_t> salt(8);
    const char* saltPrefix = "mnemonic";
    std::memcpy(salt.data(), saltPrefix, 8);

    masterSeed = pbkdf2_hmac(mnemonic, salt, 100000, 64);
}

void Wallet::Impl::deriveKeys() {
    if (masterSeed.empty()) return;

    auto hash = crypto::sha256(masterSeed.data(), masterSeed.size());
    std::memcpy(privateKey.data(), hash.data(), std::min(hash.size(), privateKey.size()));

    publicKey = crypto::derivePublicKey(privateKey);
    address = deriveAddress();
}

std::string Wallet::Impl::deriveAddress() {
    if (publicKey.size() == 0) return "";
    return crypto::canonicalWalletAddressFromPublicKey(publicKey);
}

void Wallet::Impl::ensureHybridKeyPair() {
    const bool haveFullPair =
        hybridKeyPair.classicPublicKey.size() == WALLET_CLASSIC_PK_SIZE
        && hybridKeyPair.classicSecretKey.size() == WALLET_CLASSIC_SK_SIZE
        && hybridKeyPair.pqcPublicKey.size() == quantum::DILITHIUM_PUBLIC_KEY_SIZE
        && hybridKeyPair.pqcSecretKey.size() == quantum::DILITHIUM_SECRET_KEY_SIZE;
    if (haveFullPair) return;

    quantum::HybridSig signer;
    hybridKeyPair = signer.generateKeyPair();
}

void Wallet::Impl::zeroHybridSecret() {
    if (!hybridKeyPair.classicSecretKey.empty()) {
        OPENSSL_cleanse(hybridKeyPair.classicSecretKey.data(),
                        hybridKeyPair.classicSecretKey.size());
        hybridKeyPair.classicSecretKey.clear();
    }
    if (!hybridKeyPair.pqcSecretKey.empty()) {
        OPENSSL_cleanse(hybridKeyPair.pqcSecretKey.data(),
                        hybridKeyPair.pqcSecretKey.size());
        hybridKeyPair.pqcSecretKey.clear();
    }
    hybridKeyPair.classicPublicKey.clear();
    hybridKeyPair.pqcPublicKey.clear();
}

Wallet::Wallet() : impl_(std::make_unique<Impl>()) {}
Wallet::~Wallet() {
    lock();
}

bool Wallet::create() {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    impl_->seedWords.clear();

    auto entropy = generateRandom(32);

    for (int i = 0; i < 24; i++) {
        int idx = 0;
        for (int j = 0; j < 11; j++) {
            int bitPos = i * 11 + j;
            int bytePos = bitPos / 8;
            int bitOffset = 7 - (bitPos % 8);
            if (bytePos < 32 && (entropy[bytePos] >> bitOffset) & 1) {
                idx |= (1 << (10 - j));
            }
        }
        idx %= ::synapse::tui::BIP39_WORDLIST_SIZE;
        impl_->seedWords.push_back(::synapse::tui::BIP39_WORDLIST[idx]);
    }

    impl_->deriveMasterSeed();
    impl_->deriveKeys();
    impl_->ensureHybridKeyPair();
    impl_->locked = false;

    return true;
}

bool Wallet::restore(const std::vector<std::string>& seedWords) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    if (seedWords.size() != 24) return false;

    impl_->seedWords = seedWords;
    impl_->deriveMasterSeed();
    impl_->deriveKeys();
    impl_->ensureHybridKeyPair();
    impl_->locked = false;

    return true;
}

bool Wallet::load(const std::string& path, const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    std::vector<uint8_t> encrypted((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    file.close();
    if (encrypted.empty()) return false;

    uint8_t formatVersion = 0;
    std::vector<uint8_t> plaintext;
    if (!decryptFile(encrypted, password, formatVersion, plaintext)) {
        utils::Logger::error("wallet: decrypt failed for " + path);
        return false;
    }

    std::string mnemonic;
    quantum::HybridKeyPair loadedKeyPair;
    bool needsMigration = false;

    if (formatVersion == WALLET_FORMAT_V3_HYBRID_GCM || formatVersion == WALLET_FORMAT_V2_HYBRID) {
        if (!decodeV2Plaintext(plaintext, mnemonic, loadedKeyPair)) {
            utils::Logger::error("wallet: hybrid plaintext structure invalid for " + path);
            return false;
        }
        if (formatVersion == WALLET_FORMAT_V2_HYBRID) {
            needsMigration = true;
        }
    } else {
        mnemonic.assign(plaintext.begin(), plaintext.end());
        needsMigration = true;
    }

    impl_->seedWords.clear();
    std::string word;
    for (char c : mnemonic) {
        if (c == ' ' || c == '\n' || c == 0) {
            if (!word.empty()) { impl_->seedWords.push_back(word); word.clear(); }
        } else if (c >= 'a' && c <= 'z') {
            word += c;
        }
    }
    if (!word.empty()) impl_->seedWords.push_back(word);
    if (impl_->seedWords.size() != 24) return false;

    impl_->deriveMasterSeed();
    impl_->deriveKeys();
    impl_->walletPath = path;

    if (formatVersion == WALLET_FORMAT_V3_HYBRID_GCM || formatVersion == WALLET_FORMAT_V2_HYBRID) {
        impl_->hybridKeyPair = std::move(loadedKeyPair);
    } else {
        impl_->zeroHybridSecret();
        impl_->ensureHybridKeyPair();
    }

    impl_->locked = false;

    if (needsMigration) {
        std::string mnemonicNorm;
        for (const auto& w : impl_->seedWords) {
            if (!mnemonicNorm.empty()) mnemonicNorm += " ";
            mnemonicNorm += w;
        }
        auto upgraded = encodeV2Plaintext(mnemonicNorm, impl_->hybridKeyPair);
        if (!encryptAndWriteV3(path, upgraded, password)) {
            utils::Logger::error("wallet: lazy migrate to v3 hybrid-gcm format failed for " + path);
        }
    }

    return true;
}

bool Wallet::save(const std::string& path, const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    if (impl_->seedWords.empty()) return false;
    impl_->ensureHybridKeyPair();

    std::string mnemonic;
    for (const auto& w : impl_->seedWords) {
        if (!mnemonic.empty()) mnemonic += " ";
        mnemonic += w;
    }
    auto plaintext = encodeV2Plaintext(mnemonic, impl_->hybridKeyPair);
    if (!encryptAndWriteV3(path, plaintext, password)) return false;
    impl_->walletPath = path;
    return true;
}

void Wallet::lock() {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    OPENSSL_cleanse(impl_->masterSeed.data(), impl_->masterSeed.size());
    OPENSSL_cleanse(impl_->privateKey.data(), impl_->privateKey.size());
    impl_->masterSeed.clear();
    impl_->zeroHybridSecret();
    impl_->locked = true;
}

bool Wallet::unlock(const std::string& password) {
    return load(impl_->walletPath, password);
}

bool Wallet::isLocked() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->locked;
}

std::vector<std::string> Wallet::getSeedWords() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->seedWords;
}

std::string Wallet::getAddress() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->address;
}

std::vector<uint8_t> Wallet::getPublicKey() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return std::vector<uint8_t>(impl_->publicKey.begin(), impl_->publicKey.end());
}

bool Wallet::hasHybridKeyPair() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->locked) return false;
    return impl_->hybridKeyPair.classicSecretKey.size() == WALLET_CLASSIC_SK_SIZE
        && impl_->hybridKeyPair.pqcSecretKey.size() == quantum::DILITHIUM_SECRET_KEY_SIZE
        && impl_->hybridKeyPair.classicPublicKey.size() == WALLET_CLASSIC_PK_SIZE
        && impl_->hybridKeyPair.pqcPublicKey.size() == quantum::DILITHIUM_PUBLIC_KEY_SIZE;
}

quantum::HybridKeyPair Wallet::getHybridKeyPair() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->locked) return quantum::HybridKeyPair{};
    return impl_->hybridKeyPair;
}

std::vector<uint8_t> Wallet::getHybridClassicPublicKey() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->hybridKeyPair.classicPublicKey;
}

std::vector<uint8_t> Wallet::getHybridPqcPublicKey() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->hybridKeyPair.pqcPublicKey;
}

std::string Wallet::getHybridIdentityId() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string id;
    if (!quantum::applicationSignatureIdentityIdFromPublicKey(impl_->hybridKeyPair, id)) {
        return std::string();
    }
    return id;
}

AmountAtoms Wallet::getBalance() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->balance;
}

AmountAtoms Wallet::getPendingBalance() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->pendingBalance;
}

AmountAtoms Wallet::getStakedBalance() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->stakedBalance;
}

void Wallet::setBalance(AmountAtoms balance) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->balance = balance;
}

void Wallet::setPendingBalance(AmountAtoms pending) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->pendingBalance = pending;
}

void Wallet::setStakedBalance(AmountAtoms staked) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->stakedBalance = staked;
}

std::vector<uint8_t> Wallet::sign(const std::vector<uint8_t>& message) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    if (impl_->locked) return {};

    auto hash = crypto::sha256(message.data(), message.size());
    crypto::Hash256 h;
    std::memcpy(h.data(), hash.data(), std::min(hash.size(), h.size()));

    auto sig = crypto::sign(h, impl_->privateKey);
    return std::vector<uint8_t>(sig.begin(), sig.end());
}

bool Wallet::verify(const std::vector<uint8_t>& message, const std::vector<uint8_t>& signature,
                    const std::vector<uint8_t>& publicKey) {
    if (signature.size() != 64 || publicKey.size() != 33) return false;

    auto hash = crypto::sha256(message.data(), message.size());
    crypto::Hash256 h;
    std::memcpy(h.data(), hash.data(), std::min(hash.size(), h.size()));

    crypto::Signature sig;
    std::memcpy(sig.data(), signature.data(), std::min(signature.size(), sig.size()));

    crypto::PublicKey pub;
    std::memcpy(pub.data(), publicKey.data(), std::min(publicKey.size(), pub.size()));

    return crypto::verify(h, sig, pub);
}

}
}
