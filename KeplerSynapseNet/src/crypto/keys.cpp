#include "crypto/keys.h"
#include "crypto/address.h"
#include "crypto/crypto.h"
#include "tui/bip39_wordlist.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <set>
#include <random>
#include <openssl/evp.h>

#ifdef SYNAPSE_USE_SECP256K1
#include <secp256k1.h>
#include <secp256k1_ecdh.h>
#endif

namespace synapse {
namespace crypto {

static const std::vector<std::string>& bip39Wordlist() {
    static std::vector<std::string> list = [] {
        std::vector<std::string> words;
        words.reserve(tui::BIP39_WORDLIST_SIZE);
        for (int i = 0; i < tui::BIP39_WORDLIST_SIZE; i++) {
            words.emplace_back(tui::BIP39_WORDLIST[i]);
        }
        return words;
    }();
    return list;
}

#ifdef SYNAPSE_USE_SECP256K1
static secp256k1_context* ecdhContext() {
    static secp256k1_context* ctx = secp256k1_context_create(
        SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    return ctx;
}
#endif

struct Keys::Impl {
    std::vector<uint8_t> publicKey;
    std::vector<uint8_t> privateKey;
    std::vector<uint8_t> seed;
    std::string mnemonic;
    KeyType keyType = KeyType::SECP256K1;
    bool valid = false;
    
    void derivePublicKey();
    std::vector<uint8_t> hmacSha512(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);
};

void Keys::Impl::derivePublicKey() {
    if (privateKey.size() != PRIVATE_KEY_SIZE) return;
    PrivateKey sk{};
    std::memcpy(sk.data(), privateKey.data(), PRIVATE_KEY_SIZE);
    PublicKey pk = synapse::crypto::derivePublicKey(sk);
    publicKey.assign(pk.begin(), pk.end());
}

std::vector<uint8_t> Keys::Impl::hmacSha512(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
    return synapse::crypto::hmacSha512(key, data);
}

Keys::Keys() : impl_(std::make_unique<Impl>()) {}
Keys::~Keys() { wipe(); }

bool Keys::generate(KeyType type) {
    impl_->keyType = type;
    impl_->privateKey.resize(32);
    impl_->seed.resize(64);

    for (int attempt = 0; attempt < 1000; ++attempt) {
        auto rnd = randomBytes(32);
        std::memcpy(impl_->privateKey.data(), rnd.data(), 32);
        impl_->derivePublicKey();
        bool allZero = true;
        for (auto b : impl_->publicKey) {
            if (b != 0) { allZero = false; break; }
        }
        if (!allZero) {
            auto rnd2 = randomBytes(64);
            std::memcpy(impl_->seed.data(), rnd2.data(), 64);
            impl_->valid = true;
            return true;
        }
    }

    impl_->valid = false;
    return false;
}

bool Keys::fromSeed(const std::vector<uint8_t>& seed) {
    if (seed.size() < 16 || seed.size() > 64) return false;
    
    impl_->seed = seed;
    
    std::vector<uint8_t> key(13);
    std::memcpy(key.data(), "Bitcoin seed", 12);
    auto derived = impl_->hmacSha512(key, seed);
    
    impl_->privateKey.assign(derived.begin(), derived.begin() + 32);

    for (int attempt = 0; attempt < 1000; ++attempt) {
        impl_->derivePublicKey();
        bool allZero = true;
        for (auto b : impl_->publicKey) {
            if (b != 0) { allZero = false; break; }
        }
        if (!allZero) {
            impl_->valid = true;
            return true;
        }
        Hash256 h = sha256(impl_->privateKey.data(), impl_->privateKey.size());
        impl_->privateKey.assign(h.begin(), h.end());
    }

    impl_->valid = false;
    return false;
}

bool Keys::fromMnemonic(const std::string& mnemonic) {
    if (!validateMnemonic(mnemonic)) return false;
    
    impl_->mnemonic = mnemonic;
    // Derive seed using PBKDF2-HMAC-SHA512 per BIP39 (100000 iterations)
    impl_->seed.resize(64);
    const unsigned char* salt = reinterpret_cast<const unsigned char*>("mnemonic");
    const int saltLen = 8; // length of "mnemonic"
    if (1 != PKCS5_PBKDF2_HMAC(mnemonic.c_str(), static_cast<int>(mnemonic.size()),
                               salt, saltLen, 100000, EVP_sha512(), 64, impl_->seed.data())) {
        return false;
    }

    return fromSeed(impl_->seed);
}

bool Keys::fromPrivateKey(const std::vector<uint8_t>& privateKey) {
    if (privateKey.size() != 32) return false;
    impl_->privateKey = privateKey;
    impl_->derivePublicKey();
    bool allZero = true;
    for (auto b : impl_->publicKey) {
        if (b != 0) { allZero = false; break; }
    }
    impl_->valid = !allZero;
    return impl_->valid;
}

std::string Keys::generateMnemonic(uint32_t wordCount) {
    if (wordCount != 12 && wordCount != 15 && wordCount != 18 && 
        wordCount != 21 && wordCount != 24) {
        wordCount = 24;
    }
    
    uint32_t entropyBits = wordCount * 11 - wordCount / 3;
    uint32_t entropyBytes = entropyBits / 8;
    
    auto entropy = randomBytes(entropyBytes);
    Hash256 hash = sha256(entropy.data(), entropy.size());
    
    std::vector<bool> bits;
    for (uint8_t byte : entropy) {
        for (int i = 7; i >= 0; i--) {
            bits.push_back((byte >> i) & 1);
        }
    }
    
    uint32_t checksumBits = entropyBytes / 4;
    for (uint32_t i = 0; i < checksumBits; i++) {
        bits.push_back((hash[i / 8] >> (7 - (i % 8))) & 1);
    }
    
    std::vector<std::string> mnemonicWords;
    for (size_t i = 0; i < bits.size(); i += 11) {
        uint32_t idx = 0;
        for (int j = 0; j < 11 && i + j < bits.size(); j++) {
            idx = (idx << 1) | bits[i + j];
        }
        const auto& list = bip39Wordlist();
        mnemonicWords.push_back(list[idx % list.size()]);
    }
    
    std::string result;
    for (size_t i = 0; i < mnemonicWords.size(); i++) {
        if (i > 0) result += " ";
        result += mnemonicWords[i];
    }
    
    impl_->mnemonic = result;
    return result;
}

std::string Keys::toMnemonic() const {
    return impl_->mnemonic;
}

bool Keys::validateMnemonic(const std::string& mnemonic) {
    std::istringstream iss(mnemonic);
    std::string word;
    std::vector<std::string> mnemonicWords;
    const auto& list = bip39Wordlist();
    std::set<std::string> wordSet(list.begin(), list.end());
    
    while (iss >> word) {
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);
        if (wordSet.find(word) == wordSet.end()) return false;
        mnemonicWords.push_back(word);
    }
    
    return mnemonicWords.size() == 12 || mnemonicWords.size() == 15 || 
           mnemonicWords.size() == 18 || mnemonicWords.size() == 21 || mnemonicWords.size() == 24;
}

std::vector<uint8_t> Keys::getPublicKey() const {
    return impl_->publicKey;
}

// SecureBuffer implementation
Keys::SecureBuffer::SecureBuffer(std::vector<uint8_t> data) : buf_(std::move(data)) {}

Keys::SecureBuffer::~SecureBuffer() {
    if (!buf_.empty()) {
        secureZero(buf_.data(), buf_.size());
        buf_.clear();
    }
}

const uint8_t* Keys::SecureBuffer::data() const { return buf_.data(); }
size_t Keys::SecureBuffer::size() const { return buf_.size(); }

Keys::SecureBuffer::SecureBuffer(Keys::SecureBuffer&& other) noexcept : buf_(std::move(other.buf_)) {
    other.buf_.clear();
}

Keys::SecureBuffer& Keys::SecureBuffer::operator=(Keys::SecureBuffer&& other) noexcept {
    if (this != &other) {
        if (!buf_.empty()) secureZero(buf_.data(), buf_.size());
        buf_ = std::move(other.buf_);
        other.buf_.clear();
    }
    return *this;
}

Keys::SecureBuffer Keys::getPrivateKey() const {
    return Keys::SecureBuffer(impl_->privateKey);
}

std::vector<uint8_t> Keys::getSeed() const {
    return impl_->seed;
}

KeyType Keys::getKeyType() const {
    return impl_->keyType;
}

std::string Keys::getAddress() const {
    if (impl_->publicKey.size() < PUBLIC_KEY_SIZE) return "";
    PublicKey publicKey{};
    std::memcpy(publicKey.data(), impl_->publicKey.data(), publicKey.size());
    return canonicalWalletAddressFromPublicKey(publicKey);
}
DerivedKey Keys::deriveChild(uint32_t index) {
    DerivedKey derived;
    derived.index = index;
    derived.path = "m/" + std::to_string(index);
    
    std::vector<uint8_t> data;
    data.push_back(0x00);
    data.insert(data.end(), impl_->privateKey.begin(), impl_->privateKey.end());
    data.push_back((index >> 24) & 0xff);
    data.push_back((index >> 16) & 0xff);
    data.push_back((index >> 8) & 0xff);
    data.push_back(index & 0xff);
    
    auto hmac = impl_->hmacSha512(impl_->seed, data);
    derived.key.assign(hmac.begin(), hmac.begin() + 32);
    
    return derived;
}

DerivedKey Keys::derivePath(const std::string& path) {
    DerivedKey result;
    result.path = path;
    result.key = impl_->privateKey;
    
    std::istringstream iss(path);
    std::string segment;
    uint32_t idx = 0;
    
    while (std::getline(iss, segment, '/')) {
        if (segment == "m") continue;
        bool hardened = segment.back() == '\'';
        if (hardened) segment.pop_back();
        uint32_t index = std::stoul(segment);
        if (hardened) index += 0x80000000;
        
        auto child = deriveChild(index);
        result.key = child.key;
        result.index = index;
        idx++;
    }
    
    return result;
}

std::vector<DerivedKey> Keys::deriveRange(uint32_t start, uint32_t count) {
    std::vector<DerivedKey> keys;
    for (uint32_t i = 0; i < count; i++) {
        keys.push_back(deriveChild(start + i));
    }
    return keys;
}

std::vector<uint8_t> Keys::sign(const std::vector<uint8_t>& message) const {
    if (impl_->privateKey.empty()) return {};
    
    Hash256 hash = doubleSha256(message.data(), message.size());
    PrivateKey privKey;
    std::memcpy(privKey.data(), impl_->privateKey.data(), 32);
    
    Signature sig = synapse::crypto::sign(hash, privKey);
    return std::vector<uint8_t>(sig.begin(), sig.end());
}

bool Keys::verify(const std::vector<uint8_t>& message, 
                  const std::vector<uint8_t>& signature) const {
    if (impl_->publicKey.empty() || signature.size() != 64) return false;
    
    Hash256 hash = doubleSha256(message.data(), message.size());
    Signature sig;
    PublicKey pubKey;
    
    std::memcpy(sig.data(), signature.data(), 64);
    std::memcpy(pubKey.data(), impl_->publicKey.data(), std::min(size_t(33), impl_->publicKey.size()));
    
    return synapse::crypto::verify(hash, sig, pubKey);
}

std::vector<uint8_t> Keys::sharedSecret(const std::vector<uint8_t>& otherPublicKey) const {
    if (impl_->keyType != KeyType::SECP256K1) return {};
    if (impl_->privateKey.size() != PRIVATE_KEY_SIZE) return {};
    if (otherPublicKey.size() != PUBLIC_KEY_SIZE && otherPublicKey.size() != 65) return {};

#ifdef SYNAPSE_USE_SECP256K1
    secp256k1_context* ctx = ecdhContext();
    std::array<uint8_t, PRIVATE_KEY_SIZE> privateKey{};
    std::array<uint8_t, SHA256_SIZE> secret{};
    secp256k1_pubkey peerPubKey{};
    std::memcpy(privateKey.data(), impl_->privateKey.data(), privateKey.size());

    const bool ok =
        secp256k1_ec_seckey_verify(ctx, privateKey.data()) == 1 &&
        secp256k1_ec_pubkey_parse(ctx, &peerPubKey, otherPublicKey.data(), otherPublicKey.size()) == 1 &&
        secp256k1_ecdh(ctx, secret.data(), &peerPubKey, privateKey.data(), nullptr, nullptr) == 1;

    secureZero(privateKey.data(), privateKey.size());
    secureZero(&peerPubKey, sizeof(peerPubKey));

    if (!ok) {
        secureZero(secret.data(), secret.size());
        return {};
    }

    std::vector<uint8_t> out(secret.begin(), secret.end());
    secureZero(secret.data(), secret.size());
    return out;
#else
    return {};
#endif
}

bool Keys::save(const std::string& path, const std::string& password) {
    std::vector<uint8_t> encrypted;
    if (!exportEncrypted(encrypted, password)) return false;
    
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    
    file.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
    return true;
}

bool Keys::load(const std::string& path, const std::string& password) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    size_t size = file.tellg();
    file.seekg(0);
    
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    return importEncrypted(data, password);
}

bool Keys::exportEncrypted(std::vector<uint8_t>& output, const std::string& password) {
    if (impl_->privateKey.empty()) return false;
    
    auto salt = randomBytes(16);
    auto keyArr = deriveKey(password, salt);

    std::vector<uint8_t> plaintext(impl_->privateKey.begin(), impl_->privateKey.end());
    auto ciphertext = crypto::encryptAES(plaintext, keyArr);
    if (ciphertext.empty()) return false;

    output.clear();
    output.insert(output.end(), salt.begin(), salt.end());
    output.insert(output.end(), ciphertext.begin(), ciphertext.end());
    return true;
}

bool Keys::importEncrypted(const std::vector<uint8_t>& data, const std::string& password) {
    // minimum: 16 salt + 12 IV + 16 tag = 44
    if (data.size() < 44) return false;

    std::vector<uint8_t> salt(data.begin(), data.begin() + 16);
    auto keyArr = deriveKey(password, salt);

    std::vector<uint8_t> ciphertext(data.begin() + 16, data.end());
    auto decrypted = crypto::decryptAES(ciphertext, keyArr);
    if (decrypted.empty()) return false;

    impl_->privateKey = std::move(decrypted);
    impl_->derivePublicKey();
    bool allZero = true;
    for (auto b : impl_->publicKey) {
        if (b != 0) { allZero = false; break; }
    }
    impl_->valid = !allZero;
    return impl_->valid;
}

void Keys::wipe() {
    if (!impl_->privateKey.empty()) {
        secureZero(impl_->privateKey.data(), impl_->privateKey.size());
        impl_->privateKey.clear();
    }
    if (!impl_->seed.empty()) {
        secureZero(impl_->seed.data(), impl_->seed.size());
        impl_->seed.clear();
    }
    impl_->publicKey.clear();
    if (!impl_->mnemonic.empty()) {
        secureZero(const_cast<char*>(impl_->mnemonic.data()), impl_->mnemonic.size());
        impl_->mnemonic.clear();
    }
    impl_->valid = false;
}

bool Keys::isValid() const {
    return impl_->valid && !impl_->privateKey.empty() && !impl_->publicKey.empty();
}

std::vector<std::string> Keys::getWordlist() {
    return bip39Wordlist();
}

bool Keys::isValidWord(const std::string& word) {
    std::string lower = word;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    const auto& words = bip39Wordlist();
    return std::find(words.begin(), words.end(), lower) != words.end();
}

}
}
