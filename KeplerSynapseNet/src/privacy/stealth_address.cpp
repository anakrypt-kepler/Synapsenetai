#include "privacy/privacy.h"
#include "crypto/crypto.h"
#include <cstring>
#include <random>
#include <sodium.h>

namespace synapse {
namespace privacy {

struct StealthAddress::Impl {
    std::vector<uint8_t> viewKey;
    std::vector<uint8_t> spendKey;
    std::vector<uint8_t> viewPublic;
    std::vector<uint8_t> spendPublic;

    bool generateScalarKeyPair(std::vector<uint8_t>& secretScalar, std::vector<uint8_t>& publicPoint);
    bool computeSharedSecret(const std::vector<uint8_t>& secretScalar,
                             const std::vector<uint8_t>& publicPoint,
                             std::vector<uint8_t>& sharedPoint) const;
    bool hashToScalar(const std::vector<uint8_t>& data, std::vector<uint8_t>& scalar) const;
    bool oneTimeFromSecret(const std::vector<uint8_t>& sharedPoint,
                           const std::vector<uint8_t>& spendPub,
                           std::vector<uint8_t>& oneTime) const;
};

bool StealthAddress::Impl::generateScalarKeyPair(std::vector<uint8_t>& secretScalar,
                                                 std::vector<uint8_t>& publicPoint) {
    unsigned char seed[crypto_core_ed25519_NONREDUCEDSCALARBYTES];
    randombytes_buf(seed, sizeof(seed));

    secretScalar.resize(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_reduce(secretScalar.data(), seed);
    sodium_memzero(seed, sizeof(seed));

    publicPoint.resize(crypto_core_ed25519_BYTES);
    if (crypto_scalarmult_ed25519_base_noclamp(publicPoint.data(), secretScalar.data()) != 0) {
        return false;
    }

    return true;
}

bool StealthAddress::Impl::computeSharedSecret(const std::vector<uint8_t>& secretScalar,
                                               const std::vector<uint8_t>& publicPoint,
                                               std::vector<uint8_t>& sharedPoint) const {
    if (secretScalar.size() != crypto_core_ed25519_SCALARBYTES ||
        publicPoint.size() != crypto_core_ed25519_BYTES) {
        return false;
    }

    sharedPoint.resize(crypto_core_ed25519_BYTES);
    if (crypto_scalarmult_ed25519_noclamp(sharedPoint.data(),
                                          secretScalar.data(),
                                          publicPoint.data()) != 0) {
        return false;
    }

    return true;
}

bool StealthAddress::Impl::hashToScalar(const std::vector<uint8_t>& data,
                                        std::vector<uint8_t>& scalar) const {
    auto first = crypto::sha256(data.data(), data.size());

    std::vector<uint8_t> doubled;
    doubled.insert(doubled.end(), first.begin(), first.end());
    auto second = crypto::sha256(first.data(), first.size());
    doubled.insert(doubled.end(), second.begin(), second.end());

    unsigned char wide[crypto_core_ed25519_NONREDUCEDSCALARBYTES];
    std::memcpy(wide, doubled.data(), crypto_core_ed25519_NONREDUCEDSCALARBYTES);

    scalar.resize(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_reduce(scalar.data(), wide);
    sodium_memzero(wide, sizeof(wide));

    return true;
}

bool StealthAddress::Impl::oneTimeFromSecret(const std::vector<uint8_t>& sharedPoint,
                                             const std::vector<uint8_t>& spendPub,
                                             std::vector<uint8_t>& oneTime) const {
    if (spendPub.size() != crypto_core_ed25519_BYTES) {
        return false;
    }

    std::vector<uint8_t> scalar;
    if (!hashToScalar(sharedPoint, scalar)) {
        return false;
    }

    std::vector<uint8_t> scalarPoint(crypto_core_ed25519_BYTES);
    if (crypto_scalarmult_ed25519_base_noclamp(scalarPoint.data(), scalar.data()) != 0) {
        return false;
    }

    oneTime.resize(crypto_core_ed25519_BYTES);
    if (crypto_core_ed25519_add(oneTime.data(), scalarPoint.data(), spendPub.data()) != 0) {
        return false;
    }

    return true;
}

StealthAddress::StealthAddress() : impl_(std::make_unique<Impl>()) {}
StealthAddress::~StealthAddress() = default;

bool StealthAddress::generateKeys() {
    if (sodium_init() < 0) {
        return false;
    }

    if (!impl_->generateScalarKeyPair(impl_->viewKey, impl_->viewPublic)) {
        return false;
    }

    if (!impl_->generateScalarKeyPair(impl_->spendKey, impl_->spendPublic)) {
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
    if (sodium_init() < 0) {
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> ephemeralSecret;
    if (!impl_->generateScalarKeyPair(ephemeralSecret, ephemeralPub)) {
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> sharedPoint;
    if (!impl_->computeSharedSecret(ephemeralSecret, recipientViewPub, sharedPoint)) {
        sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());
        return std::vector<uint8_t>();
    }
    sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());

    std::vector<uint8_t> oneTime;
    if (!impl_->oneTimeFromSecret(sharedPoint, recipientSpendPub, oneTime)) {
        return std::vector<uint8_t>();
    }

    return oneTime;
}

bool StealthAddress::checkOwnership(const std::vector<uint8_t>& oneTimeAddress,
                                    const std::vector<uint8_t>& ephemeralPub) const {
    if (sodium_init() < 0) {
        return false;
    }

    std::vector<uint8_t> sharedPoint;
    if (!impl_->computeSharedSecret(impl_->viewKey, ephemeralPub, sharedPoint)) {
        return false;
    }

    std::vector<uint8_t> expected;
    if (!impl_->oneTimeFromSecret(sharedPoint, impl_->spendPublic, expected)) {
        return false;
    }

    if (oneTimeAddress.size() != expected.size()) {
        return false;
    }

    return sodium_memcmp(oneTimeAddress.data(), expected.data(), expected.size()) == 0;
}

std::vector<uint8_t> StealthAddress::deriveSpendingKey(const std::vector<uint8_t>& ephemeralPub) const {
    if (sodium_init() < 0) {
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> sharedPoint;
    if (!impl_->computeSharedSecret(impl_->viewKey, ephemeralPub, sharedPoint)) {
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> scalar;
    if (!impl_->hashToScalar(sharedPoint, scalar)) {
        return std::vector<uint8_t>();
    }

    if (impl_->spendKey.size() != crypto_core_ed25519_SCALARBYTES) {
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> spendingKey(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_add(spendingKey.data(), scalar.data(), impl_->spendKey.data());
    sodium_memzero(scalar.data(), scalar.size());

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
