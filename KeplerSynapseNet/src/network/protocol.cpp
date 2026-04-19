#include "network/network.h"
#include "crypto/crypto.h"
#include <string>
#include <vector>
#include <map>
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <cctype>
#include <openssl/rand.h>

namespace synapse {
namespace network {

static const uint32_t LOCAL_PROTOCOL_VERSION = 70015;
static const uint32_t MIN_PROTOCOL_VERSION = 70001;
static const uint32_t MAGIC_MAINNET = 0xD9B4BEF9;
static const uint32_t MAGIC_TESTNET = 0x0709110B;
static const uint32_t MAGIC_REGTEST = 0xDAB5BFFA;
static const size_t LOCAL_MAX_MESSAGE_SIZE = 32 * 1024 * 1024;
static constexpr uint64_t MAX_VAR_STRING = 4 * 1024 * 1024;
static const size_t MAX_INV_SIZE = 50000;
static const size_t MAX_HEADERS_SIZE = 2000;
static const size_t MAX_ADDR_SIZE = 1000;

enum class LocalMessageType : uint8_t {
    Version = 0x01,
    Verack = 0x02,
    Ping = 0x03,
    Pong = 0x04,
    GetAddr = 0x05,
    Addr = 0x06,
    Inv = 0x07,
    GetData = 0x08,
    NotFound = 0x09,
    GetBlocks = 0x0A,
    GetHeaders = 0x0B,
    Headers = 0x0C,
    Block = 0x0D,
    Tx = 0x0E,
    Reject = 0x0F,
    MemPool = 0x10,
    FilterLoad = 0x11,
    FilterAdd = 0x12,
    FilterClear = 0x13,
    MerkleBlock = 0x14,
    Alert = 0x15,
    SendHeaders = 0x16,
    FeeFilter = 0x17,
    SendCmpct = 0x18,
    CmpctBlock = 0x19,
    GetBlockTxn = 0x1A,
    BlockTxn = 0x1B,
    Knowledge = 0x20,
    GetKnowledge = 0x21,
    Model = 0x22,
    GetModel = 0x23,
    Inference = 0x24,
    InferenceResult = 0x25
};

enum class InvType : uint32_t {
    Error = 0,
    Tx = 1,
    Block = 2,
    FilteredBlock = 3,
    CmpctBlock = 4,
    WitnessTx = 0x40000001,
    WitnessBlock = 0x40000002,
    FilteredWitnessBlock = 0x40000003,
    Knowledge = 0x50000001,
    Model = 0x50000002
};

enum class RejectCode : uint8_t {
    Malformed = 0x01,
    Invalid = 0x10,
    Obsolete = 0x11,
    Duplicate = 0x12,
    NonStandard = 0x40,
    Dust = 0x41,
    InsufficientFee = 0x42,
    Checkpoint = 0x43
};

struct LocalMessageHeader {
    uint32_t magic;
    char command[12];
    uint32_t length;
    uint32_t checksum;
};

struct NetAddress {
    uint64_t services;
    uint8_t ip[16];
    uint16_t port;
    uint32_t time;
};

struct VersionMessage {
    uint32_t version;
    uint64_t services;
    int64_t timestamp;
    NetAddress addrRecv;
    NetAddress addrFrom;
    uint64_t nonce;
    std::string userAgent;
    int32_t startHeight;
    bool relay;
};

struct InvItem {
    InvType type;
    uint8_t hash[32];
};

struct BlockHeader {
    int32_t version;
    uint8_t prevBlock[32];
    uint8_t merkleRoot[32];
    uint32_t timestamp;
    uint32_t bits;
    uint32_t nonce;
};

class MessageSerializer {
public:
    static std::vector<uint8_t> serializeVersion(const VersionMessage& msg) {
        std::vector<uint8_t> data;
        data.reserve(128);

        appendUint32(data, msg.version);
        appendUint64(data, msg.services);
        appendInt64(data, msg.timestamp);
        appendNetAddress(data, msg.addrRecv);
        appendNetAddress(data, msg.addrFrom);
        appendUint64(data, msg.nonce);
        appendVarString(data, msg.userAgent);
        appendInt32(data, msg.startHeight);
        appendBool(data, msg.relay);

        return data;
    }

    static VersionMessage deserializeVersion(const std::vector<uint8_t>& data) {
        VersionMessage msg;
        size_t offset = 0;

        msg.version = readUint32(data, offset);
        msg.services = readUint64(data, offset);
        msg.timestamp = readInt64(data, offset);
        msg.addrRecv = readNetAddress(data, offset);
        msg.addrFrom = readNetAddress(data, offset);
        msg.nonce = readUint64(data, offset);
        msg.userAgent = readVarString(data, offset);
        msg.startHeight = readInt32(data, offset);
        msg.relay = (offset < data.size()) ? readBool(data, offset) : true;

        return msg;
    }

    static std::vector<uint8_t> serializePing(uint64_t nonce) {
        std::vector<uint8_t> data;
        appendUint64(data, nonce);
        return data;
    }

    static uint64_t deserializePing(const std::vector<uint8_t>& data) {
        size_t offset = 0;
        return readUint64(data, offset);
    }

    static std::vector<uint8_t> serializeAddr(const std::vector<NetAddress>& addrs) {
        std::vector<uint8_t> data;
        appendVarInt(data, addrs.size());
        for (const auto& addr : addrs) {
            appendNetAddress(data, addr);
        }
        return data;
    }

    static std::vector<NetAddress> deserializeAddr(const std::vector<uint8_t>& data) {
        std::vector<NetAddress> addrs;
        size_t offset = 0;
        uint64_t count = readVarInt(data, offset);

        if (count > MAX_ADDR_SIZE) {
            count = MAX_ADDR_SIZE;
        }

        for (uint64_t i = 0; i < count && offset < data.size(); i++) {
            addrs.push_back(readNetAddress(data, offset));
        }

        return addrs;
    }

    static std::vector<uint8_t> serializeInv(const std::vector<InvItem>& items) {
        std::vector<uint8_t> data;
        appendVarInt(data, items.size());
        for (const auto& item : items) {
            appendUint32(data, static_cast<uint32_t>(item.type));
            data.insert(data.end(), item.hash, item.hash + 32);
        }
        return data;
    }

    static std::vector<InvItem> deserializeInv(const std::vector<uint8_t>& data) {
        std::vector<InvItem> items;
        size_t offset = 0;
        uint64_t count = readVarInt(data, offset);

        if (count > MAX_INV_SIZE) {
            count = MAX_INV_SIZE;
        }

        for (uint64_t i = 0; i < count && offset + 36 <= data.size(); i++) {
            InvItem item;
            item.type = static_cast<InvType>(readUint32(data, offset));
            std::memcpy(item.hash, &data[offset], 32);
            offset += 32;
            items.push_back(item);
        }

        return items;
    }

private:
    static void appendUint8(std::vector<uint8_t>& data, uint8_t val) {
        data.push_back(val);
    }

    static void appendUint16(std::vector<uint8_t>& data, uint16_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
    }

    static void appendUint32(std::vector<uint8_t>& data, uint32_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
        data.push_back((val >> 16) & 0xFF);
        data.push_back((val >> 24) & 0xFF);
    }

    static void appendUint64(std::vector<uint8_t>& data, uint64_t val) {
        for (int i = 0; i < 8; i++) {
            data.push_back((val >> (i * 8)) & 0xFF);
        }
    }

    static void appendInt32(std::vector<uint8_t>& data, int32_t val) {
        appendUint32(data, static_cast<uint32_t>(val));
    }

    static void appendInt64(std::vector<uint8_t>& data, int64_t val) {
        appendUint64(data, static_cast<uint64_t>(val));
    }

    static void appendBool(std::vector<uint8_t>& data, bool val) {
        data.push_back(val ? 1 : 0);
    }

    static void appendVarInt(std::vector<uint8_t>& data, uint64_t val) {
        if (val < 0xFD) {
            data.push_back(static_cast<uint8_t>(val));
        } else if (val <= 0xFFFF) {
            data.push_back(0xFD);
            appendUint16(data, static_cast<uint16_t>(val));
        } else if (val <= 0xFFFFFFFF) {
            data.push_back(0xFE);
            appendUint32(data, static_cast<uint32_t>(val));
        } else {
            data.push_back(0xFF);
            appendUint64(data, val);
        }
    }

    static void appendVarString(std::vector<uint8_t>& data, const std::string& str) {
        appendVarInt(data, str.size());
        data.insert(data.end(), str.begin(), str.end());
    }

    static void appendNetAddress(std::vector<uint8_t>& data, const NetAddress& addr) {
        appendUint32(data, addr.time);
        appendUint64(data, addr.services);
        data.insert(data.end(), addr.ip, addr.ip + 16);
        data.push_back((addr.port >> 8) & 0xFF);
        data.push_back(addr.port & 0xFF);
    }

    static uint8_t readUint8(const std::vector<uint8_t>& data, size_t& offset) {
        if (offset + 1 > data.size()) return 0;
        return data[offset++];
    }

    static uint16_t readUint16(const std::vector<uint8_t>& data, size_t& offset) {
        if (offset + 2 > data.size()) { offset = data.size(); return 0; }
        uint16_t val = data[offset] | (data[offset + 1] << 8);
        offset += 2;
        return val;
    }

    static uint32_t readUint32(const std::vector<uint8_t>& data, size_t& offset) {
        if (offset + 4 > data.size()) { offset = data.size(); return 0; }
        uint32_t val = data[offset] | (data[offset + 1] << 8) |
                       (data[offset + 2] << 16) | (data[offset + 3] << 24);
        offset += 4;
        return val;
    }

    static uint64_t readUint64(const std::vector<uint8_t>& data, size_t& offset) {
        if (offset + 8 > data.size()) { offset = data.size(); return 0; }
        uint64_t val = 0;
        for (int i = 0; i < 8; i++) {
            val |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
        }
        offset += 8;
        return val;
    }

    static int32_t readInt32(const std::vector<uint8_t>& data, size_t& offset) {
        return static_cast<int32_t>(readUint32(data, offset));
    }

    static int64_t readInt64(const std::vector<uint8_t>& data, size_t& offset) {
        return static_cast<int64_t>(readUint64(data, offset));
    }

    static bool readBool(const std::vector<uint8_t>& data, size_t& offset) {
        if (offset + 1 > data.size()) return false;
        return data[offset++] != 0;
    }

    static uint64_t readVarInt(const std::vector<uint8_t>& data, size_t& offset) {
        if (offset + 1 > data.size()) return 0;
        uint8_t first = data[offset++];
        if (first < 0xFD) {
            return first;
        } else if (first == 0xFD) {
            return readUint16(data, offset);
        } else if (first == 0xFE) {
            return readUint32(data, offset);
        } else {
            return readUint64(data, offset);
        }
    }

    static std::string readVarString(const std::vector<uint8_t>& data, size_t& offset) {
        uint64_t len = readVarInt(data, offset);
        if (len == 0) return std::string();
        if (len > MAX_VAR_STRING || len > data.size() - offset) { offset = data.size(); return std::string(); }
        std::string str(data.begin() + offset, data.begin() + offset + len);
        offset += len;
        return str;
    }

    static NetAddress readNetAddress(const std::vector<uint8_t>& data, size_t& offset) {
        NetAddress addr;
        addr.time = readUint32(data, offset);
        addr.services = readUint64(data, offset);
        if (offset + 16 > data.size()) { std::memset(addr.ip, 0, 16); offset = data.size(); addr.port = 0; return addr; }
        std::memcpy(addr.ip, &data[offset], 16);
        offset += 16;
        if (offset + 2 > data.size()) { addr.port = 0; offset = data.size(); return addr; }
        addr.port = (data[offset] << 8) | data[offset + 1];
        offset += 2;
        return addr;
    }
};

class MessageBuilder {
private:
    uint32_t magic;
    std::vector<uint8_t> buffer;

public:
    MessageBuilder(uint32_t networkMagic = MAGIC_MAINNET) : magic(networkMagic) {}

    std::vector<uint8_t> buildMessage(const std::string& command, const std::vector<uint8_t>& payload) {
        if (payload.size() > LOCAL_MAX_MESSAGE_SIZE) return {};
        std::vector<uint8_t> message;
        message.reserve(24 + payload.size());

        message.push_back(magic & 0xFF);
        message.push_back((magic >> 8) & 0xFF);
        message.push_back((magic >> 16) & 0xFF);
        message.push_back((magic >> 24) & 0xFF);

        for (size_t i = 0; i < 12; i++) {
            message.push_back(i < command.size() ? command[i] : 0);
        }

        uint32_t length = payload.size();
        message.push_back(length & 0xFF);
        message.push_back((length >> 8) & 0xFF);
        message.push_back((length >> 16) & 0xFF);
        message.push_back((length >> 24) & 0xFF);

        uint32_t checksum = calculateChecksum(payload);
        message.push_back(checksum & 0xFF);
        message.push_back((checksum >> 8) & 0xFF);
        message.push_back((checksum >> 16) & 0xFF);
        message.push_back((checksum >> 24) & 0xFF);

        message.insert(message.end(), payload.begin(), payload.end());

        return message;
    }

    std::vector<uint8_t> buildVersionMessage(const VersionMessage& version) {
        auto payload = MessageSerializer::serializeVersion(version);
        return buildMessage("version", payload);
    }

    std::vector<uint8_t> buildVerackMessage() {
        return buildMessage("verack", {});
    }

    std::vector<uint8_t> buildPingMessage(uint64_t nonce) {
        auto payload = MessageSerializer::serializePing(nonce);
        return buildMessage("ping", payload);
    }

    std::vector<uint8_t> buildPongMessage(uint64_t nonce) {
        auto payload = MessageSerializer::serializePing(nonce);
        return buildMessage("pong", payload);
    }

    std::vector<uint8_t> buildGetAddrMessage() {
        return buildMessage("getaddr", {});
    }

    std::vector<uint8_t> buildAddrMessage(const std::vector<NetAddress>& addrs) {
        auto payload = MessageSerializer::serializeAddr(addrs);
        return buildMessage("addr", payload);
    }

    std::vector<uint8_t> buildInvMessage(const std::vector<InvItem>& items) {
        auto payload = MessageSerializer::serializeInv(items);
        return buildMessage("inv", payload);
    }

    std::vector<uint8_t> buildGetDataMessage(const std::vector<InvItem>& items) {
        auto payload = MessageSerializer::serializeInv(items);
        return buildMessage("getdata", payload);
    }

    std::vector<uint8_t> buildNotFoundMessage(const std::vector<InvItem>& items) {
        auto payload = MessageSerializer::serializeInv(items);
        return buildMessage("notfound", payload);
    }

    std::vector<uint8_t> buildMemPoolMessage() {
        return buildMessage("mempool", {});
    }

    std::vector<uint8_t> buildSendHeadersMessage() {
        return buildMessage("sendheaders", {});
    }

private:
    uint32_t calculateChecksum(const std::vector<uint8_t>& data) {
        auto hash = crypto::doubleSha256(data.data(), data.size());
        return static_cast<uint32_t>(hash[0]) |
               (static_cast<uint32_t>(hash[1]) << 8) |
               (static_cast<uint32_t>(hash[2]) << 16) |
               (static_cast<uint32_t>(hash[3]) << 24);
    }
};

class MessageParser {
private:
    std::vector<uint8_t> buffer;
    uint32_t expectedMagic;
    size_t headerSize;

    static bool isValidCommandField(const uint8_t* p, size_t len) {
        bool seenNull = false;
        for (size_t i = 0; i < len; ++i) {
            uint8_t c = p[i];
            if (c == 0) {
                seenNull = true;
                continue;
            }
            if (seenNull) return false;
            if (!std::isprint(static_cast<unsigned char>(c))) return false;
        }
        return true;
    }

    static uint32_t calculateChecksum(const std::vector<uint8_t>& data) {
        auto hash = crypto::doubleSha256(data.data(), data.size());
        return static_cast<uint32_t>(hash[0]) |
               (static_cast<uint32_t>(hash[1]) << 8) |
               (static_cast<uint32_t>(hash[2]) << 16) |
               (static_cast<uint32_t>(hash[3]) << 24);
    }

public:
    MessageParser(uint32_t magic = MAGIC_MAINNET) : expectedMagic(magic), headerSize(24) {}

    void feed(const uint8_t* data, size_t len) {
        const size_t limit = LOCAL_MAX_MESSAGE_SIZE * 2;
        if (buffer.size() >= limit) return;
        size_t allowed = (limit > buffer.size()) ? (limit - buffer.size()) : 0;
        size_t toInsert = std::min(len, allowed);
        if (toInsert == 0) return;
        buffer.insert(buffer.end(), data, data + toInsert);
    }

    void feed(const std::vector<uint8_t>& data) {
        const size_t limit = LOCAL_MAX_MESSAGE_SIZE * 2;
        if (buffer.size() >= limit) return;
        size_t allowed = (limit > buffer.size()) ? (limit - buffer.size()) : 0;
        size_t toInsert = std::min(data.size(), allowed);
        if (toInsert == 0) return;
        buffer.insert(buffer.end(), data.begin(), data.begin() + toInsert);
    }

    bool hasCompleteMessage() const {
        if (buffer.size() < headerSize) return false;

        uint32_t magic = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
        if (magic != expectedMagic) return true;

        uint32_t length = buffer[16] | (buffer[17] << 8) | (buffer[18] << 16) | (buffer[19] << 24);
        if (length > LOCAL_MAX_MESSAGE_SIZE) return true;
        return buffer.size() >= headerSize + length;
    }

    struct ParsedMessage {
        std::string command;
        std::vector<uint8_t> payload;
        bool valid;
    };

    ParsedMessage getNextMessage() {
        ParsedMessage msg;
        msg.valid = false;

        if (!hasCompleteMessage()) return msg;

        uint32_t magic = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
        if (magic != expectedMagic) {
            uint8_t magicBytes[4];
            magicBytes[0] = expectedMagic & 0xFF;
            magicBytes[1] = (expectedMagic >> 8) & 0xFF;
            magicBytes[2] = (expectedMagic >> 16) & 0xFF;
            magicBytes[3] = (expectedMagic >> 24) & 0xFF;
            auto it = std::search(buffer.begin() + 1, buffer.end(), magicBytes, magicBytes + 4);
            if (it == buffer.end()) {
                buffer.clear();
                return msg;
            } else {
                buffer.erase(buffer.begin(), it);
                return msg;
            }
        }

        if (!isValidCommandField(buffer.data() + 4, 12)) {
            buffer.erase(buffer.begin(), buffer.begin() + headerSize);
            return msg;
        }

        msg.command = std::string(reinterpret_cast<char*>(&buffer[4]), 12);
        msg.command = msg.command.c_str();

        uint32_t length = buffer[16] | (buffer[17] << 8) | (buffer[18] << 16) | (buffer[19] << 24);

        if (length > LOCAL_MAX_MESSAGE_SIZE) {
            buffer.erase(buffer.begin(), buffer.begin() + headerSize);
            return msg;
        }

        uint32_t checksum = buffer[20] | (buffer[21] << 8) | (buffer[22] << 16) | (buffer[23] << 24);
        msg.payload.assign(buffer.begin() + headerSize, buffer.begin() + headerSize + length);
        uint32_t calc = calculateChecksum(msg.payload);
        if (checksum != calc) {
            buffer.erase(buffer.begin(), buffer.begin() + headerSize + length);
            msg.payload.clear();
            return msg;
        }

        buffer.erase(buffer.begin(), buffer.begin() + headerSize + length);
        msg.valid = true;

        return msg;
    }

    void clear() {
        buffer.clear();
    }

    size_t bufferedSize() const {
        return buffer.size();
    }
};

class ProtocolHandler {
private:
    MessageBuilder builder;
    MessageParser parser;
    uint32_t protocolVersion;
    uint64_t localNonce;
    std::string userAgent;
    int32_t startHeight;
    uint64_t services;
    bool handshakeComplete;
    bool sendHeaders;
    std::mt19937_64 rng;

public:
    ProtocolHandler() : protocolVersion(LOCAL_PROTOCOL_VERSION), startHeight(0),
                        services(1), handshakeComplete(false), sendHeaders(false),
                        rng(std::random_device{}()) {
        // initialize localNonce with secure RNG
        if (1 != RAND_bytes(reinterpret_cast<unsigned char*>(&localNonce), sizeof(localNonce))) {
            // fallback to PRNG if RAND_bytes fails
            localNonce = rng();
        }
        userAgent = "/SynapseNet:0.1.0/";
    }

    void setStartHeight(int32_t height) {
        startHeight = height;
    }

    void setUserAgent(const std::string& agent) {
        userAgent = agent;
    }

    void setServices(uint64_t svc) {
        services = svc;
    }

    std::vector<uint8_t> createVersionMessage(const std::string& remoteIP, uint16_t remotePort) {
        VersionMessage version;
        version.version = protocolVersion;
        version.services = services;
        version.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        version.addrRecv.services = 1;
        parseIP(remoteIP, version.addrRecv.ip);
        version.addrRecv.port = remotePort;
        version.addrRecv.time = 0;

        version.addrFrom.services = services;
        std::memset(version.addrFrom.ip, 0, 16);
        version.addrFrom.port = 0;
        version.addrFrom.time = 0;

        version.nonce = localNonce;
        version.userAgent = userAgent;
        version.startHeight = startHeight;
        version.relay = true;

        return builder.buildVersionMessage(version);
    }

    std::vector<uint8_t> createVerackMessage() {
        return builder.buildVerackMessage();
    }

    std::vector<uint8_t> createPingMessage() {
        uint64_t nonce = rng();
        return builder.buildPingMessage(nonce);
    }

    std::vector<uint8_t> createPongMessage(uint64_t nonce) {
        return builder.buildPongMessage(nonce);
    }

    std::vector<uint8_t> createGetAddrMessage() {
        return builder.buildGetAddrMessage();
    }

    std::vector<uint8_t> createAddrMessage(const std::vector<NetAddress>& addrs) {
        return builder.buildAddrMessage(addrs);
    }

    std::vector<uint8_t> createInvMessage(const std::vector<InvItem>& items) {
        return builder.buildInvMessage(items);
    }

    std::vector<uint8_t> createGetDataMessage(const std::vector<InvItem>& items) {
        return builder.buildGetDataMessage(items);
    }

    void feedData(const uint8_t* data, size_t len) {
        parser.feed(data, len);
    }

    void feedData(const std::vector<uint8_t>& data) {
        parser.feed(data);
    }

    bool hasMessage() const {
        return parser.hasCompleteMessage();
    }

    MessageParser::ParsedMessage getNextMessage() {
        return parser.getNextMessage();
    }

    bool processVersionMessage(const std::vector<uint8_t>& payload, VersionMessage& outVersion) {
        try {
            outVersion = MessageSerializer::deserializeVersion(payload);

            if (outVersion.version < MIN_PROTOCOL_VERSION) {
                return false;
            }

            if (outVersion.nonce == localNonce) {
                return false;
            }

            return true;
        } catch (...) {
            return false;
        }
    }

    void completeHandshake() {
        handshakeComplete = true;
    }

    bool isHandshakeComplete() const {
        return handshakeComplete;
    }

    void enableSendHeaders() {
        sendHeaders = true;
    }

    bool shouldSendHeaders() const {
        return sendHeaders;
    }

private:
    void parseIP(const std::string& ip, uint8_t* out) {
        std::memset(out, 0, 16);

        if (ip.find(':') != std::string::npos) {
            // IPv6 address
            if (inet_pton(AF_INET6, ip.c_str(), out) != 1) {
                std::memset(out, 0, 16);
            }
        } else {
            out[10] = 0xFF;
            out[11] = 0xFF;

            std::istringstream iss(ip);
            std::string octet;
            int i = 12;
            while (std::getline(iss, octet, '.') && i < 16) {
                out[i++] = std::stoi(octet);
            }
        }
    }
};

class HandshakeManager {
private:
    enum class HandshakeState {
        Initial,
        VersionSent,
        VersionReceived,
        VerackSent,
        VerackReceived,
        Complete,
        Failed
    };

    HandshakeState state;
    VersionMessage remoteVersion;
    std::chrono::steady_clock::time_point startTime;
    int timeoutMs;

public:
    HandshakeManager() : state(HandshakeState::Initial), timeoutMs(10000) {}

    void start() {
        state = HandshakeState::Initial;
        startTime = std::chrono::steady_clock::now();
    }

    void versionSent() {
        if (state == HandshakeState::Initial) {
            state = HandshakeState::VersionSent;
        }
    }

    bool versionReceived(const VersionMessage& version) {
        remoteVersion = version;

        if (state == HandshakeState::Initial) {
            state = HandshakeState::VersionReceived;
        } else if (state == HandshakeState::VersionSent) {
            state = HandshakeState::VersionReceived;
        }

        return true;
    }

    void verackSent() {
        if (state == HandshakeState::VersionReceived) {
            state = HandshakeState::VerackSent;
        }
    }

    void verackReceived() {
        if (state == HandshakeState::VerackSent || state == HandshakeState::VersionReceived) {
            state = HandshakeState::Complete;
        }
    }

    bool isComplete() const {
        return state == HandshakeState::Complete;
    }

    bool isFailed() const {
        return state == HandshakeState::Failed;
    }

    bool isTimedOut() const {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        return elapsed > timeoutMs;
    }

    void fail() {
        state = HandshakeState::Failed;
    }

    const VersionMessage& getRemoteVersion() const {
        return remoteVersion;
    }

    void setTimeout(int ms) {
        timeoutMs = ms;
    }
};

}
}
