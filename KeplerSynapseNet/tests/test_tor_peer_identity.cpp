#include "core/tor_peer_identity.h"

#include <cassert>
#include <string>

static void testPeerHelloRoundTrip() {
    const std::string onionHost(56, 'a');
    synapse::core::PeerHelloInfo hello;
    hello.transport = "tor-onion";
    hello.advertisedHost = onionHost + ".onion";
    hello.advertisedPort = 8333;
    hello.displayId = "tor:seed";

    const auto payload = synapse::core::serializePeerHelloPayload(hello);
    const auto parsed = synapse::core::parsePeerHelloPayload(payload);
    assert(parsed.has_value());
    assert(parsed->transport == "tor-onion");
    assert(parsed->advertisedHost == hello.advertisedHost);
    assert(parsed->advertisedPort == 8333);
    assert(parsed->displayId == "tor:seed");
}

static void testMalformedPeerHelloIsIgnored() {
    const std::string bad = "{not-json";
    const std::vector<uint8_t> payload(bad.begin(), bad.end());
    const auto parsed = synapse::core::parsePeerHelloPayload(payload);
    assert(!parsed.has_value());
}

static void testPeerDisplayPrefersOnionThenDisplayIdThenRaw() {
    const std::string onionHost(56, 'b');
    synapse::core::PeerHelloInfo onion;
    onion.transport = "tor-onion";
    onion.advertisedHost = onionHost + ".onion";
    onion.advertisedPort = 8333;
    auto display = synapse::core::selectPeerDisplayInfo("127.0.0.1", 44920, onion);
    assert(display.transport == "tor-onion");
    assert(display.displayAddress == onion.advertisedHost + ":8333");
    assert(display.rawAddress == "127.0.0.1");
    assert(display.tor);

    synapse::core::PeerHelloInfo socks;
    socks.transport = "tor-socks";
    socks.displayId = "tor:abc123";
    display = synapse::core::selectPeerDisplayInfo("127.0.0.1", 44920, socks);
    assert(display.transport == "tor-socks");
    assert(display.displayAddress == "tor:abc123");
    assert(display.tor);

    display = synapse::core::selectPeerDisplayInfo("144.31.169.103", 8333, std::nullopt);
    assert(display.transport == "clearnet");
    assert(display.displayAddress == "144.31.169.103:8333");
    assert(!display.tor);
}

int main() {
    testPeerHelloRoundTrip();
    testMalformedPeerHelloIsIgnored();
    testPeerDisplayPrefersOnionThenDisplayIdThenRaw();
    return 0;
}
