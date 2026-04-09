#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include "pqc_backend_oqs.h"
#include <mutex>
#include <random>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <cstdio>
#include <openssl/rand.h>

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse {
namespace quantum {

namespace {
std::vector<uint8_t> deriveDilithiumSignatureMaterial(const std::vector<uint8_t>& message,
                                                      const std::array<uint8_t, 32>& publicTag) {
    std::vector<uint8_t> seed(message.begin(), message.end());
    seed.insert(seed.end(), publicTag.begin(), publicTag.end());
    auto state = crypto::sha256(seed.data(), seed.size());

    std::vector<uint8_t> out(DILITHIUM_SIGNATURE_SIZE);
    size_t written = 0;
    uint32_t counter = 0;
    while (written < out.size()) {
        std::vector<uint8_t> input(state.begin(), state.end());
        input.push_back(static_cast<uint8_t>((counter >> 24) & 0xff));
        input.push_back(static_cast<uint8_t>((counter >> 16) & 0xff));
        input.push_back(static_cast<uint8_t>((counter >> 8) & 0xff));
        input.push_back(static_cast<uint8_t>(counter & 0xff));
        auto block = crypto::sha256(input.data(), input.size());
        size_t take = std::min(block.size(), out.size() - written);
        std::memcpy(out.data() + written, block.data(), take);
        written += take;
        state = block;
        counter++;
    }
    return out;
}
}

struct Dilithium::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    
        Impl() : rng(0) {
    #ifdef USE_LIBOQS
        std::random_device rd;
        rng.seed(rd());
    #else
        // nondeterministic seeding even when liboqs is not available
        uint64_t seedVal = 0;
        if (RAND_bytes(reinterpret_cast<unsigned char*>(&seedVal), sizeof(seedVal)) == 1) {
            rng.seed(seedVal);
        } else {
            std::random_device rd;
            rng.seed(rd());
        }
    #endif
        }
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
};

Dilithium::Dilithium() : impl_(std::make_unique<Impl>()) {}
Dilithium::~Dilithium() = default;

DilithiumKeyPair Dilithium::generateKeyPair() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    DilithiumKeyPair kp;
    bool liboqsFailure = false;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredDilithiumSig();
    if (sig) {
        std::vector<uint8_t> pub(sig->length_public_key);
        std::vector<uint8_t> priv(sig->length_secret_key);
        if (OQS_SIG_keypair(sig, pub.data(), priv.data()) == OQS_SUCCESS) {
            size_t copyPub = std::min(pub.size(), kp.publicKey.size());
            size_t copyPriv = std::min(priv.size(), kp.secretKey.size());
            std::memcpy(kp.publicKey.data(), pub.data(), copyPub);
            std::memcpy(kp.secretKey.data(), priv.data(), copyPriv);
            OQS_SIG_free(sig);
            return kp;
        }
        liboqsFailure = true;
        OQS_SIG_free(sig);
    } else {
        liboqsFailure = true;
    }
#endif

    impl_->fillRandom(kp.publicKey.data(), kp.publicKey.size());
    impl_->fillRandom(kp.secretKey.data(), kp.secretKey.size());
    
    auto hash = crypto::sha256(kp.secretKey.data(), 32);
    std::memcpy(kp.publicKey.data(), hash.data(), 32);
#ifdef USE_LIBOQS
    if (liboqsFailure) {
        std::fprintf(stderr, "CRITICAL: liboqs was compiled in but Dilithium key generation failed\n");
    } else {
        std::fprintf(stderr, "warning: Dilithium using simulated implementation (liboqs not available)\n");
    }
#else
    std::fprintf(stderr, "warning: Dilithium using simulated implementation (liboqs not available)\n");
#endif
    return kp;
}

SignatureResult Dilithium::sign(const std::vector<uint8_t>& message,
                                 const DilithiumSecretKey& secretKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    SignatureResult result;
    bool liboqsFailure = false;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredDilithiumSig();
    if (sig) {
        std::vector<uint8_t> signed_msg(DILITHIUM_SIGNATURE_SIZE);
        size_t sig_len = 0;
        if (sig->length_signature <= signed_msg.size() &&
            OQS_SIG_sign(sig, signed_msg.data(), &sig_len, message.data(), message.size(), secretKey.data()) == OQS_SUCCESS) {
            if (sig_len < signed_msg.size()) {
                std::fill(signed_msg.begin() + static_cast<std::ptrdiff_t>(sig_len), signed_msg.end(), 0);
            }
            result.signature = std::move(signed_msg);
            result.success = true;
            OQS_SIG_free(sig);
            return result;
        }
        liboqsFailure = true;
        OQS_SIG_free(sig);
    } else {
        liboqsFailure = true;
    }
#endif

    auto publicHash = crypto::sha256(secretKey.data(), 32);
    std::array<uint8_t, 32> publicTag{};
    std::memcpy(publicTag.data(), publicHash.data(), publicTag.size());
    result.signature = deriveDilithiumSignatureMaterial(message, publicTag);
    result.success = true;
#ifdef USE_LIBOQS
    if (liboqsFailure) {
        std::fprintf(stderr, "CRITICAL: liboqs was compiled in but Dilithium signing failed\n");
    } else {
        std::fprintf(stderr, "warning: Dilithium sign using simulated implementation (liboqs not available)\n");
    }
#else
    std::fprintf(stderr, "warning: Dilithium sign using simulated implementation (liboqs not available)\n");
#endif
    return result;
}

bool Dilithium::verify(const std::vector<uint8_t>& message,
                       const DilithiumSignature& signature,
                       const DilithiumPublicKey& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!validatePublicKey(publicKey)) return false;
    bool liboqsFailure = false;
#ifdef USE_LIBOQS
    OQS_SIG* sig = detail::newPreferredDilithiumSig();
    if (sig) {
        const size_t verifyLen = std::min(signature.size(), sig->length_signature);
        if (OQS_SIG_verify(sig, message.data(), message.size(), signature.data(), verifyLen, publicKey.data()) == OQS_SUCCESS) {
            OQS_SIG_free(sig);
            return true;
        }
        liboqsFailure = true;
        OQS_SIG_free(sig);
    } else {
        liboqsFailure = true;
    }
#endif

    std::array<uint8_t, 32> publicTag{};
    std::memcpy(publicTag.data(), publicKey.data(), publicTag.size());
    auto expected = deriveDilithiumSignatureMaterial(message, publicTag);
    for (size_t i = 0; i < signature.size(); i++) {
        if (signature[i] != expected[i]) {
            return false;
        }
    }
#ifdef USE_LIBOQS
    if (liboqsFailure) {
        std::fprintf(stderr, "CRITICAL: liboqs was compiled in but Dilithium verification failed\n");
    } else {
        std::fprintf(stderr, "warning: Dilithium verify using simulated implementation (liboqs not available)\n");
    }
#else
    std::fprintf(stderr, "warning: Dilithium verify using simulated implementation (liboqs not available)\n");
#endif
    return true;
}

bool Dilithium::validatePublicKey(const DilithiumPublicKey& publicKey) {
    for (size_t i = 0; i < publicKey.size(); i++) {
        if (publicKey[i] != 0) return true;
    }
    return false;
}

bool Dilithium::validateSecretKey(const DilithiumSecretKey& secretKey) {
    for (size_t i = 0; i < secretKey.size(); i++) {
        if (secretKey[i] != 0) return true;
    }
    return false;
}

std::vector<uint8_t> Dilithium::serializePublicKey(const DilithiumPublicKey& key) {
    return std::vector<uint8_t>(key.begin(), key.end());
}

std::vector<uint8_t> Dilithium::serializeSecretKey(const DilithiumSecretKey& key) {
    return std::vector<uint8_t>(key.begin(), key.end());
}

DilithiumPublicKey Dilithium::deserializePublicKey(const std::vector<uint8_t>& data) {
    DilithiumPublicKey key{};
    size_t copyLen = std::min(data.size(), key.size());
    std::memcpy(key.data(), data.data(), copyLen);
    return key;
}

DilithiumSecretKey Dilithium::deserializeSecretKey(const std::vector<uint8_t>& data) {
    DilithiumSecretKey key{};
    size_t copyLen = std::min(data.size(), key.size());
    std::memcpy(key.data(), data.data(), copyLen);
    return key;
}

}
}
