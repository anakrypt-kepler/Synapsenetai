#pragma once

#include <cstdint>

namespace synapse {

constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr uint32_t MIN_PROTOCOL_VERSION = 1;

constexpr uint16_t DEFAULT_PORT = 8333;
constexpr uint16_t DEFAULT_RPC_PORT = 8334;

constexpr uint32_t MAX_MESSAGE_SIZE = 4 * 1024 * 1024;
constexpr uint32_t MAX_PEERS = 125;
constexpr uint32_t MAX_OUTBOUND = 8;

constexpr uint64_t BLOCK_INTERVAL_MS = 60000;
constexpr uint32_t MAX_BLOCK_SIZE = 1024 * 1024;

constexpr uint64_t MIN_STAKE = 10;
constexpr uint64_t VALIDATOR_STAKE = 100;

constexpr uint32_t COMMUNITY_SERVICE_COUNT = 50;

namespace msg {
    constexpr const char* VERSION = "version";
    constexpr const char* VERACK = "verack";
    constexpr const char* PING = "ping";
    constexpr const char* PONG = "pong";
    constexpr const char* GETPEERS = "getpeers";
    constexpr const char* PEERS = "peers";
    constexpr const char* KNOWLEDGE = "knowledge";
    constexpr const char* TRANSFER = "transfer";
    constexpr const char* BLOCK = "block";
    constexpr const char* GETBLOCKS = "getblocks";
    constexpr const char* INV = "inv";
    constexpr const char* GETDATA = "getdata";
}

}
