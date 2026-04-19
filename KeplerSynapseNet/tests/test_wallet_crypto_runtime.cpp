#include "core/wallet.h"
#include "crypto/address.h"
#include "crypto/crypto.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
        if (!check(seedWords.size() == 24, "wallet did not create 24 seed words")) return 1;
        if (!check(!address.empty(), "wallet address is empty")) return 1;
        if (!check(synapse::crypto::isCanonicalWalletAddress(address), "wallet address is not canonical ngt1 format")) return 1;

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
        if (!check(roundTripBytes[1] == 0x01, "AES round-trip version mismatch")) return 1;
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }

    std::filesystem::remove_all(root);
    std::cout << "wallet crypto runtime tests passed\n";
    return 0;
}
