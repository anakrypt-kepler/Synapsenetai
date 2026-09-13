#include "privacy/privacy.h"
#include "privacy/private_transfer.h"
#include "crypto/confidential_tx.h"
#include "crypto/crypto.h"
#include "quantum/quantum_security.h"

#include <sodium.h>
#include <iostream>
#include <vector>
#include <string>

using synapse::privacy::StealthAddress;
using synapse::privacy::OwnedOutput;
using synapse::privacy::PrivateSendResult;
using synapse::privacy::DecoyMember;
using synapse::privacy::mintOwnedOutput;
using synapse::privacy::buildPrivateSend;
using synapse::privacy::verifyPrivateTx;
using synapse::privacy::scanOutput;

static int fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return 1;
}

int main() {
    if (sodium_init() < 0) return fail("sodium");

    auto blind = synapse::crypto::ConfidentialTx::generateBlindingFactor();
    auto ped = synapse::crypto::ConfidentialTx::commit(77, blind);
    auto rp = synapse::crypto::ConfidentialTx::proveRange(77, blind);
    if (rp.empty()) return fail("proveRange empty");
    if (!synapse::crypto::ConfidentialTx::verifyRange(ped.commitment, rp)) return fail("verifyRange");
    auto known = synapse::crypto::ConfidentialTx::proveKnownAmount(ped.commitment, 77, blind);
    if (!synapse::crypto::ConfidentialTx::verifyKnownAmount(ped.commitment, 77, known)) {
        return fail("known amount");
    }
    if (synapse::crypto::ConfidentialTx::verifyKnownAmount(ped.commitment, 78, known)) {
        return fail("known amount accepted wrong v");
    }

    StealthAddress alice;
    StealthAddress bob;
    if (!alice.generateKeys() || !bob.generateKeys()) return fail("keys");

    synapse::privacy::StealthPayment pay;
    if (!alice.createPayment(bob.getViewPublicKey(), bob.getSpendPublicKey(), 42, pay))
        return fail("createPayment");
    uint64_t atoms = 0;
    std::vector<uint8_t> blinding;
    if (!bob.tryOpenPayment(pay.ephemeralPub, pay.ecdh, atoms, blinding) || atoms != 42)
        return fail("tryOpenPayment");

    std::vector<OwnedOutput> wallet;
    OwnedOutput minted;
    if (!mintOwnedOutput(alice, 500000000ULL, minted)) return fail("mint");
    wallet.push_back(minted);

    PrivateSendResult result;
    std::string err;
    std::vector<DecoyMember> decoys;
    if (!buildPrivateSend(alice, bob.encodeAddress(), 150000000ULL, wallet, decoys, result, err))
        return fail(std::string("buildPrivateSend: ") + err);
    if (!verifyPrivateTx(result.tx, err))
        return fail(std::string("verifyPrivateTx: ") + err);
    if (result.tx.vins.empty() || result.tx.vins[0].ringP.size() != synapse::privacy::kPrivateRingSize)
        return fail("ring size");
    if (result.tx.vins[0].ringC.size() != result.tx.vins[0].ringP.size()) return fail("ringC");
    if (result.tx.vouts.empty()) return fail("no outputs");
    for (const auto& o : result.tx.vouts) {
        if (o.rangeProof.empty()) return fail("missing range proof");
    }

    int found = 0;
    for (const auto& o : result.tx.vouts) {
        OwnedOutput owned;
        if (scanOutput(bob, o, owned)) {
            found++;
            if (owned.amountAtoms != 150000000ULL) return fail("wrong amount");
        }
    }
    if (found != 1) return fail("bob should own exactly one output");

    const auto pqc = synapse::quantum::getPQCBackendStatus();
    if (pqc.kyberReal) {
        bool anyWrap = false;
        for (const auto& o : result.tx.vouts) {
            if (!o.ecdh.empty() && o.ecdh[0] == 0x03 && o.ecdh.size() > 80) anyWrap = true;
        }
        if (!anyWrap) return fail("expected 0x03 ecdh wrap");
    }
    if (pqc.dilithiumReal) {
        if (result.tx.pqcSig.empty()) return fail("expected pqc_sig");
        auto tampered = result.tx;
        tampered.pqcSig.back() ^= 0x01;
        std::string e2;
        if (verifyPrivateTx(tampered, e2)) return fail("tampered pqc_sig accepted");
        if (pqc.kyberReal) {
            auto missing = result.tx;
            missing.pqcSig.clear();
            std::string e3;
            if (verifyPrivateTx(missing, e3)) return fail("v3 without pqc_sig accepted");
        }
    }

    std::cout << "PrivateTransferTests OK\n";
    return 0;
}
