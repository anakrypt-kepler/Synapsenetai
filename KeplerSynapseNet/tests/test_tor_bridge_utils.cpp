#include "core/tor_bridge_utils.h"

#include <cassert>
#include <vector>

static const char* kFpA = "0123456789ABCDEF0123456789ABCDEF01234567";
static const char* kFpB = "89abcdef0123456789abcdef0123456789abcdef";
static const char* kFpC = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
static const char* kFpD = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
static const char* kFpE = "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC";
static const char* kFpW = "1111111111111111111111111111111111111111";
static const char* kFpX = "2222222222222222222222222222222222222222";
static const char* kFpY = "3333333333333333333333333333333333333333";
static const char* kFpZ = "4444444444444444444444444444444444444444";

static void testNormalizeObfs4BridgeLineAcceptsBridgeAndShortForm() {
    {
        const auto r = synapse::core::normalizeObfs4BridgeLine(
            std::string("Bridge obfs4 1.2.3.4:443 ") + kFpA + " cert=abc iat-mode=0");
        assert(r.status == synapse::core::TorBridgeLineParseStatus::Valid);
        assert(r.normalized == std::string("Bridge obfs4 1.2.3.4:443 ") + kFpA + " cert=abc iat-mode=0");
    }
    {
        const auto r = synapse::core::normalizeObfs4BridgeLine(
            std::string("  obfs4 5.6.7.8:9001 ") + kFpB + " cert=xyz iat-mode=2  ");
        assert(r.status == synapse::core::TorBridgeLineParseStatus::Valid);
        assert(r.normalized == std::string("Bridge obfs4 5.6.7.8:9001 ") + kFpB + " cert=xyz iat-mode=2");
    }
}

static void testNormalizeObfs4BridgeLineSkipsEmptyAndComments() {
    assert(synapse::core::normalizeObfs4BridgeLine("").status ==
           synapse::core::TorBridgeLineParseStatus::Skip);
    assert(synapse::core::normalizeObfs4BridgeLine("   ").status ==
           synapse::core::TorBridgeLineParseStatus::Skip);
    assert(synapse::core::normalizeObfs4BridgeLine(" # comment").status ==
           synapse::core::TorBridgeLineParseStatus::Skip);
}

static void testNormalizeObfs4BridgeLineRejectsInvalidLines() {
    const auto r1 = synapse::core::normalizeObfs4BridgeLine("UseBridges 1");
    assert(r1.status == synapse::core::TorBridgeLineParseStatus::Invalid);

    const auto r2 = synapse::core::normalizeObfs4BridgeLine("Bridge snowflake 192.0.2.3:1");
    assert(r2.status == synapse::core::TorBridgeLineParseStatus::Invalid);

    const auto r3 = synapse::core::normalizeObfs4BridgeLine(
        std::string("Bridge obfs4 1.2.3.4:443 ") + kFpA + " iat-mode=0");
    assert(r3.status == synapse::core::TorBridgeLineParseStatus::Invalid);

    const auto r4 = synapse::core::normalizeObfs4BridgeLine(
        std::string("Bridge obfs4 1.2.3.4:443 ") + kFpA + " cert=abc");
    assert(r4.status == synapse::core::TorBridgeLineParseStatus::Invalid);

    const auto r5 = synapse::core::normalizeObfs4BridgeLine(
        "Bridge obfs4 1.2.3.4:70000 0123 cert=abc iat-mode=0");
    assert(r5.status == synapse::core::TorBridgeLineParseStatus::Invalid);

    const auto r6 = synapse::core::normalizeObfs4BridgeLine(
        "Bridge obfs4 1.2.3.4:443 NOTHEX cert=abc iat-mode=0");
    assert(r6.status == synapse::core::TorBridgeLineParseStatus::Invalid);
}

static void testSanitizeAndDedupeObfs4BridgeLinesPreservesOrder() {
    const std::vector<std::string> raw = {
        "  # comment",
        std::string("obfs4 1.2.3.4:443 ") + kFpA + " cert=a iat-mode=0",
        std::string("Bridge obfs4 1.2.3.4:443 ") + kFpA + " cert=a iat-mode=0",
        "UseBridges 1",
        std::string("Bridge obfs4 9.9.9.9:9001 ") + kFpB + " cert=b iat-mode=2",
        std::string("Bridge obfs4 9.9.9.9:9001 ") + kFpB + " cert=b iat-mode=2",
        "",
        std::string("obfs4 2.2.2.2:443 ") + kFpC + " cert=c iat-mode=1",
    };

    const auto out = synapse::core::sanitizeAndDedupeObfs4BridgeLines(raw);
    assert(out.size() == 3);
    assert(out[0] == std::string("Bridge obfs4 1.2.3.4:443 ") + kFpA + " cert=a iat-mode=0");
    assert(out[1] == std::string("Bridge obfs4 9.9.9.9:9001 ") + kFpB + " cert=b iat-mode=2");
    assert(out[2] == std::string("Bridge obfs4 2.2.2.2:443 ") + kFpC + " cert=c iat-mode=1");
}

static void testReadinessTransitions() {
    using synapse::core::TorReadinessState;

    {
        const auto events = synapse::core::describeTorReadinessTransitions(
            TorReadinessState{false, false, true},
            TorReadinessState{true, false, true});
        assert((events == std::vector<std::string>{"SOCKS_UP"}));
    }
    {
        const auto events = synapse::core::describeTorReadinessTransitions(
            TorReadinessState{true, false, true},
            TorReadinessState{true, true, false});
        assert((events == std::vector<std::string>{"WEB_READY_GAINED", "DEGRADED_EXIT"}));
    }
    {
        const auto events = synapse::core::describeTorReadinessTransitions(
            TorReadinessState{true, true, false},
            TorReadinessState{false, false, true});
        assert((events == std::vector<std::string>{"SOCKS_DOWN", "WEB_READY_LOST", "DEGRADED_ENTER"}));
    }
    {
        const auto events = synapse::core::describeTorReadinessTransitions(
            TorReadinessState{true, true, false},
            TorReadinessState{true, true, false});
        assert(events.empty());
    }
}

static void testReadinessTransitionsDeterministicReplay() {
    using synapse::core::TorReadinessState;
    const TorReadinessState prev{true, false, true};
    const TorReadinessState next{true, true, false};
    const auto baseline = synapse::core::describeTorReadinessTransitions(prev, next);
    for (int i = 0; i < 256; ++i) {
        const auto replay = synapse::core::describeTorReadinessTransitions(prev, next);
        assert(replay == baseline);
    }
}

static void testDeterministicBridgeSubsetSelectionReplay() {
    const std::vector<std::string> raw = {
        "# comment",
        std::string("Bridge obfs4 1.1.1.1:443 ") + kFpA + " cert=a iat-mode=0",
        std::string("obfs4 2.2.2.2:443 ") + kFpB + " cert=b iat-mode=0",
        std::string("Bridge obfs4 3.3.3.3:443 ") + kFpC + " cert=c iat-mode=1",
        std::string("Bridge obfs4 4.4.4.4:443 ") + kFpD + " cert=d iat-mode=2",
        std::string("Bridge obfs4 2.2.2.2:443 ") + kFpB + " cert=b iat-mode=0",
        "UseBridges 1",
        std::string("Bridge obfs4 5.5.5.5:443 ") + kFpE + " cert=e iat-mode=0",
    };

    const auto baseline = synapse::core::selectDeterministicObfs4BridgeSubset(
        raw, "node-alpha", 42, 3);

    assert(baseline.size() == 3);
    for (int i = 0; i < 512; ++i) {
        const auto replay = synapse::core::selectDeterministicObfs4BridgeSubset(
            raw, "node-alpha", 42, 3);
        assert(replay == baseline);
    }

    const auto sameInputsDifferentEpoch = synapse::core::selectDeterministicObfs4BridgeSubset(
        raw, "node-alpha", 43, 3);
    const auto sameInputsDifferentNode = synapse::core::selectDeterministicObfs4BridgeSubset(
        raw, "node-beta", 42, 3);

    const auto sanitizedAll = synapse::core::sanitizeAndDedupeObfs4BridgeLines(raw);
    assert(baseline.size() <= sanitizedAll.size());
    assert(sameInputsDifferentEpoch.size() == 3);
    assert(sameInputsDifferentNode.size() == 3);

    const auto none = synapse::core::selectDeterministicObfs4BridgeSubset(raw, "node-alpha", 42, 0);
    assert(none.empty());

    const auto all = synapse::core::selectDeterministicObfs4BridgeSubset(raw, "node-alpha", 42, 99);
    assert(all == sanitizedAll);
}

static void testDeterministicBridgeSubsetSelectionIndependentOfInputOrderAfterSanitize() {
    const std::vector<std::string> a = {
        std::string("obfs4 9.9.9.9:443 ") + kFpZ + " cert=z iat-mode=0",
        std::string("Bridge obfs4 8.8.8.8:443 ") + kFpY + " cert=y iat-mode=0",
        std::string("Bridge obfs4 7.7.7.7:443 ") + kFpX + " cert=x iat-mode=0",
        std::string("Bridge obfs4 6.6.6.6:443 ") + kFpW + " cert=w iat-mode=0",
    };
    const std::vector<std::string> b = {
        std::string("Bridge obfs4 6.6.6.6:443 ") + kFpW + " cert=w iat-mode=0",
        std::string("Bridge obfs4 7.7.7.7:443 ") + kFpX + " cert=x iat-mode=0",
        std::string("Bridge obfs4 8.8.8.8:443 ") + kFpY + " cert=y iat-mode=0",
        std::string("obfs4 9.9.9.9:443 ") + kFpZ + " cert=z iat-mode=0",
    };

    const auto sa = synapse::core::selectDeterministicObfs4BridgeSubset(a, "node-gamma", 99, 2);
    const auto sb = synapse::core::selectDeterministicObfs4BridgeSubset(b, "node-gamma", 99, 2);
    assert(sa == sb);
}

int main() {
    testNormalizeObfs4BridgeLineAcceptsBridgeAndShortForm();
    testNormalizeObfs4BridgeLineSkipsEmptyAndComments();
    testNormalizeObfs4BridgeLineRejectsInvalidLines();
    testSanitizeAndDedupeObfs4BridgeLinesPreservesOrder();
    testReadinessTransitions();
    testReadinessTransitionsDeterministicReplay();
    testDeterministicBridgeSubsetSelectionReplay();
    testDeterministicBridgeSubsetSelectionIndependentOfInputOrderAfterSanitize();
    return 0;
}
