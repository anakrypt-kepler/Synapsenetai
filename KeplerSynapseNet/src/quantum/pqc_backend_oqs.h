#pragma once

#include "quantum/quantum_security.h"

#include <cstring>

#ifdef USE_LIBOQS
#include <oqs/oqs.h>
#endif

namespace synapse::quantum::detail {

// Fixed names only. Callers do not pass an algorithm string into liboqs.
inline constexpr const char* kMlKem768Name = "ML-KEM-768";
inline constexpr const char* kKyber768LegacyName = "Kyber768";
inline constexpr const char* kMlDsa65Name = "ML-DSA-65";
inline constexpr const char* kDilithium3LegacyName = "Dilithium3";
inline constexpr const char* kSlhDsaSha2128sName = "SLH-DSA-SHA2-128s";
inline constexpr const char* kSphincsSha2128sSimpleName = "SPHINCS+-SHA2-128s-simple";

#ifdef USE_LIBOQS

inline bool exactAlgName(const char* got, const char* expect) {
    if (got == nullptr || expect == nullptr) return false;
    const size_t n = std::strlen(expect);
    if (n == 0 || n > 64) return false;
    return std::strncmp(got, expect, n) == 0 && got[n] == '\0';
}

inline bool kyberKemPinned(const OQS_KEM* kem) {
    if (kem == nullptr || kem->keypair == nullptr || kem->encaps == nullptr || kem->decaps == nullptr) {
        return false;
    }
    const bool nameOk = exactAlgName(kem->method_name, kMlKem768Name)
        || exactAlgName(kem->method_name, kKyber768LegacyName);
    return nameOk
        && kem->length_public_key == KYBER_PUBLIC_KEY_SIZE
        && kem->length_secret_key == KYBER_SECRET_KEY_SIZE
        && kem->length_ciphertext == KYBER_CIPHERTEXT_SIZE
        && kem->length_shared_secret == KYBER_SHARED_SECRET_SIZE;
}

inline bool dilithiumSigPinned(const OQS_SIG* sig) {
    if (sig == nullptr || sig->keypair == nullptr || sig->sign == nullptr || sig->verify == nullptr) {
        return false;
    }
    const bool nameOk = exactAlgName(sig->method_name, kMlDsa65Name)
        || exactAlgName(sig->method_name, kDilithium3LegacyName);
    return nameOk
        && sig->length_public_key == DILITHIUM_PUBLIC_KEY_SIZE
        && sig->length_secret_key == DILITHIUM_SECRET_KEY_SIZE
        && sig->length_signature == DILITHIUM_SIGNATURE_SIZE;
}

inline bool sphincsSigPinned(const OQS_SIG* sig) {
    if (sig == nullptr || sig->keypair == nullptr || sig->sign == nullptr || sig->verify == nullptr) {
        return false;
    }
    const bool nameOk = exactAlgName(sig->method_name, kSlhDsaSha2128sName)
        || exactAlgName(sig->method_name, kSphincsSha2128sSimpleName);
    return nameOk
        && sig->length_public_key == SPHINCS_PUBLIC_KEY_SIZE
        && sig->length_secret_key == SPHINCS_SECRET_KEY_SIZE
        && sig->length_signature == SPHINCS_SIGNATURE_SIZE;
}

inline OQS_KEM* adoptIfNamed(OQS_KEM* kem, const char* requestedName) {
    if (!kyberKemPinned(kem) || !exactAlgName(kem->method_name, requestedName)) {
        if (kem != nullptr) OQS_KEM_free(kem);
        return nullptr;
    }
    return kem;
}

inline OQS_SIG* adoptDilithiumIfNamed(OQS_SIG* sig, const char* requestedName) {
    if (!dilithiumSigPinned(sig) || !exactAlgName(sig->method_name, requestedName)) {
        if (sig != nullptr) OQS_SIG_free(sig);
        return nullptr;
    }
    return sig;
}

inline OQS_SIG* adoptSphincsIfNamed(OQS_SIG* sig, const char* requestedName) {
    if (!sphincsSigPinned(sig) || !exactAlgName(sig->method_name, requestedName)) {
        if (sig != nullptr) OQS_SIG_free(sig);
        return nullptr;
    }
    return sig;
}

inline OQS_KEM* newPreferredKyberKem() {
    if (auto* kem = adoptIfNamed(OQS_KEM_new(kMlKem768Name), kMlKem768Name)) return kem;
    return adoptIfNamed(OQS_KEM_new(kKyber768LegacyName), kKyber768LegacyName);
}

inline OQS_SIG* newPreferredDilithiumSig() {
    if (auto* sig = adoptDilithiumIfNamed(OQS_SIG_new(kMlDsa65Name), kMlDsa65Name)) return sig;
    return adoptDilithiumIfNamed(OQS_SIG_new(kDilithium3LegacyName), kDilithium3LegacyName);
}

inline OQS_SIG* newPreferredSphincsSig() {
    if (auto* sig = adoptSphincsIfNamed(OQS_SIG_new(kSlhDsaSha2128sName), kSlhDsaSha2128sName)) return sig;
    return adoptSphincsIfNamed(OQS_SIG_new(kSphincsSha2128sSimpleName), kSphincsSha2128sSimpleName);
}

#endif // USE_LIBOQS

} // namespace synapse::quantum::detail
