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
        status.dilithiumReal = true;
        OQS_SIG_free(sig);
    }
    if (auto* sig = detail::newPreferredSphincsSig()) {
        status.sphincsReal = true;
        OQS_SIG_free(sig);
    }
#endif
    return status;
}

}
}
