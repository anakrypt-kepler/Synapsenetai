#include "core/wallet.h"
#include "crypto/address.h"
#include "crypto/crypto.h"
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

static bool saveEncryptedSeedWords(const std::string& path,
                                   const std::vector<std::string>& seedWords,
                                   const std::string& password) {
    if (seedWords.empty()) return false;

    std::string plaintext;
    for (const auto& word : seedWords) {
        if (!plaintext.empty()) plaintext += " ";
        plaintext += word;
    }

    const uint8_t magic = 0xA5;
    const uint8_t version = 1;
    auto salt = generateRandom(16);
    auto derived = pbkdf2_hmac(password, salt, 100000, 64);
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
    if (1 != EVP_EncryptUpdate(ctx,
                               ciphertext.data(),
                               &outlen,
                               reinterpret_cast<const unsigned char*>(plaintext.data()),
                               static_cast<int>(plaintext.size()))) {
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
    hmacInput.reserve(salt.size() + iv.size() + ciphertext.size());
    hmacInput.insert(hmacInput.end(), salt.begin(), salt.end());
    hmacInput.insert(hmacInput.end(), iv.begin(), iv.end());
    hmacInput.insert(hmacInput.end(), ciphertext.begin(), ciphertext.end());

    unsigned int hmacLen = 0;
    unsigned char hmacOut[EVP_MAX_MD_SIZE];
    if (!HMAC(EVP_sha256(),
              hmacKey.data(),
              static_cast<int>(hmacKey.size()),
              hmacInput.data(),
              static_cast<int>(hmacInput.size()),
              hmacOut,
              &hmacLen)) {
        return false;
    }
    if (hmacLen != 32) return false;

    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.put(static_cast<char>(magic));
    file.put(static_cast<char>(version));
    file.write(reinterpret_cast<char*>(salt.data()), salt.size());
    file.write(reinterpret_cast<char*>(iv.data()), iv.size());
    file.write(reinterpret_cast<char*>(ciphertext.data()), ciphertext.size());
    file.write(reinterpret_cast<char*>(hmacOut), 32);
    return file.good();
}

struct Wallet::Impl {
    std::vector<std::string> seedWords;
    std::vector<uint8_t> masterSeed;
    crypto::PrivateKey privateKey;
    crypto::PublicKey publicKey;
    std::string address;
    AmountAtoms balance = 0;
    AmountAtoms pendingBalance = 0;
    AmountAtoms stakedBalance = 0;
    bool locked = true;
    std::string walletPath;
    mutable std::mutex mtx;
    
    void deriveMasterSeed();
    void deriveKeys();
    std::string deriveAddress();
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
    impl_->locked = false;
    
    return true;
}

bool Wallet::restore(const std::vector<std::string>& seedWords) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (seedWords.size() != 24) return false;
    
    impl_->seedWords = seedWords;
    impl_->deriveMasterSeed();
    impl_->deriveKeys();
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

    const uint8_t MAGIC = 0xA5;
    if (encrypted.size() >= 2 && encrypted[0] == MAGIC && encrypted[1] == 1) {
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
        hmacInput.reserve(salt.size() + iv.size() + ciphertext.size());
        hmacInput.insert(hmacInput.end(), salt.begin(), salt.end());
        hmacInput.insert(hmacInput.end(), iv.begin(), iv.end());
        hmacInput.insert(hmacInput.end(), ciphertext.begin(), ciphertext.end());

        unsigned int hmacLen = 0;
        unsigned char hmacOut[EVP_MAX_MD_SIZE];
        if (!HMAC(EVP_sha256(), hmacKey.data(), static_cast<int>(hmacKey.size()), hmacInput.data(), static_cast<int>(hmacInput.size()), hmacOut, &hmacLen)) return false;
        if (hmacLen != 32) return false;
        if (CRYPTO_memcmp(hmacOut, hmacStored.data(), 32) != 0) return false;

        std::vector<uint8_t> plaintext;
        plaintext.resize(ciphertext.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        int outlen = 0;
        int tmplen = 0;
        if (!ctx) return false;
        if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, aesKey.data(), iv.data())) { EVP_CIPHER_CTX_free(ctx); return false; }
        if (1 != EVP_DecryptUpdate(ctx, plaintext.data(), &outlen, ciphertext.data(), static_cast<int>(ciphertext.size()))) { EVP_CIPHER_CTX_free(ctx); return false; }
        if (1 != EVP_DecryptFinal_ex(ctx, plaintext.data() + outlen, &tmplen)) { EVP_CIPHER_CTX_free(ctx); return false; }
        outlen += tmplen;
        plaintext.resize(outlen);
        EVP_CIPHER_CTX_free(ctx);

        impl_->seedWords.clear();
        std::string word;
        for (uint8_t c : plaintext) {
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
        impl_->locked = false;
        return true;
    }

    utils::Logger::error("Legacy XOR wallet format is no longer supported. Please use wallet migration tool.");
    return false;
}

bool Wallet::save(const std::string& path, const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->seedWords.empty()) return false;
    if (!saveEncryptedSeedWords(path, impl_->seedWords, password)) return false;
    impl_->walletPath = path;
    return true;
}

void Wallet::lock() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    OPENSSL_cleanse(impl_->masterSeed.data(), impl_->masterSeed.size());
    OPENSSL_cleanse(impl_->privateKey.data(), impl_->privateKey.size());
    impl_->masterSeed.clear();
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
