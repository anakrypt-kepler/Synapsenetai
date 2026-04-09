#include "core/consensus.h"
#include <cassert>
#include <cstdint>
#include <vector>

static synapse::crypto::PublicKey makePk(uint8_t tag) {
    synapse::crypto::PublicKey pk{};
    for (size_t i = 0; i < pk.size(); ++i) {
        pk[i] = static_cast<uint8_t>(tag + static_cast<uint8_t>(i));
    }
    return pk;
}

static synapse::core::Validator makeValidator(uint8_t tag, uint64_t stake, double rep) {
    synapse::core::Validator v{};
    v.pubKey = makePk(tag);
    v.address = synapse::crypto::toHex(v.pubKey);
    v.stake = stake;
    v.reputation = rep;
    v.validationsCompleted = 0;
    v.lastActive = 1;
    v.eligible = true;
    v.active = true;
    v.totalRewards = 0;
    return v;
}

static void testSelectionDeterministicAcrossRegistrationOrder() {
    synapse::core::Consensus c1;
    synapse::core::Consensus c2;

    auto v1 = makeValidator(10, 500, 1.2);
    auto v2 = makeValidator(20, 900, 0.9);
    auto v3 = makeValidator(30, 300, 1.5);
    auto v4 = makeValidator(40, 800, 1.1);
    auto v5 = makeValidator(50, 1200, 0.8);

    assert(c1.registerValidator(v1));
    assert(c1.registerValidator(v2));
    assert(c1.registerValidator(v3));
    assert(c1.registerValidator(v4));
    assert(c1.registerValidator(v5));

    assert(c2.registerValidator(v3));
    assert(c2.registerValidator(v1));
    assert(c2.registerValidator(v5));
    assert(c2.registerValidator(v2));
    assert(c2.registerValidator(v4));

    const uint64_t eventId = 0x12345678ULL;
    auto s1 = c1.selectValidators(eventId, 3);
    auto s2 = c2.selectValidators(eventId, 3);

    assert(s1.size() == 3);
    assert(s2.size() == 3);
    for (size_t i = 0; i < s1.size(); ++i) {
        assert(s1[i].pubKey == s2[i].pubKey);
    }
}

int main() {
    testSelectionDeterministicAcrossRegistrationOrder();
    return 0;
}
