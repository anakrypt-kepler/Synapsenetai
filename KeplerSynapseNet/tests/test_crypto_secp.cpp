#include "crypto/crypto.h"
#include "crypto/keys.h"

#include <cassert>
#include <algorithm>
#include <string>
#include <vector>

static bool isAllZero(const synapse::crypto::Signature& sig) {
    return std::all_of(sig.begin(), sig.end(), [](uint8_t b) { return b == 0; });
}

static bool isAllZero(const synapse::crypto::PublicKey& pub) {
    return std::all_of(pub.begin(), pub.end(), [](uint8_t b) { return b == 0; });
}

static void testDeriveSignVerifyAndTamper() {
    auto hash = synapse::crypto::sha256(std::string("poe.real.crypto"));
    auto keyPair = synapse::crypto::generateKeyPair();

    auto derived = synapse::crypto::derivePublicKey(keyPair.privateKey);
    assert(!isAllZero(derived));
    assert(derived == keyPair.publicKey);

    auto sig = synapse::crypto::sign(hash, keyPair.privateKey);
    assert(!isAllZero(sig));
    assert(synapse::crypto::verify(hash, sig, keyPair.publicKey));

    auto tamperedHash = hash;
    tamperedHash[0] ^= 0x01;
    assert(!synapse::crypto::verify(tamperedHash, sig, keyPair.publicKey));

    auto tamperedSig = sig;
    tamperedSig[7] ^= 0x80;
    assert(!synapse::crypto::verify(hash, tamperedSig, keyPair.publicKey));

    auto otherKeyPair = synapse::crypto::generateKeyPair();
    assert(!synapse::crypto::verify(hash, sig, otherKeyPair.publicKey));
}

static void testInvalidPrivateKeyRejected() {
    synapse::crypto::PrivateKey invalid{};
    auto hash = synapse::crypto::sha256(std::string("invalid-seckey"));
    auto sig = synapse::crypto::sign(hash, invalid);
    assert(isAllZero(sig));
}

static void testHexParsingIsStrict() {
    auto ok = synapse::crypto::fromHex("00AaFf");
    assert(ok.size() == 3);
    assert(ok[0] == 0x00);
    assert(ok[1] == 0xAA);
    assert(ok[2] == 0xFF);

    assert(synapse::crypto::fromHex("0").empty());
    assert(synapse::crypto::fromHex("zz").empty());
    assert(synapse::crypto::fromHex("12xz").empty());
}

static void testAesGcmTamperRejected() {
    std::array<uint8_t, synapse::crypto::AES_KEY_SIZE> key{};
    for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i + 1);

    const std::vector<uint8_t> plaintext = {'s', 'e', 'c', 'u', 'r', 'e'};
    const auto encrypted = synapse::crypto::encryptAES(plaintext, key);
    assert(encrypted.size() >= plaintext.size() + 12 + 16);

    auto decrypted = synapse::crypto::decryptAES(encrypted, key);
    assert(decrypted == plaintext);

    auto tamperedTag = encrypted;
    tamperedTag.back() ^= 0x01;
    auto badTagDecrypt = synapse::crypto::decryptAES(tamperedTag, key);
    assert(badTagDecrypt.empty());

    auto tamperedCiphertext = encrypted;
    tamperedCiphertext[12] ^= 0x01;
    auto badCiphertextDecrypt = synapse::crypto::decryptAES(tamperedCiphertext, key);
    assert(badCiphertextDecrypt.empty());
}

static void testSecp256k1SharedSecretSymmetry() {
    auto kpA = synapse::crypto::generateKeyPair();
    auto kpB = synapse::crypto::generateKeyPair();

    synapse::crypto::Keys keysA;
    synapse::crypto::Keys keysB;
    assert(keysA.fromPrivateKey(std::vector<uint8_t>(kpA.privateKey.begin(), kpA.privateKey.end())));
    assert(keysB.fromPrivateKey(std::vector<uint8_t>(kpB.privateKey.begin(), kpB.privateKey.end())));

    auto sharedAB = keysA.sharedSecret(keysB.getPublicKey());
    auto sharedBA = keysB.sharedSecret(keysA.getPublicKey());

    assert(sharedAB.size() == synapse::crypto::SHA256_SIZE);
    assert(sharedBA.size() == synapse::crypto::SHA256_SIZE);
    assert(sharedAB == sharedBA);

    auto badInput = keysA.sharedSecret({0x01, 0x02, 0x03});
    assert(badInput.empty());
}

int main() {
    testDeriveSignVerifyAndTamper();
    testInvalidPrivateKeyRejected();
    testHexParsingIsStrict();
    testAesGcmTamperRejected();
    testSecp256k1SharedSecretSymmetry();
    return 0;
}
