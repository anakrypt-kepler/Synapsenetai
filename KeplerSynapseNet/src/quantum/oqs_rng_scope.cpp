#include "oqs_rng_scope.h"
#include "crypto/crypto.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse::quantum::detail {

namespace {

std::mutex& rngMutex() {
    static std::mutex m;
    return m;
}

thread_local std::vector<uint8_t> tl_stream;
thread_local size_t tl_pos = 0;
thread_local bool tl_active = false;
thread_local bool tl_underflow = false;
thread_local int tl_os_depth = 0;

bool fillOsRandom(uint8_t* out, size_t n) {
    crypto::containForeignCryptoModules();
    if (out == nullptr || n == 0 || tl_os_depth != 0) return false;
    ++tl_os_depth;
    uint8_t* cursor = out;
    size_t left = n;
    bool ok = true;
    while (ok && left > 0) {
        const size_t chunk = std::min(left, static_cast<size_t>(1u << 30));
        if (RAND_bytes(cursor, static_cast<int>(chunk)) != 1) {
            RAND_poll();
            if (RAND_bytes(cursor, static_cast<int>(chunk)) != 1) {
                ok = false;
                break;
            }
        }
        cursor += chunk;
        left -= chunk;
    }
    --tl_os_depth;
    if (!ok) OPENSSL_cleanse(out, n);
    return ok;
}

void rngCallback(uint8_t* out, size_t n) {
    if (n == 0) return;
    if (out == nullptr) {
        tl_underflow = true;
        return;
    }
    // A thread that did not arm the hook draws from the system generator.
    if (!tl_active) {
        if (!fillOsRandom(out, n)) {
            OPENSSL_cleanse(out, n);
            std::abort();
        }
        return;
    }
    if (tl_pos > tl_stream.size() || n > tl_stream.size() - tl_pos) {
        tl_underflow = true;
        // Do not publish zeros. The keygen caller discards the result.
        if (!fillOsRandom(out, n)) {
            OPENSSL_cleanse(out, n);
            std::abort();
        }
        return;
    }
    std::memcpy(out, tl_stream.data() + tl_pos, n);
    tl_pos += n;
}

} // namespace

struct OqsDeterministicRngGuard::Lock {
    std::lock_guard<std::mutex> guard;
    explicit Lock(std::mutex& m) : guard(m) {}
};

OqsDeterministicRngGuard::OqsDeterministicRngGuard(std::vector<uint8_t> stream)
    : lock_(new Lock(rngMutex())),
      prevStream_(std::move(tl_stream)),
      prevPos_(tl_pos),
      prevActive_(tl_active),
      prevUnderflow_(tl_underflow) {
    crypto::containForeignCryptoModules();
    tl_stream = std::move(stream);
    tl_pos = 0;
    tl_underflow = false;
    tl_active = true;
#ifdef USE_LIBOQS
    OQS_randombytes_custom_algorithm(rngCallback);
    armed_ = true;
#else
    (void)armed_;
#endif
}

OqsDeterministicRngGuard::~OqsDeterministicRngGuard() {
    if (!tl_stream.empty()) {
        OPENSSL_cleanse(tl_stream.data(), tl_stream.size());
    }
    tl_stream = std::move(prevStream_);
    tl_pos = prevPos_;
    tl_active = prevActive_;
    tl_underflow = prevUnderflow_;
#ifdef USE_LIBOQS
    if (armed_) {
        const auto restored = OQS_randombytes_switch_algorithm(OQS_RAND_alg_system);
        armed_ = false;
        if (restored != OQS_SUCCESS) {
            delete lock_;
            lock_ = nullptr;
            std::abort();
        }
    }
#endif
    delete lock_;
    lock_ = nullptr;
}

bool OqsDeterministicRngGuard::accepted(size_t minCoins) const {
    return tl_active && !tl_underflow && tl_pos >= minCoins;
}

} // namespace synapse::quantum::detail
