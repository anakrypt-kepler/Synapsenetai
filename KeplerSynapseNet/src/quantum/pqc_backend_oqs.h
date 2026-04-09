#pragma once

#ifdef USE_LIBOQS

#include <oqs/oqs.h>

namespace synapse::quantum::detail {

#ifdef OQS_KEM_alg_ml_kem_768
inline constexpr const char* kMlKem768Name = OQS_KEM_alg_ml_kem_768;
#else
inline constexpr const char* kMlKem768Name = "ML-KEM-768";
#endif

#ifdef OQS_KEM_alg_kyber_768
inline constexpr const char* kKyber768LegacyName = OQS_KEM_alg_kyber_768;
#else
inline constexpr const char* kKyber768LegacyName = "Kyber768";
#endif

#ifdef OQS_SIG_alg_ml_dsa_65
inline constexpr const char* kMlDsa65Name = OQS_SIG_alg_ml_dsa_65;
#else
inline constexpr const char* kMlDsa65Name = "ML-DSA-65";
#endif

#ifdef OQS_SIG_alg_dilithium_3
inline constexpr const char* kDilithium3LegacyName = OQS_SIG_alg_dilithium_3;
#else
inline constexpr const char* kDilithium3LegacyName = "Dilithium3";
#endif

#ifdef OQS_SIG_alg_slh_dsa_sha2_128s
inline constexpr const char* kSlhDsaSha2128sName = OQS_SIG_alg_slh_dsa_sha2_128s;
#else
inline constexpr const char* kSlhDsaSha2128sName = "SLH-DSA-SHA2-128s";
#endif

#ifdef OQS_SIG_alg_sphincs_sha2_128s_simple
inline constexpr const char* kSphincsSha2128sSimpleName = OQS_SIG_alg_sphincs_sha2_128s_simple;
#else
inline constexpr const char* kSphincsSha2128sSimpleName = "SPHINCS+-SHA2-128s-simple";
#endif

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

} // namespace synapse::quantum::detail

#endif
