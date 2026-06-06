#include "privacy/privacy.h"
#include "crypto/ring_signature.h"
#include "crypto/confidential_tx.h"

#include <sodium.h>

#include <cassert>
#include <vector>
#include <cstdint>
#include <utility>

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

static void testRingFromPool() {
    std::vector<uint8_t> message = {0x70, 0x6f, 0x6f, 0x6c};

    std::vector<uint8_t> signerKey = randomScalar();
    std::vector<std::vector<uint8_t>> pool;
    for (int i = 0; i < 6; ++i) {
        pool.push_back(pointFromScalar(randomScalar()));
    }

    std::vector<std::vector<uint8_t>> ring;
    ring.push_back(pointFromScalar(signerKey));
    ring.push_back(pool[0]);
    ring.push_back(pool[1]);
    ring.push_back(pool[2]);

    size_t signerIndex = 0;
    size_t target = 2;
    std::swap(ring[signerIndex], ring[target]);
    signerIndex = target;

    RingSignature sig = RingSign::sign(message, ring, signerKey, signerIndex);
    assert(RingSign::verify(message, ring, sig) == true);

    std::vector<uint8_t> tamperedMessage = message;
    tamperedMessage[0] ^= 0xFF;
    assert(RingSign::verify(tamperedMessage, ring, sig) == false);
}

static void testConfidentialBalance() {
    std::vector<uint8_t> ra = ConfidentialTx::generateBlindingFactor();
    std::vector<uint8_t> rb = ConfidentialTx::generateBlindingFactor();
    std::vector<uint8_t> rc = ConfidentialTx::generateBlindingFactor();
    std::vector<uint8_t> rd = ConfidentialTx::generateBlindingFactor();

    std::vector<std::vector<uint8_t>> outFactors;
    outFactors.push_back(rc);
    outFactors.push_back(rd);
    std::vector<uint8_t> sumOut = ConfidentialTx::blindingSum(outFactors, false);

    std::vector<std::vector<uint8_t>> raVec;
    raVec.push_back(ra);
    std::vector<uint8_t> negRa = ConfidentialTx::blindingSum(raVec, true);

    std::vector<std::vector<uint8_t>> secondFactors;
    secondFactors.push_back(sumOut);
    secondFactors.push_back(negRa);
    std::vector<uint8_t> rin2 = ConfidentialTx::blindingSum(secondFactors, false);

    PedersenCommitment in1 = ConfidentialTx::commit(700, ra);
    PedersenCommitment in2 = ConfidentialTx::commit(300, rin2);
    PedersenCommitment out1 = ConfidentialTx::commit(600, rc);
    PedersenCommitment out2 = ConfidentialTx::commit(400, rd);

    std::vector<PedersenCommitment> inputs;
    inputs.push_back(in1);
    inputs.push_back(in2);
    std::vector<PedersenCommitment> outputs;
    outputs.push_back(out1);
    outputs.push_back(out2);

    assert(ConfidentialTx::verifyBalance(inputs, outputs, 0) == true);

    std::vector<PedersenCommitment> badOutputs;
    badOutputs.push_back(ConfidentialTx::commit(601, rc));
    badOutputs.push_back(ConfidentialTx::commit(400, rd));
    assert(ConfidentialTx::verifyBalance(inputs, badOutputs, 0) == false);
}

static void testDoubleSpend() {
    std::vector<uint8_t> message = {0x64, 0x73};
    std::vector<uint8_t> signerKey = randomScalar();
    std::vector<std::vector<uint8_t>> ring;
    ring.push_back(pointFromScalar(signerKey));
    ring.push_back(pointFromScalar(randomScalar()));
    ring.push_back(pointFromScalar(randomScalar()));

    RingSignature sig1 = RingSign::sign(message, ring, signerKey, 0);
    RingSignature sig2 = RingSign::sign(message, ring, signerKey, 0);

    assert(sig1.keyImage.size() == sig2.keyImage.size());
    assert(sodium_memcmp(sig1.keyImage.data(), sig2.keyImage.data(), sig1.keyImage.size()) == 0);

    std::vector<std::vector<uint8_t>> used;
    used.push_back(sig1.keyImage);
    assert(RingSign::isDoubleSpend(sig2.keyImage, used) == true);

    std::vector<uint8_t> fresh = pointFromScalar(randomScalar());
    assert(RingSign::isDoubleSpend(fresh, used) == false);
}

int main() {
    assert(sodium_init() >= 0);
    testRingFromPool();
    testConfidentialBalance();
    testDoubleSpend();
    return 0;
}
