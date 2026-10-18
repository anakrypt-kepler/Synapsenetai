#pragma once

// Portable stand-in for the compiler 128-bit integer. GCC and Clang keep the
// native type so consensus math stays bit-identical. MSVC gets the same
// wraparound multiply, add, subtract, and divide.

#include <cstdint>
#include <limits>
#include <type_traits>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if !defined(_MSC_VER)

using SynapseU128 = unsigned __int128;
using SynapseI128 = __int128;

#else

struct SynapseU128 {
    uint64_t lo = 0;
    uint64_t hi = 0;

    SynapseU128() = default;
    template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    SynapseU128(T v) : lo(static_cast<uint64_t>(v)), hi(0) {
        if constexpr (std::is_signed_v<T>) {
            if (v < 0) hi = ~uint64_t{0};
        }
    }

    explicit operator bool() const { return lo || hi; }
    explicit operator uint64_t() const { return lo; }
    explicit operator uint32_t() const { return static_cast<uint32_t>(lo); }

    friend SynapseU128 operator+(SynapseU128 a, SynapseU128 b) {
        SynapseU128 r;
        r.lo = a.lo + b.lo;
        r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0);
        return r;
    }
    friend SynapseU128 operator-(SynapseU128 a, SynapseU128 b) {
        SynapseU128 r;
        r.lo = a.lo - b.lo;
        r.hi = a.hi - b.hi - (a.lo < b.lo ? 1 : 0);
        return r;
    }
    friend SynapseU128 operator*(SynapseU128 a, SynapseU128 b) {
        uint64_t hi = 0;
        SynapseU128 r;
        r.lo = _umul128(a.lo, b.lo, &hi);
        r.hi = hi + a.lo * b.hi + a.hi * b.lo;
        return r;
    }
    friend SynapseU128 operator/(SynapseU128 n, SynapseU128 d);
    friend SynapseU128 operator%(SynapseU128 n, SynapseU128 d) {
        if (!d.lo && !d.hi) return SynapseU128(0);
        return n - (n / d) * d;
    }
    friend bool operator==(SynapseU128 a, SynapseU128 b) { return a.lo == b.lo && a.hi == b.hi; }
    friend bool operator!=(SynapseU128 a, SynapseU128 b) { return !(a == b); }
    friend bool operator<(SynapseU128 a, SynapseU128 b) {
        return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo);
    }
    friend bool operator>(SynapseU128 a, SynapseU128 b) { return b < a; }
    friend bool operator<=(SynapseU128 a, SynapseU128 b) { return !(b < a); }
    friend bool operator>=(SynapseU128 a, SynapseU128 b) { return !(a < b); }

    SynapseU128& operator+=(SynapseU128 other) {
        *this = *this + other;
        return *this;
    }
    SynapseU128& operator*=(SynapseU128 other) {
        *this = *this * other;
        return *this;
    }
};

inline SynapseU128 operator/(SynapseU128 n, SynapseU128 d) {
    if (!d.lo && !d.hi) return SynapseU128(0);
    if (n.hi == 0 && d.hi == 0) return SynapseU128(n.lo / d.lo);
    SynapseU128 q;
    SynapseU128 rem;
    for (int bit = 127; bit >= 0; --bit) {
        const uint64_t carry = rem.hi >> 63;
        rem.hi = (rem.hi << 1) | (rem.lo >> 63);
        rem.lo <<= 1;
        const uint64_t src = bit >= 64 ? n.hi : n.lo;
        const int shift = bit >= 64 ? bit - 64 : bit;
        if ((src >> shift) & 1ULL) rem.lo |= 1ULL;
        const bool ge = carry || rem.hi > d.hi || (rem.hi == d.hi && rem.lo >= d.lo);
        if (!ge) continue;
        rem = carry ? (rem + (SynapseU128(0) - d)) : (rem - d);
        if (bit >= 64) q.hi |= 1ULL << (bit - 64);
        else q.lo |= 1ULL << bit;
    }
    return q;
}

struct SynapseI128 {
    SynapseU128 mag;

    SynapseI128() = default;
    template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    SynapseI128(T v) {
        if constexpr (std::is_signed_v<T>) {
            if (v < 0) {
                const auto wide = static_cast<long long>(v);
                const uint64_t bits = static_cast<uint64_t>(-(wide + 1)) + 1ULL;
                mag = SynapseU128(0) - SynapseU128(bits);
                return;
            }
        }
        mag = SynapseU128(static_cast<uint64_t>(v));
    }
    explicit SynapseI128(SynapseU128 bits) : mag(bits) {}

    bool negative() const { return (mag.hi >> 63) != 0; }

    explicit operator long long() const {
        if (!negative()) {
            if (mag.hi != 0 || mag.lo > static_cast<uint64_t>(LLONG_MAX)) return LLONG_MAX;
            return static_cast<long long>(mag.lo);
        }
        const SynapseU128 pos = SynapseU128(0) - mag;
        if (pos.hi != 0 || pos.lo > static_cast<uint64_t>(LLONG_MAX) + 1ULL) return LLONG_MIN;
        if (pos.lo == static_cast<uint64_t>(LLONG_MAX) + 1ULL) return LLONG_MIN;
        return -static_cast<long long>(pos.lo);
    }

    friend SynapseI128 operator-(SynapseI128 v) {
        SynapseI128 r;
        r.mag = SynapseU128(0) - v.mag;
        return r;
    }
    friend SynapseI128 operator+(SynapseI128 a, SynapseI128 b) {
        SynapseI128 r;
        r.mag = a.mag + b.mag;
        return r;
    }
    friend SynapseI128 operator-(SynapseI128 a, SynapseI128 b) { return a + (-b); }
    friend SynapseI128 operator*(SynapseI128 a, SynapseI128 b) {
        SynapseI128 r;
        r.mag = a.mag * b.mag;
        return r;
    }
    friend SynapseI128 operator/(SynapseI128 n, SynapseI128 d) {
        const bool neg = n.negative() != d.negative();
        const SynapseU128 an = n.negative() ? (-n).mag : n.mag;
        const SynapseU128 ad = d.negative() ? (-d).mag : d.mag;
        const SynapseU128 q = an / ad;
        return neg ? -SynapseI128(q) : SynapseI128(q);
    }
    friend bool operator>=(SynapseI128 a, SynapseI128 b) {
        const bool an = a.negative();
        const bool bn = b.negative();
        if (an != bn) return !an;
        return a.mag >= b.mag;
    }
};

#endif
