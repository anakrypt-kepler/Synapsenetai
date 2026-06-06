#include "crypto/ring_signature.h"

#include <sodium.h>

#include <cstring>
#include <stdexcept>

namespace synapse {
namespace crypto {

namespace {

constexpr size_t kPointSize = crypto_core_ed25519_BYTES;
constexpr size_t kScalarSize = crypto_core_ed25519_SCALARBYTES;
constexpr size_t kHashSize = crypto_core_ed25519_HASHBYTES;

void requireSodium() {
    static bool initialized = false;
    if (!initialized) {
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium initialization failed");
        }
        initialized = true;
    }
}

void appendBytes(std::vector<uint8_t>& buffer, const std::vector<uint8_t>& data) {
    buffer.insert(buffer.end(), data.begin(), data.end());
}

void appendU32(std::vector<uint8_t>& buffer, uint32_t value) {
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

uint32_t readU32(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("ring signature deserialize out of bounds");
    }
    uint32_t value = static_cast<uint32_t>(data[offset]) |
                     (static_cast<uint32_t>(data[offset + 1]) << 8) |
                     (static_cast<uint32_t>(data[offset + 2]) << 16) |
                     (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return value;
}

std::vector<uint8_t> readField(const std::vector<uint8_t>& data, size_t& offset) {
    uint32_t length = readU32(data, offset);
    if (offset + length > data.size()) {
        throw std::runtime_error("ring signature deserialize field overflow");
    }
    std::vector<uint8_t> field(data.begin() + offset, data.begin() + offset + length);
    offset += length;
    return field;
}

std::vector<uint8_t> scalarAdd(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<uint8_t> out(kScalarSize);
    crypto_core_ed25519_scalar_add(out.data(), a.data(), b.data());
    return out;
}

std::vector<uint8_t> scalarSub(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<uint8_t> out(kScalarSize);
    crypto_core_ed25519_scalar_sub(out.data(), a.data(), b.data());
    return out;
}

std::vector<uint8_t> scalarMul(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<uint8_t> out(kScalarSize);
    crypto_core_ed25519_scalar_mul(out.data(), a.data(), b.data());
    return out;
}

std::vector<uint8_t> randomScalar() {
    std::vector<uint8_t> out(kScalarSize);
    crypto_core_ed25519_scalar_random(out.data());
    return out;
}

std::vector<uint8_t> scalarMultBase(const std::vector<uint8_t>& scalar) {
    std::vector<uint8_t> out(kPointSize);
    if (crypto_scalarmult_ed25519_base_noclamp(out.data(), scalar.data()) != 0) {
        throw std::runtime_error("ed25519 base scalar multiplication failed");
    }
    return out;
}

std::vector<uint8_t> scalarMultPoint(const std::vector<uint8_t>& scalar, const std::vector<uint8_t>& point) {
    std::vector<uint8_t> out(kPointSize);
    if (crypto_scalarmult_ed25519_noclamp(out.data(), scalar.data(), point.data()) != 0) {
        throw std::runtime_error("ed25519 scalar multiplication failed");
    }
    return out;
}

std::vector<uint8_t> pointAdd(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<uint8_t> out(kPointSize);
    if (crypto_core_ed25519_add(out.data(), a.data(), b.data()) != 0) {
        throw std::runtime_error("ed25519 point addition failed");
    }
    return out;
}

}

std::vector<uint8_t> RingSignature::serialize() const {
    std::vector<uint8_t> buffer;
    appendU32(buffer, static_cast<uint32_t>(keyImage.size()));
    appendBytes(buffer, keyImage);
    appendU32(buffer, static_cast<uint32_t>(c0.size()));
    appendBytes(buffer, c0);
    appendU32(buffer, static_cast<uint32_t>(responses.size()));
    for (const auto& response : responses) {
        appendU32(buffer, static_cast<uint32_t>(response.size()));
        appendBytes(buffer, response);
    }
    return buffer;
}

RingSignature RingSignature::deserialize(const std::vector<uint8_t>& data) {
    RingSignature sig;
    size_t offset = 0;
    sig.keyImage = readField(data, offset);
    sig.c0 = readField(data, offset);
    uint32_t count = readU32(data, offset);
    sig.responses.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        sig.responses.push_back(readField(data, offset));
    }
    return sig;
}

RingSign::RingSign() {
    requireSodium();
}

RingSign::~RingSign() {
}

std::vector<uint8_t> RingSign::hashToPoint(const std::vector<uint8_t>& data) {
    requireSodium();
    std::vector<uint8_t> hash(kHashSize);
    crypto_hash_sha512(hash.data(), data.data(), data.size());
    std::vector<uint8_t> point(kPointSize);
    crypto_core_ed25519_from_hash(point.data(), hash.data());
    return point;
}

std::vector<uint8_t> RingSign::hashToScalar(const std::vector<uint8_t>& data) {
    requireSodium();
    std::vector<uint8_t> hash(crypto_core_ed25519_NONREDUCEDSCALARBYTES);
    crypto_hash_sha512(hash.data(), data.data(), data.size());
    std::vector<uint8_t> scalar(kScalarSize);
    crypto_core_ed25519_scalar_reduce(scalar.data(), hash.data());
    return scalar;
}

RingSignature RingSign::sign(
    const std::vector<uint8_t>& message,
    const std::vector<std::vector<uint8_t>>& ring,
    const std::vector<uint8_t>& privateKey,
    size_t signerIndex
) {
    requireSodium();
    const size_t n = ring.size();
    if (n == 0) {
        throw std::runtime_error("ring must contain at least one key");
    }
    if (signerIndex >= n) {
        throw std::runtime_error("signer index out of range");
    }
    if (privateKey.size() != kScalarSize) {
        throw std::runtime_error("private key must be a 32 byte scalar");
    }
    for (const auto& key : ring) {
        if (key.size() != kPointSize) {
            throw std::runtime_error("ring key must be a 32 byte point");
        }
    }

    std::vector<uint8_t> secret(kScalarSize);
    {
        std::vector<uint8_t> padded(crypto_core_ed25519_NONREDUCEDSCALARBYTES, 0);
        std::memcpy(padded.data(), privateKey.data(), kScalarSize);
        crypto_core_ed25519_scalar_reduce(secret.data(), padded.data());
    }

    std::vector<uint8_t> keyImagePoint = hashToPoint(ring[signerIndex]);
    std::vector<uint8_t> keyImage = scalarMultPoint(secret, keyImagePoint);

    std::vector<std::vector<uint8_t>> responses(n);
    std::vector<std::vector<uint8_t>> challenges(n);

    std::vector<uint8_t> alpha = randomScalar();

    std::vector<uint8_t> lInit = scalarMultBase(alpha);
    std::vector<uint8_t> rInit = scalarMultPoint(alpha, keyImagePoint);

    auto computeChallenge = [&](const std::vector<uint8_t>& l, const std::vector<uint8_t>& r) {
        std::vector<uint8_t> data;
        appendBytes(data, message);
        appendBytes(data, l);
        appendBytes(data, r);
        return hashToScalar(data);
    };

    size_t next = (signerIndex + 1) % n;
    challenges[next] = computeChallenge(lInit, rInit);

    for (size_t step = 1; step < n; ++step) {
        size_t i = (signerIndex + step) % n;
        responses[i] = randomScalar();
        std::vector<uint8_t> hp = hashToPoint(ring[i]);
        std::vector<uint8_t> lPart = pointAdd(scalarMultBase(responses[i]), scalarMultPoint(challenges[i], ring[i]));
        std::vector<uint8_t> rPart = pointAdd(scalarMultPoint(responses[i], hp), scalarMultPoint(challenges[i], keyImage));
        size_t following = (i + 1) % n;
        challenges[following] = computeChallenge(lPart, rPart);
    }

    responses[signerIndex] = scalarSub(alpha, scalarMul(challenges[signerIndex], secret));

    RingSignature sig;
    sig.keyImage = keyImage;
    sig.c0 = challenges[0];
    sig.responses = responses;

    secureZero(secret.data(), secret.size());
    secureZero(alpha.data(), alpha.size());

    return sig;
}

bool RingSign::verify(
    const std::vector<uint8_t>& message,
    const std::vector<std::vector<uint8_t>>& ring,
    const RingSignature& sig
) {
    requireSodium();
    const size_t n = ring.size();
    if (n == 0) {
        return false;
    }
    if (sig.responses.size() != n) {
        return false;
    }
    if (sig.keyImage.size() != kPointSize) {
        return false;
    }
    if (sig.c0.size() != kScalarSize) {
        return false;
    }
    for (const auto& key : ring) {
        if (key.size() != kPointSize) {
            return false;
        }
    }
    for (const auto& response : sig.responses) {
        if (response.size() != kScalarSize) {
            return false;
        }
    }
    if (crypto_core_ed25519_is_valid_point(sig.keyImage.data()) != 1) {
        return false;
    }

    auto computeChallenge = [&](const std::vector<uint8_t>& l, const std::vector<uint8_t>& r) {
        std::vector<uint8_t> data;
        appendBytes(data, message);
        appendBytes(data, l);
        appendBytes(data, r);
        return hashToScalar(data);
    };

    std::vector<uint8_t> challenge = sig.c0;
    for (size_t i = 0; i < n; ++i) {
        std::vector<uint8_t> hp = hashToPoint(ring[i]);
        std::vector<uint8_t> lPart = pointAdd(scalarMultBase(sig.responses[i]), scalarMultPoint(challenge, ring[i]));
        std::vector<uint8_t> rPart = pointAdd(scalarMultPoint(sig.responses[i], hp), scalarMultPoint(challenge, sig.keyImage));
        challenge = computeChallenge(lPart, rPart);
    }

    return sodium_memcmp(challenge.data(), sig.c0.data(), kScalarSize) == 0;
}

bool RingSign::isDoubleSpend(
    const std::vector<uint8_t>& keyImage,
    const std::vector<std::vector<uint8_t>>& usedKeyImages
) {
    for (const auto& used : usedKeyImages) {
        if (used.size() == keyImage.size() &&
            !keyImage.empty() &&
            sodium_memcmp(used.data(), keyImage.data(), keyImage.size()) == 0) {
            return true;
        }
    }
    return false;
}

}
}
