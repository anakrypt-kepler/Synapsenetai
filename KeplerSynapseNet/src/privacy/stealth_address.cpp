#include "privacy/privacy.h"
#include "crypto/crypto.h"
#include "crypto/confidential_tx.h"
#include "quantum/quantum_security.h"
#include "quantum/application_signature.h"
#include "../quantum/pqc_backend_oqs.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <random>
#include <sodium.h>
#include <openssl/evp.h>

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse {
namespace privacy {

struct StealthAddress::Impl {
    std::vector<uint8_t> viewKey;
    std::vector<uint8_t> spendKey;
    std::vector<uint8_t> viewPublic;
    std::vector<uint8_t> spendPublic;
    // ML-KEM-768 KP from the view scalar (see deriveKyberFromViewSeed).
    std::vector<uint8_t> kyberPk;
    std::vector<uint8_t> kyberSk;

    ~Impl();
    void refreshKyber();
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

StealthAddress::Impl::~Impl() {
    if (!viewKey.empty()) sodium_memzero(viewKey.data(), viewKey.size());
    if (!spendKey.empty()) sodium_memzero(spendKey.data(), spendKey.size());
    if (!kyberSk.empty()) sodium_memzero(kyberSk.data(), kyberSk.size());
}

namespace {

constexpr uint8_t kEcdhWrapV3 = 0x03;

// Domain-separated SHAKE256 stream for deterministic OQS keygen.
std::vector<uint8_t> shake256Expand(const std::vector<uint8_t>& seed, size_t outputLen) {
    std::vector<uint8_t> out(outputLen);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    bool ok = true;
    if (1 != EVP_DigestInit_ex(ctx, EVP_shake256(), nullptr)) ok = false;
    if (ok && 1 != EVP_DigestUpdate(ctx, seed.data(), seed.size())) ok = false;
    if (ok && 1 != EVP_DigestFinalXOF(ctx, out.data(), outputLen)) ok = false;
    EVP_MD_CTX_free(ctx);
    if (!ok) return {};
    return out;
}

std::mutex& kyberDetRngMutex() {
    static std::mutex m;
    return m;
}

thread_local std::vector<uint8_t> tl_kyber_det_stream;
thread_local size_t tl_kyber_det_pos = 0;

void kyberDetRngCallback(uint8_t* out, size_t n) {
    while (n > 0) {
        if (tl_kyber_det_pos >= tl_kyber_det_stream.size()) {
            std::memset(out, 0, n);
            return;
        }
        const size_t take = std::min(n, tl_kyber_det_stream.size() - tl_kyber_det_pos);
        std::memcpy(out, tl_kyber_det_stream.data() + tl_kyber_det_pos, take);
        tl_kyber_det_pos += take;
        out += take;
        n -= take;
    }
}

// Kyber KP is derived from the stealth VIEW SCALAR (32-byte ed25519 secret),
// not from the public view point. Recipients decapsulate with this SK.
// Senders encapsulate to the matching PK, which is published as an optional
// suffix on the SN address: SN || hex(viewPub || spendPub || mlkem768_pk).
// Old SN+128-hex addresses have no suffix; those outputs stay classical.
bool deriveKyberFromViewSeed(const std::vector<uint8_t>& viewKey,
                             std::vector<uint8_t>& pk,
                             std::vector<uint8_t>& sk) {
    pk.clear();
    sk.clear();
    if (viewKey.size() != crypto_core_ed25519_SCALARBYTES) return false;
    if (!quantum::getPQCBackendStatus().kyberReal) return false;
#ifdef USE_LIBOQS
    const char tag[] = "SynapseNet|stealth|mlkem768|view|v1";
    std::vector<uint8_t> seed(tag, tag + sizeof(tag) - 1);
    seed.insert(seed.end(), viewKey.begin(), viewKey.end());

    std::lock_guard<std::mutex> rngLock(kyberDetRngMutex());
    const size_t streamLen = 64 * 1024;
    auto prevStream = std::move(tl_kyber_det_stream);
    const size_t prevPos = tl_kyber_det_pos;
    tl_kyber_det_stream = shake256Expand(seed, streamLen);
    tl_kyber_det_pos = 0;
    sodium_memzero(seed.data(), seed.size());
    if (tl_kyber_det_stream.size() != streamLen) {
        tl_kyber_det_stream = std::move(prevStream);
        tl_kyber_det_pos = prevPos;
        return false;
    }

    OQS_randombytes_custom_algorithm(kyberDetRngCallback);
    OQS_KEM* kem = quantum::detail::newPreferredKyberKem();
    bool ok = false;
    if (kem) {
        pk.assign(kem->length_public_key, 0);
        sk.assign(kem->length_secret_key, 0);
        if (OQS_KEM_keypair(kem, pk.data(), sk.data()) == OQS_SUCCESS) {
            ok = true;
        } else {
            pk.clear();
            sk.clear();
        }
        OQS_KEM_free(kem);
    }
    OQS_randombytes_switch_algorithm(OQS_RAND_alg_system);
    std::fill(tl_kyber_det_stream.begin(), tl_kyber_det_stream.end(), 0);
    tl_kyber_det_stream = std::move(prevStream);
    tl_kyber_det_pos = prevPos;
    return ok;
#else
    (void)viewKey;
    return false;
#endif
}

bool wrapEcdhV3(const std::vector<uint8_t>& classical,
                const std::vector<uint8_t>& recipientKyberPk,
                std::vector<uint8_t>& out) {
    // Blob: 0x03 | uint16le kem_ct_len | kem_ct | secretbox(classical, kem_ss).
    // Trailing bytes are the classical ecdh payload (nonce||box) encrypted
    // with the ML-KEM-768 shared secret so an ECDH break does not leak amount.
    out.clear();
    if (classical.empty() || recipientKyberPk.size() != quantum::KYBER_PUBLIC_KEY_SIZE) {
        return false;
    }
    quantum::Kyber kyber;
    quantum::KyberPublicKey pk{};
    std::memcpy(pk.data(), recipientKyberPk.data(), pk.size());
    auto enc = kyber.encapsulate(pk);
    if (!enc.success || enc.ciphertext.empty() ||
        enc.sharedSecret.size() != crypto_secretbox_KEYBYTES ||
        enc.ciphertext.size() > 0xffff) {
        if (!enc.sharedSecret.empty()) {
            sodium_memzero(enc.sharedSecret.data(), enc.sharedSecret.size());
        }
        return false;
    }

    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    std::vector<uint8_t> boxed(crypto_secretbox_MACBYTES + classical.size());
    if (crypto_secretbox_easy(boxed.data(), classical.data(), classical.size(),
                              nonce, enc.sharedSecret.data()) != 0) {
        sodium_memzero(enc.sharedSecret.data(), enc.sharedSecret.size());
        sodium_memzero(nonce, sizeof(nonce));
        return false;
    }

    const uint16_t ctLen = static_cast<uint16_t>(enc.ciphertext.size());
    out.push_back(kEcdhWrapV3);
    out.push_back(static_cast<uint8_t>(ctLen & 0xff));
    out.push_back(static_cast<uint8_t>((ctLen >> 8) & 0xff));
    out.insert(out.end(), enc.ciphertext.begin(), enc.ciphertext.end());
    out.insert(out.end(), nonce, nonce + sizeof(nonce));
    out.insert(out.end(), boxed.begin(), boxed.end());

    sodium_memzero(enc.sharedSecret.data(), enc.sharedSecret.size());
    sodium_memzero(nonce, sizeof(nonce));
    return true;
}

bool unwrapEcdhV3(const std::vector<uint8_t>& wrapped,
                  const std::vector<uint8_t>& kyberSk,
                  std::vector<uint8_t>& classical) {
    classical.clear();
    if (wrapped.size() < 3 || wrapped[0] != kEcdhWrapV3) return false;
    const uint16_t ctLen = static_cast<uint16_t>(wrapped[1]) |
                           (static_cast<uint16_t>(wrapped[2]) << 8);
    if (ctLen == 0 || wrapped.size() < static_cast<size_t>(3) + ctLen +
        crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) {
        return false;
    }
    if (kyberSk.size() != quantum::KYBER_SECRET_KEY_SIZE) return false;
    if (ctLen > quantum::KYBER_CIPHERTEXT_SIZE) return false;

    quantum::KyberCiphertext ct{};
    std::memcpy(ct.data(), wrapped.data() + 3, ctLen);
    quantum::KyberSecretKey sk{};
    std::memcpy(sk.data(), kyberSk.data(), sk.size());
    quantum::Kyber kyber;
    auto ss = kyber.decapsulate(ct, sk);
    sodium_memzero(sk.data(), sk.size());
    if (ss.size() != crypto_secretbox_KEYBYTES) {
        if (!ss.empty()) sodium_memzero(ss.data(), ss.size());
        return false;
    }

    const unsigned char* nonce = wrapped.data() + 3 + ctLen;
    const unsigned char* cipher = nonce + crypto_secretbox_NONCEBYTES;
    const unsigned long long clen = static_cast<unsigned long long>(
        wrapped.size() - (3 + ctLen + crypto_secretbox_NONCEBYTES));
    std::vector<uint8_t> plain(clen >= crypto_secretbox_MACBYTES
                                   ? clen - crypto_secretbox_MACBYTES
                                   : 0);
    if (plain.empty() ||
        crypto_secretbox_open_easy(plain.data(), cipher, clen, nonce, ss.data()) != 0) {
        sodium_memzero(ss.data(), ss.size());
        return false;
    }
    sodium_memzero(ss.data(), ss.size());
    classical = std::move(plain);
    return true;
}

} // namespace

void StealthAddress::Impl::refreshKyber() {
    if (!kyberSk.empty()) {
        sodium_memzero(kyberSk.data(), kyberSk.size());
        kyberSk.clear();
    }
    kyberPk.clear();
    // If liboqs Kyber is not wired, keep classical ECDH only. Simulated KEM
    // would fingerprint as "PQ" without actually wrapping the amount blob.
    if (!quantum::getPQCBackendStatus().kyberReal) return;
    deriveKyberFromViewSeed(viewKey, kyberPk, kyberSk);
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

    impl_->refreshKyber();
    return true;
}

bool StealthAddress::setKeys(const std::vector<uint8_t>& viewSecret,
                             const std::vector<uint8_t>& spendSecret) {
    if (sodium_init() < 0) {
        return false;
    }
    if (viewSecret.size() != crypto_core_ed25519_SCALARBYTES ||
        spendSecret.size() != crypto_core_ed25519_SCALARBYTES) {
        return false;
    }

    impl_->viewKey = viewSecret;
    impl_->spendKey = spendSecret;
    impl_->viewPublic.resize(crypto_core_ed25519_BYTES);
    impl_->spendPublic.resize(crypto_core_ed25519_BYTES);
    if (crypto_scalarmult_ed25519_base_noclamp(impl_->viewPublic.data(),
                                               impl_->viewKey.data()) != 0) {
        return false;
    }
    if (crypto_scalarmult_ed25519_base_noclamp(impl_->spendPublic.data(),
                                               impl_->spendKey.data()) != 0) {
        return false;
    }
    impl_->refreshKyber();
    return true;
}

bool StealthAddress::hasKeys() const {
    return impl_->viewKey.size() == crypto_core_ed25519_SCALARBYTES &&
           impl_->spendKey.size() == crypto_core_ed25519_SCALARBYTES &&
           impl_->viewPublic.size() == crypto_core_ed25519_BYTES &&
           impl_->spendPublic.size() == crypto_core_ed25519_BYTES;
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

namespace {

bool deriveEcdhKey(const std::vector<uint8_t>& shared,
                   unsigned char key[crypto_secretbox_KEYBYTES]) {
    const char domain[] = "SynapseNet_ecdh_key_v1";
    crypto_generichash_state st;
    if (crypto_generichash_init(&st, nullptr, 0, crypto_secretbox_KEYBYTES) != 0) {
        return false;
    }
    crypto_generichash_update(&st, reinterpret_cast<const unsigned char*>(domain), sizeof(domain) - 1);
    crypto_generichash_update(&st, shared.data(), shared.size());
    crypto_generichash_final(&st, key, crypto_secretbox_KEYBYTES);
    return true;
}

} // namespace

bool StealthAddress::createPayment(const std::vector<uint8_t>& recipientViewPub,
                                   const std::vector<uint8_t>& recipientSpendPub,
                                   uint64_t amountAtoms,
                                   StealthPayment& out,
                                   const std::vector<uint8_t>& recipientKyberPk) const {
    out = StealthPayment{};
    if (sodium_init() < 0) {
        return false;
    }
    if (amountAtoms == 0) {
        return false;
    }

    std::vector<uint8_t> ephemeralSecret;
    if (!impl_->generateScalarKeyPair(ephemeralSecret, out.ephemeralPub)) {
        return false;
    }

    std::vector<uint8_t> sharedPoint;
    if (!impl_->computeSharedSecret(ephemeralSecret, recipientViewPub, sharedPoint)) {
        sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());
        return false;
    }
    if (!impl_->oneTimeFromSecret(sharedPoint, recipientSpendPub, out.oneTimeAddress)) {
        sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());
        sodium_memzero(sharedPoint.data(), sharedPoint.size());
        return false;
    }

    out.blinding = crypto::ConfidentialTx::generateBlindingFactor();
    crypto::PedersenCommitment c = crypto::ConfidentialTx::commit(amountAtoms, out.blinding);
    if (c.commitment.size() != crypto_core_ed25519_BYTES) {
        sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());
        sodium_memzero(sharedPoint.data(), sharedPoint.size());
        return false;
    }
    out.commitment = c.commitment;

    unsigned char payload[8 + crypto_core_ed25519_SCALARBYTES];
    std::memset(payload, 0, sizeof(payload));
    for (int i = 0; i < 8; i++) {
        payload[i] = static_cast<unsigned char>((amountAtoms >> (i * 8)) & 0xff);
    }
    std::memcpy(payload + 8, out.blinding.data(), crypto_core_ed25519_SCALARBYTES);

    unsigned char key[crypto_secretbox_KEYBYTES];
    if (!deriveEcdhKey(sharedPoint, key)) {
        sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());
        sodium_memzero(sharedPoint.data(), sharedPoint.size());
        sodium_memzero(payload, sizeof(payload));
        return false;
    }

    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    std::vector<uint8_t> cipher(crypto_secretbox_MACBYTES + sizeof(payload));
    if (crypto_secretbox_easy(cipher.data(), payload, sizeof(payload), nonce, key) != 0) {
        sodium_memzero(key, sizeof(key));
        sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());
        sodium_memzero(sharedPoint.data(), sharedPoint.size());
        sodium_memzero(payload, sizeof(payload));
        return false;
    }

    out.ecdh.assign(nonce, nonce + sizeof(nonce));
    out.ecdh.insert(out.ecdh.end(), cipher.begin(), cipher.end());

    sodium_memzero(key, sizeof(key));
    sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());
    sodium_memzero(sharedPoint.data(), sharedPoint.size());
    sodium_memzero(payload, sizeof(payload));
    sodium_memzero(nonce, sizeof(nonce));

    // Quantum-WRAP: prefix 0x03 + ML-KEM-768 ct, then classical ecdh encrypted
    // under the KEM shared secret. Skip when kyberReal is false (no fake PQ).
    if (quantum::getPQCBackendStatus().kyberReal) {
        std::vector<uint8_t> kemPk = recipientKyberPk;
        if (kemPk.empty() &&
            recipientViewPub.size() == impl_->viewPublic.size() &&
            sodium_memcmp(recipientViewPub.data(), impl_->viewPublic.data(),
                          recipientViewPub.size()) == 0) {
            kemPk = impl_->kyberPk;
        }
        if (kemPk.size() == quantum::KYBER_PUBLIC_KEY_SIZE) {
            std::vector<uint8_t> wrapped;
            if (!wrapEcdhV3(out.ecdh, kemPk, wrapped)) {
                out.ecdh.clear();
                return false;
            }
            out.ecdh = std::move(wrapped);
        }
    }
    return true;
}

bool StealthAddress::tryOpenPayment(const std::vector<uint8_t>& ephemeralPub,
                                    const std::vector<uint8_t>& ecdh,
                                    uint64_t& amountAtoms,
                                    std::vector<uint8_t>& blinding) const {
    amountAtoms = 0;
    blinding.clear();
    if (!hasKeys()) {
        return false;
    }

    std::vector<uint8_t> classical = ecdh;
    // v3 wrap is far larger than classical (80 bytes). An unlucky 0x03 nonce
    // on an old blob must still take the classical path.
    if (!ecdh.empty() && ecdh[0] == kEcdhWrapV3 && ecdh.size() > 80) {
        // Fail closed: v3 wrap must decapsulate. Do not strip the prefix
        // and retry as classical ECDH.
        if (!unwrapEcdhV3(ecdh, impl_->kyberSk, classical)) {
            return false;
        }
    }

    if (classical.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + 8 + crypto_core_ed25519_SCALARBYTES) {
        return false;
    }

    std::vector<uint8_t> sharedPoint;
    if (!impl_->computeSharedSecret(impl_->viewKey, ephemeralPub, sharedPoint)) {
        return false;
    }

    unsigned char key[crypto_secretbox_KEYBYTES];
    if (!deriveEcdhKey(sharedPoint, key)) {
        sodium_memzero(sharedPoint.data(), sharedPoint.size());
        return false;
    }

    const unsigned char* nonce = classical.data();
    const unsigned char* cipher = classical.data() + crypto_secretbox_NONCEBYTES;
    unsigned long long clen = static_cast<unsigned long long>(
        classical.size() - crypto_secretbox_NONCEBYTES);
    unsigned char payload[8 + crypto_core_ed25519_SCALARBYTES];
    if (crypto_secretbox_open_easy(payload, cipher, clen, nonce, key) != 0) {
        sodium_memzero(key, sizeof(key));
        sodium_memzero(sharedPoint.data(), sharedPoint.size());
        return false;
    }

    uint64_t atoms = 0;
    for (int i = 0; i < 8; i++) {
        atoms |= static_cast<uint64_t>(payload[i]) << (i * 8);
    }
    blinding.assign(payload + 8, payload + 8 + crypto_core_ed25519_SCALARBYTES);
    amountAtoms = atoms;

    sodium_memzero(key, sizeof(key));
    sodium_memzero(sharedPoint.data(), sharedPoint.size());
    sodium_memzero(payload, sizeof(payload));
    return amountAtoms > 0 && blinding.size() == crypto_core_ed25519_SCALARBYTES;
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
    // Optional ML-KEM-768 PK so senders can encapsulate without an extra RPC.
    if (impl_->kyberPk.size() == quantum::KYBER_PUBLIC_KEY_SIZE) {
        combined.insert(combined.end(), impl_->kyberPk.begin(), impl_->kyberPk.end());
    }
    return "SN" + crypto::toHex(combined);
}

bool StealthAddress::decodeAddress(const std::string& address,
                                   std::vector<uint8_t>& viewPub,
                                   std::vector<uint8_t>& spendPub) {
    std::vector<uint8_t> kyberPub;
    return decodeAddress(address, viewPub, spendPub, kyberPub);
}

bool StealthAddress::decodeAddress(const std::string& address,
                                   std::vector<uint8_t>& viewPub,
                                   std::vector<uint8_t>& spendPub,
                                   std::vector<uint8_t>& kyberPub) {
    viewPub.clear();
    spendPub.clear();
    kyberPub.clear();
    if (address.size() < 130 || address.substr(0, 2) != "SN") {
        return false;
    }

    std::string hex = address.substr(2);
    std::vector<uint8_t> combined = crypto::fromHex(hex);

    if (combined.size() == 64) {
        viewPub.assign(combined.begin(), combined.begin() + 32);
        spendPub.assign(combined.begin() + 32, combined.end());
        return true;
    }
    if (combined.size() == 64 + quantum::KYBER_PUBLIC_KEY_SIZE) {
        viewPub.assign(combined.begin(), combined.begin() + 32);
        spendPub.assign(combined.begin() + 32, combined.begin() + 64);
        kyberPub.assign(combined.begin() + 64, combined.end());
        return true;
    }
    return false;
}

std::vector<uint8_t> StealthAddress::getKyberPublicKey() const {
    return impl_->kyberPk;
}

bool StealthAddress::signHybrid(const std::vector<uint8_t>& message,
                                std::vector<uint8_t>& envelope) const {
    envelope.clear();
    if (!hasKeys()) return false;
    if (!quantum::getPQCBackendStatus().dilithiumReal) {
        return true;
    }

    const char tag[] = "SynapseNet|stealth|hybridsig|spend|v1";
    std::vector<uint8_t> seed(tag, tag + sizeof(tag) - 1);
    seed.insert(seed.end(), impl_->spendKey.begin(), impl_->spendKey.end());

    quantum::HybridSig signer;
    auto kp = signer.generateKeyPairFromSeed(seed);
    sodium_memzero(seed.data(), seed.size());
    if (kp.classicSecretKey.empty() || kp.pqcSecretKey.empty()) {
        return false;
    }

    envelope = quantum::signApplicationPayload("privacy.private_tx.v3", message, {}, kp);
    if (!kp.classicSecretKey.empty()) {
        sodium_memzero(kp.classicSecretKey.data(), kp.classicSecretKey.size());
    }
    if (!kp.pqcSecretKey.empty()) {
        sodium_memzero(kp.pqcSecretKey.data(), kp.pqcSecretKey.size());
    }
    return !envelope.empty();
}

bool StealthAddress::verifyHybrid(const std::vector<uint8_t>& message,
                                  const std::vector<uint8_t>& envelope) {
    if (envelope.empty()) return false;
    return quantum::verifyApplicationPayload("privacy.private_tx.v3", message, {}, envelope);
}

namespace {

bool scalarFromUtf8Seed(const std::string& seed, std::vector<uint8_t>& scalar) {
    if (sodium_init() < 0) return false;
    unsigned char wide[crypto_core_ed25519_NONREDUCEDSCALARBYTES];
    crypto_hash_sha512(wide, reinterpret_cast<const unsigned char*>(seed.data()), seed.size());
    scalar.assign(crypto_core_ed25519_SCALARBYTES, 0);
    crypto_core_ed25519_scalar_reduce(scalar.data(), wide);
    sodium_memzero(wide, sizeof(wide));
    return true;
}

bool scalarFromBytes(const std::vector<uint8_t>& data, std::vector<uint8_t>& scalar) {
    if (sodium_init() < 0) return false;
    unsigned char wide[crypto_core_ed25519_NONREDUCEDSCALARBYTES];
    crypto_hash_sha512(wide, data.data(), data.size());
    scalar.assign(crypto_core_ed25519_SCALARBYTES, 0);
    crypto_core_ed25519_scalar_reduce(scalar.data(), wide);
    sodium_memzero(wide, sizeof(wide));
    return true;
}

} // namespace

bool stealthKeysFromMnemonic(const std::string& mnemonic, StealthAddress& out) {
    if (mnemonic.empty()) return false;
    std::vector<uint8_t> view;
    std::vector<uint8_t> spend;
    if (!scalarFromUtf8Seed(mnemonic + "|SynapseNet|stealth|view|v1", view)) return false;
    if (!scalarFromUtf8Seed(mnemonic + "|SynapseNet|stealth|spend|v1", spend)) {
        sodium_memzero(view.data(), view.size());
        return false;
    }
    bool ok = out.setKeys(view, spend);
    sodium_memzero(view.data(), view.size());
    sodium_memzero(spend.data(), spend.size());
    return ok;
}

bool stealthKeysFromSecret(const std::vector<uint8_t>& secret, StealthAddress& out) {
    if (secret.empty()) return false;
    const char viewTag[] = "SynapseNet|stealth|secp|view|v1";
    const char spendTag[] = "SynapseNet|stealth|secp|spend|v1";
    std::vector<uint8_t> viewData(viewTag, viewTag + sizeof(viewTag) - 1);
    viewData.insert(viewData.end(), secret.begin(), secret.end());
    std::vector<uint8_t> spendData(spendTag, spendTag + sizeof(spendTag) - 1);
    spendData.insert(spendData.end(), secret.begin(), secret.end());
    std::vector<uint8_t> view;
    std::vector<uint8_t> spend;
    if (!scalarFromBytes(viewData, view) || !scalarFromBytes(spendData, spend)) {
        sodium_memzero(viewData.data(), viewData.size());
        sodium_memzero(spendData.data(), spendData.size());
        return false;
    }
    sodium_memzero(viewData.data(), viewData.size());
    sodium_memzero(spendData.data(), spendData.size());
    bool ok = out.setKeys(view, spend);
    sodium_memzero(view.data(), view.size());
    sodium_memzero(spend.data(), spend.size());
    return ok;
}

}
}
