#include "core/wallet.h"
#include "crypto/address.h"
#include "crypto/crypto.h"
#include "quantum/wallet_security.h"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/evp.h>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

std::vector<uint8_t> legacyIteratedSha256Kdf(const std::string& password,
                                             const std::vector<uint8_t>& salt,
                                             int iterations) {
    std::vector<uint8_t> key(64);
    std::vector<uint8_t> block(password.begin(), password.end());
    block.insert(block.end(), salt.begin(), salt.end());

    for (int i = 0; i < iterations; ++i) {
        auto hash = synapse::crypto::sha256(block.data(), block.size());
        block.assign(hash.begin(), hash.end());
    }

    for (size_t i = 0; i < 64 && i < block.size(); ++i) {
        key[i] = block[i % block.size()];
    }
    for (size_t i = block.size(); i < 64; ++i) {
        key[i] = block[i % block.size()] ^ static_cast<uint8_t>(i);
    }

    return key;
}

std::string joinWords(const std::vector<std::string>& words) {
    std::ostringstream out;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i != 0) out << ' ';
        out << words[i];
    }
    return out.str();
}

bool writeLegacyWallet(const std::filesystem::path& path,
                       const std::vector<std::string>& seedWords,
                       const std::string& password) {
    std::vector<uint8_t> salt(16);
    for (size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<uint8_t>(i * 7 + 3);
    }

    const std::string plaintext = joinWords(seedWords);
    std::vector<uint8_t> ciphertext(plaintext.begin(), plaintext.end());
    std::vector<uint8_t> key = legacyIteratedSha256Kdf(password, salt, 2048);
    key.resize(32);
    for (size_t i = 0; i < ciphertext.size(); ++i) {
        ciphertext[i] ^= key[i % key.size()];
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(salt.data()), static_cast<std::streamsize>(salt.size()));
    file.write(reinterpret_cast<const char*>(ciphertext.data()), static_cast<std::streamsize>(ciphertext.size()));
    return file.good();
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void appendU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

std::vector<uint8_t> encodeV2Plaintext(const std::string& mnemonic,
                                       const synapse::quantum::HybridKeyPair& kp) {
    std::vector<uint8_t> out;
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

// Independent v3 writer so load still accepts AES-GCM files after v4 becomes the default save.
bool writeV3Wallet(const std::filesystem::path& path,
                   const std::vector<std::string>& seedWords,
                   const synapse::quantum::HybridKeyPair& kp,
                   const std::string& password) {
    const std::string mnemonic = joinWords(seedWords);
    const std::vector<uint8_t> plaintext = encodeV2Plaintext(mnemonic, kp);

    std::vector<uint8_t> salt(16);
    for (size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<uint8_t>(0xA0 + i);
    }
    std::vector<uint8_t> aesKey(32);
    if (!PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                           salt.data(), static_cast<int>(salt.size()),
                           600000, EVP_sha256(), 32, aesKey.data())) {
        return false;
    }
    std::vector<uint8_t> iv(12);
    for (size_t i = 0; i < iv.size(); ++i) {
        iv[i] = static_cast<uint8_t>(0xB0 + i);
    }

    std::vector<uint8_t> aad;
    aad.push_back(0xA5);
    aad.push_back(0x03);
    aad.insert(aad.end(), salt.begin(), salt.end());
    aad.insert(aad.end(), iv.begin(), iv.end());

    std::vector<uint8_t> ciphertext(plaintext.size());
    std::vector<uint8_t> tag(16);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    int outlen = 0;
    int tmplen = 0;
    int aadLen = 0;
    bool ok = true;
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)) ok = false;
    if (ok && 1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr)) ok = false;
    if (ok && 1 != EVP_EncryptInit_ex(ctx, nullptr, nullptr, aesKey.data(), iv.data())) ok = false;
    if (ok && 1 != EVP_EncryptUpdate(ctx, nullptr, &aadLen, aad.data(), static_cast<int>(aad.size()))) ok = false;
    if (ok && 1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &outlen,
                                     plaintext.data(), static_cast<int>(plaintext.size()))) ok = false;
    if (ok && 1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + outlen, &tmplen)) ok = false;
    if (ok) outlen += tmplen;
    if (ok && 1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data())) ok = false;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return false;
    ciphertext.resize(outlen);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.put(static_cast<char>(0xA5));
    file.put(static_cast<char>(0x03));
    file.write(reinterpret_cast<char*>(salt.data()), static_cast<std::streamsize>(salt.size()));
    file.write(reinterpret_cast<char*>(iv.data()), static_cast<std::streamsize>(iv.size()));
    file.write(reinterpret_cast<char*>(ciphertext.data()), static_cast<std::streamsize>(ciphertext.size()));
    file.write(reinterpret_cast<char*>(tag.data()), static_cast<std::streamsize>(tag.size()));
    return file.good();
}

}

int main() {
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("synapsenet_wallet_crypto_" + unique);
    std::filesystem::create_directories(root);

    try {
        synapse::core::Wallet wallet;
        if (!check(wallet.create(), "wallet.create failed")) return 1;
        wallet.setBalance(123456789ULL);
        wallet.setPendingBalance(234567890ULL);
        wallet.setStakedBalance(345678901ULL);
        if (!check(wallet.getBalance() == 123456789ULL, "wallet balance atoms mismatch")) return 1;
        if (!check(wallet.getPendingBalance() == 234567890ULL, "wallet pending atoms mismatch")) return 1;
        if (!check(wallet.getStakedBalance() == 345678901ULL, "wallet staked atoms mismatch")) return 1;

        const auto seedWords = wallet.getSeedWords();
        const auto address = wallet.getAddress();
        const auto hybrid = wallet.getHybridKeyPair();
        if (!check(seedWords.size() == 24, "wallet did not create 24 seed words")) return 1;
        if (!check(!address.empty(), "wallet address is empty")) return 1;
        if (!check(synapse::crypto::isCanonicalWalletAddress(address), "wallet address is not canonical ngt1 format")) return 1;

        synapse::core::Wallet restored;
        if (!check(restored.restore(seedWords), "BIP39 restore failed")) return 1;
        if (!check(restored.getSeedWords() == seedWords, "BIP39 restore seed words mismatch")) return 1;
        if (!check(restored.getAddress() == address, "BIP39 restore address mismatch")) return 1;

        if (synapse::quantum::WalletSecurity::fileKyberWrapAvailable()) {
            std::vector<uint8_t> wrapKey(32, 0x5a);
            synapse::quantum::KyberKeyPair kpA{};
            synapse::quantum::KyberKeyPair kpB{};
            if (!check(synapse::quantum::WalletSecurity::deriveFileKyberKeyPair(wrapKey, kpA),
                       "v4 Kyber wrap keypair derive failed")) return 1;
            if (!check(synapse::quantum::WalletSecurity::deriveFileKyberKeyPair(wrapKey, kpB),
                       "v4 Kyber wrap keypair re-derive failed")) return 1;
            if (!check(kpA.publicKey == kpB.publicKey && kpA.secretKey == kpB.secretKey,
                       "v4 Kyber wrap keypair is not deterministic from K")) return 1;
            std::vector<uint8_t> kemCt;
            std::vector<uint8_t> dek;
            std::vector<uint8_t> dek2;
            if (!check(synapse::quantum::WalletSecurity::encapsulateFileDek(kpA.publicKey, kemCt, dek),
                       "v4 DEK encapsulate failed")) return 1;
            if (!check(synapse::quantum::WalletSecurity::decapsulateFileDek(kemCt, kpA.secretKey, dek2),
                       "v4 DEK decapsulate failed")) return 1;
            if (!check(dek == dek2 && dek.size() == synapse::quantum::KYBER_SHARED_SECRET_SIZE,
                       "v4 DEK encaps/decaps mismatch")) return 1;
        }

        const std::filesystem::path encryptedPath = root / "wallet.dat";
        if (!check(wallet.save(encryptedPath.string(), "secret-pass"), "wallet.save failed")) return 1;

        synapse::core::Wallet loaded;
        if (!check(loaded.load(encryptedPath.string(), "secret-pass"), "wallet.load failed")) return 1;
        if (!check(loaded.getSeedWords() == seedWords, "loaded seed words mismatch")) return 1;
        if (!check(loaded.getAddress() == address, "loaded address mismatch")) return 1;

        synapse::core::Wallet wrongPassword;
        if (!check(!wrongPassword.load(encryptedPath.string(), "wrong-pass"), "wrong password unexpectedly loaded wallet")) return 1;

        std::vector<uint8_t> tampered = readBytes(encryptedPath);
        if (!check(tampered.size() > 64, "encrypted wallet file is unexpectedly small")) return 1;
        tampered.back() ^= 0x01;
        const std::filesystem::path tamperedPath = root / "wallet_tampered.dat";
        writeBytes(tamperedPath, tampered);
        synapse::core::Wallet tamperedWallet;
        if (!check(!tamperedWallet.load(tamperedPath.string(), "secret-pass"), "tampered wallet unexpectedly loaded")) return 1;

        const std::filesystem::path legacyPath = root / "wallet_legacy.dat";
        if (!check(writeLegacyWallet(legacyPath, seedWords, "legacy-pass"), "failed to write legacy wallet fixture")) return 1;
        synapse::core::Wallet legacyWallet;
        if (!check(!legacyWallet.load(legacyPath.string(), "legacy-pass"), "legacy XOR wallet should be rejected")) return 1;

        const std::filesystem::path roundTripPath = root / "wallet_roundtrip.dat";
        if (!check(wallet.save(roundTripPath.string(), "rt-pass"), "AES round-trip save failed")) return 1;
        synapse::core::Wallet reloaded;
        if (!check(reloaded.load(roundTripPath.string(), "rt-pass"), "AES round-trip load failed")) return 1;
        if (!check(reloaded.getSeedWords() == seedWords, "AES round-trip seed words mismatch")) return 1;
        if (!check(reloaded.getAddress() == address, "AES round-trip address mismatch")) return 1;
        const std::vector<uint8_t> roundTripBytes = readBytes(roundTripPath);
        if (!check(roundTripBytes.size() > 2, "AES round-trip file is too small")) return 1;
        if (!check(roundTripBytes[0] == 0xA5, "AES round-trip magic mismatch")) return 1;
        const bool kyberWrap = synapse::quantum::WalletSecurity::fileKyberWrapAvailable();
        const uint8_t expectedVersion = kyberWrap ? 0x04 : 0x03;
        if (!check(roundTripBytes[1] == expectedVersion, "AES round-trip version mismatch")) return 1;
        if (kyberWrap) {
            const size_t v4Min = 2 + 16 + synapse::quantum::KYBER_CIPHERTEXT_SIZE + 12 + 16;
            if (!check(roundTripBytes.size() >= v4Min, "v4 Kyber-GCM wallet file is too small")) return 1;
        }

        const std::filesystem::path v3Path = root / "wallet_v3.dat";
        if (!check(writeV3Wallet(v3Path, seedWords, hybrid, "v3-pass"), "failed to write v3 AES-GCM fixture")) return 1;
        const std::vector<uint8_t> v3Bytes = readBytes(v3Path);
        if (!check(v3Bytes.size() > 2 && v3Bytes[0] == 0xA5 && v3Bytes[1] == 0x03, "v3 fixture header mismatch")) return 1;
        synapse::core::Wallet v3Wrong;
        if (!check(!v3Wrong.load(v3Path.string(), "wrong-v3-pass"), "v3 wrong password unexpectedly loaded")) return 1;
        synapse::core::Wallet v3Loaded;
        if (!check(v3Loaded.load(v3Path.string(), "v3-pass"), "v3 AES-GCM wallet failed to load")) return 1;
        if (!check(v3Loaded.getSeedWords() == seedWords, "v3 round-trip seed words mismatch")) return 1;
        if (!check(v3Loaded.getAddress() == address, "v3 round-trip address mismatch")) return 1;
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }

    std::filesystem::remove_all(root);
    std::cout << "wallet crypto runtime tests passed\n";
    return 0;
}
