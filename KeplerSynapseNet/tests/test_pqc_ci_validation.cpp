#include "quantum/quantum_security.h"

#include <cstdio>

using namespace synapse::quantum;

static bool testBackendStatusConsistency() {
    PQCBackendStatus status = getPQCBackendStatus();
#ifdef USE_LIBOQS
    if (!status.kyberReal && !status.dilithiumReal && !status.sphincsReal) {
        std::fprintf(stderr, "FAIL: liboqs compiled in but no algorithm reported real\n");
        return false;
    }
#else
    if (status.kyberReal || status.dilithiumReal || status.sphincsReal) {
        std::fprintf(stderr, "FAIL: no liboqs but algorithm reported real\n");
        return false;
    }
#endif
    return true;
}

static bool testSimulationFallbackProducesKeys() {
    Kyber kyber;
    auto kp = kyber.generateKeyPair();
    if (kp.publicKey.size() != KYBER_PUBLIC_KEY_SIZE) return false;
    if (kp.secretKey.size() != KYBER_SECRET_KEY_SIZE) return false;

    Dilithium dilithium;
    auto dkp = dilithium.generateKeyPair();
    if (dkp.publicKey.size() != DILITHIUM_PUBLIC_KEY_SIZE) return false;
    if (dkp.secretKey.size() != DILITHIUM_SECRET_KEY_SIZE) return false;

    Sphincs sphincs;
    auto skp = sphincs.generateKeyPair();
    if (skp.publicKey.size() != SPHINCS_PUBLIC_KEY_SIZE) return false;
    if (skp.secretKey.size() != SPHINCS_SECRET_KEY_SIZE) return false;

    return true;
}

static bool testBackendStatusFieldDefaults() {
    PQCBackendStatus fresh;
    if (fresh.kyberReal || fresh.dilithiumReal || fresh.sphincsReal) {
        std::fprintf(stderr, "FAIL: default PQCBackendStatus should be all false\n");
        return false;
    }
    return true;
}

int main() {
    int failures = 0;

    auto run = [&](const char* name, bool (*fn)()) {
        bool ok = fn();
        std::fprintf(stderr, "%s: %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };

    run("BackendStatusConsistency", testBackendStatusConsistency);
    run("SimulationFallbackProducesKeys", testSimulationFallbackProducesKeys);
    run("BackendStatusFieldDefaults", testBackendStatusFieldDefaults);

    return failures;
}
