#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace synapse::core {

struct PeerHelloInfo {
    std::string transport = "clearnet";
    std::string advertisedHost;
    uint16_t advertisedPort = 0;
    std::string displayId;
};

struct PeerDisplayInfo {
    std::string transport = "clearnet";
    std::string displayAddress;
    std::string rawAddress;
    uint16_t rawPort = 0;
    bool tor = false;
};

std::string normalizePeerTransport(std::string transport);

bool isValidOnionHost(const std::string& host);

std::vector<uint8_t> serializePeerHelloPayload(const PeerHelloInfo& info);

std::optional<PeerHelloInfo> parsePeerHelloPayload(const std::vector<uint8_t>& payload);

PeerDisplayInfo selectPeerDisplayInfo(const std::string& rawAddress,
                                      uint16_t rawPort,
                                      const std::optional<PeerHelloInfo>& peerHello);

std::string formatPeerAddress(const std::string& host, uint16_t port);

std::string peerTransportLabel(const std::string& transport);

}
