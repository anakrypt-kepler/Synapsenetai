#include "privacy/privacy.h"
#include "crypto/crypto.h"
#include <cstring>
#include <random>

namespace synapse {
namespace privacy {

struct StealthAddress::Impl {
    std::vector<uint8_t> viewKey;
    std::vector<uint8_t> spendKey;
    std::vector<uint8_t> viewPublic;
    std::vector<uint8_t> spendPublic;
    std::mt19937_64 rng;
    
    Impl() : rng(std::random_device{}()) {}
    
    bool generateKeyPair(std::vector<uint8_t>& secretKey, std::vector<uint8_t>& publicKey);
    std::vector<uint8_t> deriveSharedSecret(const std::vector<uint8_t>& secretKey,
                                             const std::vector<uint8_t>& publicKey);
};

bool StealthAddress::Impl::generateKeyPair(std::vector<uint8_t>& secretKey,
                                            std::vector<uint8_t>& publicKey) {
    secretKey.resize(32);
    for (size_t i = 0; i < 32; i++) {
        secretKey[i] = rng() & 0xFF;
    }
    
    auto hash = crypto::sha256(secretKey.data(), secretKey.size());
    publicKey.assign(hash.begin(), hash.end());
    
    return true;
}

std::vector<uint8_t> StealthAddress::Impl::deriveSharedSecret(const std::vector<uint8_t>& secretKey,
                                                               const std::vector<uint8_t>& publicKey) {
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), secretKey.begin(), secretKey.end());
    combined.insert(combined.end(), publicKey.begin(), publicKey.end());
    
    auto hash = crypto::sha256(combined.data(), combined.size());
    return std::vector<uint8_t>(hash.begin(), hash.end());
}

StealthAddress::StealthAddress() : impl_(std::make_unique<Impl>()) {}
StealthAddress::~StealthAddress() = default;

bool StealthAddress::generateKeys() {
    if (!impl_->generateKeyPair(impl_->viewKey, impl_->viewPublic)) {
        return false;
    }
    
    if (!impl_->generateKeyPair(impl_->spendKey, impl_->spendPublic)) {
        return false;
    }
    
    return true;
}

std::vector<uint8_t> StealthAddress::getViewPublicKey() const {
    return impl_->viewPublic;
}

std::vector<uint8_t> StealthAddress::getSpendPublicKey() const {
    return impl_->spendPublic;
}

std::vector<uint8_t> StealthAddress::generateOneTimeAddress(const std::vector<uint8_t>& recipientViewPub,
                                                             const std::vector<uint8_t>& recipientSpendPub,
                                                             std::vector<uint8_t>& ephemeralPub) {
    std::vector<uint8_t> ephemeralSecret;
    impl_->generateKeyPair(ephemeralSecret, ephemeralPub);
    
    std::vector<uint8_t> sharedSecret = impl_->deriveSharedSecret(ephemeralSecret, recipientViewPub);
    
    auto derivedKey = crypto::sha256(sharedSecret.data(), sharedSecret.size());
    
    std::vector<uint8_t> oneTimeAddress(32);
    for (int i = 0; i < 32; i++) {
        oneTimeAddress[i] = recipientSpendPub[i] ^ derivedKey[i];
    }
    
    return oneTimeAddress;
}

bool StealthAddress::checkOwnership(const std::vector<uint8_t>& oneTimeAddress,
                                     const std::vector<uint8_t>& ephemeralPub) const {
    std::vector<uint8_t> sharedSecret = impl_->deriveSharedSecret(impl_->viewKey, ephemeralPub);
    
    auto derivedKey = crypto::sha256(sharedSecret.data(), sharedSecret.size());
    
    std::vector<uint8_t> expectedAddress(32);
    for (int i = 0; i < 32; i++) {
        expectedAddress[i] = impl_->spendPublic[i] ^ derivedKey[i];
    }
    
    return std::memcmp(oneTimeAddress.data(), expectedAddress.data(), 32) == 0;
}

std::vector<uint8_t> StealthAddress::deriveSpendingKey(const std::vector<uint8_t>& ephemeralPub) const {
    std::vector<uint8_t> sharedSecret = impl_->deriveSharedSecret(impl_->viewKey, ephemeralPub);
    
    auto derivedKey = crypto::sha256(sharedSecret.data(), sharedSecret.size());
    
    std::vector<uint8_t> spendingKey(32);
    for (int i = 0; i < 32; i++) {
        spendingKey[i] = impl_->spendKey[i] ^ derivedKey[i];
    }
    
    return spendingKey;
}

std::string StealthAddress::encodeAddress() const {
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), impl_->viewPublic.begin(), impl_->viewPublic.end());
    combined.insert(combined.end(), impl_->spendPublic.begin(), impl_->spendPublic.end());
    
    return "SN" + crypto::toHex(combined);
}

bool StealthAddress::decodeAddress(const std::string& address,
                                    std::vector<uint8_t>& viewPub,
                                    std::vector<uint8_t>& spendPub) {
    if (address.size() < 130 || address.substr(0, 2) != "SN") {
        return false;
    }
    
    std::string hex = address.substr(2);
    std::vector<uint8_t> combined = crypto::fromHex(hex);
    
    if (combined.size() != 64) return false;
    
    viewPub.assign(combined.begin(), combined.begin() + 32);
    spendPub.assign(combined.begin() + 32, combined.end());
    
    return true;
}

}
}
