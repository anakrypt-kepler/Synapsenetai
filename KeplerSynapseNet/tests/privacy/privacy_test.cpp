#include "privacy/privacy.h"
#include "privacy/private_transfer.h"
#include "crypto/ring_signature.h"
#include "crypto/confidential_tx.h"
#include "crypto/crypto.h"

#include <sodium.h>

#include <cassert>
#include <vector>
#include <cstdint>

using synapse::privacy::StealthAddress;
using synapse::crypto::RingSign;
using synapse::crypto::RingSignature;
using synapse::crypto::ConfidentialTx;
using synapse::crypto::PedersenCommitment;

static std::vector<uint8_t> randomScalar() {
    std::vector<uint8_t> s(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_random(s.data());
    return s;
}

static std::vector<uint8_t> pointFromScalar(const std::vector<uint8_t>& s) {
    std::vector<uint8_t> p(crypto_core_ed25519_BYTES);
    int rc = crypto_scalarmult_ed25519_base_noclamp(p.data(), s.data());
    assert(rc == 0);
    return p;
}

static void testStealth() {
    StealthAddress recipient;
    assert(recipient.generateKeys());

    std::vector<uint8_t> viewPub = recipient.getViewPublicKey();
    std::vector<uint8_t> spendPub = recipient.getSpendPublicKey();
    assert(viewPub.size() == 32);
    assert(spendPub.size() == 32);

    StealthAddress sender;
    std::vector<uint8_t> ephemeralPub;
    std::vector<uint8_t> oneTime = sender.generateOneTimeAddress(viewPub, spendPub, ephemeralPub);
    assert(oneTime.size() == 32);
    assert(ephemeralPub.size() == 32);

    assert(recipient.checkOwnership(oneTime, ephemeralPub) == true);

    StealthAddress other;
    assert(other.generateKeys());
    assert(other.checkOwnership(oneTime, ephemeralPub) == false);
}

static void testRing() {
    std::vector<uint8_t> message = {0x73, 0x79, 0x6e, 0x61, 0x70, 0x73, 0x65};

    std::vector<uint8_t> sk0 = randomScalar();
    std::vector<uint8_t> sk1 = randomScalar();
    std::vector<uint8_t> sk2 = randomScalar();

    std::vector<std::vector<uint8_t>> ring;
    ring.push_back(pointFromScalar(sk0));
    ring.push_back(pointFromScalar(sk1));
    ring.push_back(pointFromScalar(sk2));

    size_t signerIndex = 0;
    RingSignature sig = RingSign::sign(message, ring, sk0, signerIndex);
    assert(RingSign::verify(message, ring, sig) == true);

    RingSignature tampered = sig;
    assert(!tampered.responses.empty());
    tampered.responses[0][0] ^= 0xFF;
    assert(RingSign::verify(message, ring, tampered) == false);

    std::vector<std::vector<uint8_t>> used;
    used.push_back(sig.keyImage);
    assert(RingSign::isDoubleSpend(sig.keyImage, used) == true);
}

static void testConfidential() {
    std::vector<uint8_t> blinding = ConfidentialTx::generateBlindingFactor();
    PedersenCommitment c = ConfidentialTx::commit(100, blinding);
    assert(c.commitment.size() == 32);
    assert(c.verify(100) == true);
    assert(c.verify(99) == false);

    std::vector<uint8_t> ra = ConfidentialTx::generateBlindingFactor();
    std::vector<uint8_t> rb = ConfidentialTx::generateBlindingFactor();
    std::vector<std::vector<uint8_t>> factors;
    factors.push_back(ra);
    factors.push_back(rb);
    std::vector<uint8_t> rin = ConfidentialTx::blindingSum(factors, false);
    assert(rin.size() == 32);

    PedersenCommitment input = ConfidentialTx::commit(100, rin);
    PedersenCommitment out1 = ConfidentialTx::commit(70, ra);
    PedersenCommitment out2 = ConfidentialTx::commit(30, rb);

    std::vector<PedersenCommitment> inputs;
    inputs.push_back(input);
    std::vector<PedersenCommitment> outputs;
    outputs.push_back(out1);
    outputs.push_back(out2);

    assert(ConfidentialTx::verifyBalance(inputs, outputs, 0) == true);

    std::vector<PedersenCommitment> badOutputs;
    badOutputs.push_back(ConfidentialTx::commit(80, ra));
    badOutputs.push_back(ConfidentialTx::commit(30, rb));
    assert(ConfidentialTx::verifyBalance(inputs, badOutputs, 0) == false);
}

static void testPaymentOpen() {
    StealthAddress recipient;
    assert(recipient.generateKeys());
    StealthAddress sender;
    synapse::privacy::StealthPayment pay;
    assert(sender.createPayment(recipient.getViewPublicKey(), recipient.getSpendPublicKey(), 12345678ULL, pay));
    assert(pay.oneTimeAddress.size() == 32);
    assert(pay.commitment.size() == 32);
    assert(!pay.ecdh.empty());
    assert(recipient.checkOwnership(pay.oneTimeAddress, pay.ephemeralPub));

    uint64_t atoms = 0;
    std::vector<uint8_t> blinding;
    assert(recipient.tryOpenPayment(pay.ephemeralPub, pay.ecdh, atoms, blinding));
    assert(atoms == 12345678ULL);
    synapse::crypto::PedersenCommitment c;
    c.commitment = pay.commitment;
    c.blinding = blinding;
    assert(c.verify(atoms));

    StealthAddress other;
    assert(other.generateKeys());
    uint64_t bad = 0;
    std::vector<uint8_t> b2;
    assert(other.tryOpenPayment(pay.ephemeralPub, pay.ecdh, bad, b2) == false);
}

static void testPrivateSend() {
    using synapse::privacy::OwnedOutput;
    using synapse::privacy::PrivateSendResult;
    using synapse::privacy::mintOwnedOutput;
    using synapse::privacy::buildPrivateSend;
    using synapse::privacy::verifyPrivateTx;
    using synapse::privacy::scanOutput;

    StealthAddress alice;
    StealthAddress bob;
    assert(alice.generateKeys());
    assert(bob.generateKeys());

    std::vector<OwnedOutput> wallet;
    OwnedOutput minted;
    assert(mintOwnedOutput(alice, 500000000ULL, minted));
    wallet.push_back(minted);

    PrivateSendResult result;
    std::string err;
    std::vector<synapse::privacy::DecoyMember> decoys;
    assert(buildPrivateSend(alice, bob.encodeAddress(), 150000000ULL, wallet, decoys, result, err));
    assert(err.empty());
    assert(result.tx.vins.size() == 1);
    assert(result.tx.vins[0].ringP.size() == synapse::privacy::kPrivateRingSize);
    assert(result.tx.vouts.size() == 2);
    assert(verifyPrivateTx(result.tx, err));
    assert(err.empty());

    std::string pubDump;
    for (const auto& o : result.tx.vouts) {
        pubDump += synapse::crypto::toHex(o.oneTime);
        pubDump += synapse::crypto::toHex(o.commitment);
        pubDump += synapse::crypto::toHex(o.ecdh);
    }
    assert(pubDump.find(bob.encodeAddress().substr(2, 16)) == std::string::npos);

    int found = 0;
    uint64_t got = 0;
    for (const auto& o : result.tx.vouts) {
        OwnedOutput owned;
        if (scanOutput(bob, o, owned)) {
            found++;
            got = owned.amountAtoms;
        }
    }
    assert(found == 1);
    assert(got == 150000000ULL);
}

int main() {
    assert(sodium_init() >= 0);
    testStealth();
    testRing();
    testConfidential();
    testPaymentOpen();
    testPrivateSend();
    return 0;
}
