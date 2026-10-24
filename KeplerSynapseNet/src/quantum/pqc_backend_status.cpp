#include "quantum/quantum_security.h"

#include "pqc_backend_oqs.h"

namespace synapse {
namespace quantum {

PQCBackendStatus getPQCBackendStatus() {
    PQCBackendStatus status;
#ifdef USE_LIBOQS
    if (auto* kem = detail::newPreferredKyberKem()) {
        status.kyberReal = true;
        OQS_KEM_free(kem);
    }
    if (auto* sig = detail::newPreferredDilithiumSig()) {
        status.dilithiumReal =
            sig->length_public_key == DILITHIUM_PUBLIC_KEY_SIZE &&
            sig->length_secret_key == DILITHIUM_SECRET_KEY_SIZE &&
            sig->length_signature == DILITHIUM_SIGNATURE_SIZE;
        OQS_SIG_free(sig);
    }
    if (auto* sig = detail::newPreferredSphincsSig()) {
        status.sphincsReal =
            sig->length_public_key == SPHINCS_PUBLIC_KEY_SIZE &&
            sig->length_secret_key == SPHINCS_SECRET_KEY_SIZE &&
            sig->length_signature == SPHINCS_SIGNATURE_SIZE;
        OQS_SIG_free(sig);
    }
#endif
    return status;
}

}
}
