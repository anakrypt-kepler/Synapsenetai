#include "core/tor_peer_identity.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>

namespace synapse::core {

using json = nlohmann::json;

namespace {

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isOnionHostChar(char ch) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    return (uch >= 'a' && uch <= 'z') || (uch >= '2' && uch <= '7');
}

} // namespace

std::string normalizePeerTransport(std::string transport) {
    transport = toLowerAscii(std::move(transport));
    if (transport == "clearnet" || transport == "tor-socks" || transport == "tor-onion") {
        return transport;
    }
    return {};
}

bool isValidOnionHost(const std::string& host) {
    if (host.size() <= 6) return false;
    if (host.substr(host.size() - 6) != ".onion") return false;
    const std::string id = host.substr(0, host.size() - 6);
    if (id.size() != 56) return false;
    return std::all_of(id.begin(), id.end(), [](char ch) { return isOnionHostChar(ch); });
}

namespace {

std::string inferTransport(const PeerHelloInfo& info) {
    if (isValidOnionHost(info.advertisedHost)) return "tor-onion";
    if (!info.displayId.empty()) return "tor-socks";
    return "clearnet";
}

} // namespace

std::vector<uint8_t> serializePeerHelloPayload(const PeerHelloInfo& info) {
    json out = json::object();
    const std::string transport = normalizePeerTransport(info.transport).empty()
        ? inferTransport(info)
        : normalizePeerTransport(info.transport);
    out["transport"] = transport;
    out["advertisedHost"] = info.advertisedHost;
    out["advertisedPort"] = info.advertisedPort;
    out["displayId"] = info.displayId;
    const std::string text = out.dump();
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::optional<PeerHelloInfo> parsePeerHelloPayload(const std::vector<uint8_t>& payload) {
    if (payload.empty()) return std::nullopt;

    try {
        json in = json::parse(payload.begin(), payload.end());
        if (!in.is_object()) return std::nullopt;

        PeerHelloInfo info;
        if (in.contains("transport") && in["transport"].is_string()) {
            info.transport = normalizePeerTransport(in["transport"].get<std::string>());
        }
        if (in.contains("advertisedHost") && in["advertisedHost"].is_string()) {
            info.advertisedHost = toLowerAscii(in["advertisedHost"].get<std::string>());
        }
        if (in.contains("advertisedPort") && in["advertisedPort"].is_number_unsigned()) {
            const auto port = in["advertisedPort"].get<uint64_t>();
            if (port <= 65535) info.advertisedPort = static_cast<uint16_t>(port);
        } else if (in.contains("advertisedPort") && in["advertisedPort"].is_number_integer()) {
            const auto port = in["advertisedPort"].get<int64_t>();
            if (port > 0 && port <= 65535) info.advertisedPort = static_cast<uint16_t>(port);
        }
        if (in.contains("displayId") && in["displayId"].is_string()) {
            info.displayId = in["displayId"].get<std::string>();
        }

        if (!info.advertisedHost.empty() && !isValidOnionHost(info.advertisedHost)) {
            info.advertisedHost.clear();
        }

        if (info.transport.empty()) {
            info.transport = inferTransport(info);
        }

        return info;
    } catch (...) {
        return std::nullopt;
    }
}

std::string formatPeerAddress(const std::string& host, uint16_t port) {
    if (host.empty()) return {};
    if (port == 0) return host;
    return host + ":" + std::to_string(port);
}

PeerDisplayInfo selectPeerDisplayInfo(const std::string& rawAddress,
                                      uint16_t rawPort,
                                      const std::optional<PeerHelloInfo>& peerHello) {
    PeerDisplayInfo out;
    out.rawAddress = rawAddress;
    out.rawPort = rawPort;
    out.transport = rawAddress.size() > 6 && rawAddress.substr(rawAddress.size() - 6) == ".onion"
        ? "tor-onion"
        : "clearnet";

    if (peerHello) {
        out.transport = normalizePeerTransport(peerHello->transport);
        if (out.transport.empty()) {
            out.transport = inferTransport(*peerHello);
        }
        if (isValidOnionHost(peerHello->advertisedHost)) {
            out.displayAddress = formatPeerAddress(peerHello->advertisedHost, peerHello->advertisedPort);
        } else if ((out.transport == "tor-socks" || out.transport == "tor-onion") &&
                   !peerHello->displayId.empty()) {
            out.displayAddress = peerHello->displayId;
        }
    }

    if (out.displayAddress.empty()) {
        out.displayAddress = formatPeerAddress(rawAddress, rawPort);
    }

    out.tor = (out.transport == "tor-socks" || out.transport == "tor-onion");
    return out;
}

std::string peerTransportLabel(const std::string& transport) {
    if (transport == "tor-onion") return "TOR ONION";
    if (transport == "tor-socks") return "TOR SOCKS";
    return "CLEARNET";
}

}
