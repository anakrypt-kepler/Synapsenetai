#pragma once

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse::quantum::detail {

inline constexpr const char* kMlKem768Name = "ML-KEM-768";
inline constexpr const char* kKyber768LegacyName = "Kyber768";
inline constexpr const char* kMlDsa65Name = "ML-DSA-65";
inline constexpr const char* kDilithium3LegacyName = "Dilithium3";
inline constexpr const char* kSlhDsaSha2128sName = "SLH-DSA-SHA2-128s";
inline constexpr const char* kSphincsSha2128sSimpleName = "SPHINCS+-SHA2-128s-simple";

#ifdef USE_LIBOQS

inline OQS_KEM* newPreferredKyberKem() {
    if (auto* kem = OQS_KEM_new(kMlKem768Name)) return kem;
    return OQS_KEM_new(kKyber768LegacyName);
}

inline OQS_SIG* newPreferredDilithiumSig() {
    if (auto* sig = OQS_SIG_new(kMlDsa65Name)) return sig;
    return OQS_SIG_new(kDilithium3LegacyName);
}

inline OQS_SIG* newPreferredSphincsSig() {
    if (auto* sig = OQS_SIG_new(kSlhDsaSha2128sName)) return sig;
    return OQS_SIG_new(kSphincsSha2128sSimpleName);
}

#endif // USE_LIBOQS

} // namespace synapse::quantum::detail
