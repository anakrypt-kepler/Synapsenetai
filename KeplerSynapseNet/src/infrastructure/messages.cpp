#include "messages.h"
#include <cstring>
#include <ctime>

namespace synapse {

static void writeU32(std::vector<uint8_t>& out, uint32_t val) {
    for (int i = 0; i < 4; i++) out.push_back((val >> (i * 8)) & 0xff);
}

static void writeU64(std::vector<uint8_t>& out, uint64_t val) {
    for (int i = 0; i < 8; i++) out.push_back((val >> (i * 8)) & 0xff);
}

static void writeU16(std::vector<uint8_t>& out, uint16_t val) {
    out.push_back(val & 0xff);
    out.push_back((val >> 8) & 0xff);
}

static void writeVarInt(std::vector<uint8_t>& out, uint64_t val) {
    if (val < 0xfd) {
        out.push_back(static_cast<uint8_t>(val));
    } else if (val <= 0xffff) {
        out.push_back(0xfd);
        writeU16(out, static_cast<uint16_t>(val));
    } else if (val <= 0xffffffff) {
        out.push_back(0xfe);
        writeU32(out, static_cast<uint32_t>(val));
    } else {
        out.push_back(0xff);
        writeU64(out, val);
    }
}

static void writeString(std::vector<uint8_t>& out, const std::string& s) {
    writeVarInt(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

static uint32_t readU32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static uint64_t readU64(const uint8_t* p) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) val |= static_cast<uint64_t>(p[i]) << (i * 8);
    return val;
}

static uint16_t readU16(const uint8_t* p) {
    return p[0] | (p[1] << 8);
}

static uint64_t readVarInt(const uint8_t* p, size_t& offset) {
    uint8_t first = p[offset++];
    if (first < 0xfd) return first;
    if (first == 0xfd) { uint16_t v = readU16(p + offset); offset += 2; return v; }
    if (first == 0xfe) { uint32_t v = readU32(p + offset); offset += 4; return v; }
    uint64_t v = readU64(p + offset); offset += 8; return v;
}

static uint64_t safeReadVarInt(const uint8_t* p, size_t total, size_t& offset, bool& ok) {
    if (offset >= total) { ok = false; return 0; }
    uint8_t first = p[offset++];
    if (first < 0xfd) return first;
    if (first == 0xfd) {
        if (offset + 2 > total) { ok = false; return 0; }
        uint16_t v = readU16(p + offset); offset += 2; return v;
    }
    if (first == 0xfe) {
        if (offset + 4 > total) { ok = false; return 0; }
        uint32_t v = readU32(p + offset); offset += 4; return v;
    }
    if (offset + 8 > total) { ok = false; return 0; }
    uint64_t v = readU64(p + offset); offset += 8; return v;
}

static std::string readString(const uint8_t* p, size_t& offset) {
    uint64_t len = readVarInt(p, offset);
    std::string s(reinterpret_cast<const char*>(p + offset), len);
    offset += len;
    return s;
}

static std::string safeReadString(const uint8_t* p, size_t total, size_t& offset, bool& ok) {
    uint64_t len = safeReadVarInt(p, total, offset, ok);
    if (!ok) return {};
    if (len > 4 * 1024 * 1024 || offset + len > total) { ok = false; return {}; }
    std::string s(reinterpret_cast<const char*>(p + offset), static_cast<size_t>(len));
    offset += static_cast<size_t>(len);
    return s;
}

static uint32_t floatToBits(float f) {
    uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");
    std::memcpy(&bits, &f, sizeof(uint32_t));
    return bits;
}

static float bitsToFloat(uint32_t bits) {
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(uint32_t));
    return f;
}

std::vector<uint8_t> MessageHeader::serialize() const {
    std::vector<uint8_t> out;
    writeU32(out, magic);
    out.insert(out.end(), command, command + 12);
    writeU32(out, length);
    writeU32(out, checksum);
    return out;
}

MessageHeader MessageHeader::deserialize(const uint8_t* data) {
    MessageHeader h;
    h.magic = readU32(data);
    std::memcpy(h.command, data + 4, 12);
    h.length = readU32(data + 16);
    h.checksum = readU32(data + 20);
    return h;
}

std::vector<uint8_t> VersionMessage::serialize() const {
    std::vector<uint8_t> out;
    writeU32(out, version);
    writeU64(out, services);
    writeU64(out, timestamp);
    out.insert(out.end(), addrRecv.begin(), addrRecv.end());
    writeU16(out, portRecv);
    out.insert(out.end(), addrFrom.begin(), addrFrom.end());
    writeU16(out, portFrom);
    writeU64(out, nonce);
    writeString(out, userAgent);
    writeU64(out, startHeight);
    out.push_back(relay ? 1 : 0);
    return out;
}

VersionMessage VersionMessage::deserialize(const std::vector<uint8_t>& data) {
    VersionMessage m;
    const size_t sz = data.size();
    if (sz < 66) return m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    m.version = readU32(p + offset); offset += 4;
    m.services = readU64(p + offset); offset += 8;
    m.timestamp = readU64(p + offset); offset += 8;
    std::memcpy(m.addrRecv.data(), p + offset, 16); offset += 16;
    m.portRecv = readU16(p + offset); offset += 2;
    if (offset + 16 + 2 + 8 > sz) return m;
    std::memcpy(m.addrFrom.data(), p + offset, 16); offset += 16;
    m.portFrom = readU16(p + offset); offset += 2;
    m.nonce = readU64(p + offset); offset += 8;
    bool ok = true;
    m.userAgent = safeReadString(p, sz, offset, ok);
    if (!ok || offset + 9 > sz) return m;
    m.startHeight = readU64(p + offset); offset += 8;
    m.relay = p[offset] != 0;
    return m;
}

std::vector<uint8_t> PeerAddress::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, timestamp);
    writeU64(out, services);
    out.insert(out.end(), addr.begin(), addr.end());
    writeU16(out, port);
    return out;
}

PeerAddress PeerAddress::deserialize(const uint8_t* data) {
    PeerAddress a;
    a.timestamp = readU64(data);
    a.services = readU64(data + 8);
    std::memcpy(a.addr.data(), data + 16, 16);
    a.port = readU16(data + 32);
    return a;
}

std::vector<uint8_t> PeersMessage::serialize() const {
    std::vector<uint8_t> out;
    writeVarInt(out, peers.size());
    for (const auto& p : peers) {
        auto s = p.serialize();
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

PeersMessage PeersMessage::deserialize(const std::vector<uint8_t>& data) {
    PeersMessage m;
    if (data.empty()) return m;
    size_t offset = 0;
    bool ok = true;
    uint64_t count = safeReadVarInt(data.data(), data.size(), offset, ok);
    if (!ok || count > 1000) return m;
    for (uint64_t i = 0; i < count; i++) {
        if (offset + 34 > data.size()) break;
        m.peers.push_back(PeerAddress::deserialize(data.data() + offset));
        offset += 34;
    }
    return m;
}

std::vector<uint8_t> OnionPeerAddress::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, onionHost);
    writeU16(out, port);
    writeU64(out, services);
    writeU64(out, timestamp);
    return out;
}

OnionPeerAddress OnionPeerAddress::deserialize(const uint8_t* data, size_t len, size_t& consumed) {
    OnionPeerAddress a;
    size_t offset = 0;
    if (offset >= len) { consumed = 0; return a; }
    uint64_t hostLen = readVarInt(data, offset);
    if (offset + hostLen > len || hostLen > 256) { consumed = 0; return a; }
    a.onionHost.assign(reinterpret_cast<const char*>(data + offset), static_cast<size_t>(hostLen));
    offset += static_cast<size_t>(hostLen);
    if (offset + 18 > len) { consumed = 0; return a; }
    a.port = readU16(data + offset); offset += 2;
    a.services = readU64(data + offset); offset += 8;
    a.timestamp = readU64(data + offset); offset += 8;
    consumed = offset;
    return a;
}

std::vector<uint8_t> OnionPeersMessage::serialize() const {
    std::vector<uint8_t> out;
    writeVarInt(out, peers.size());
    for (const auto& p : peers) {
        auto s = p.serialize();
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

OnionPeersMessage OnionPeersMessage::deserialize(const std::vector<uint8_t>& data) {
    OnionPeersMessage m;
    if (data.empty()) return m;
    size_t offset = 0;
    uint64_t count = readVarInt(data.data(), offset);
    if (count > 1000) return m;
    for (uint64_t i = 0; i < count; i++) {
        if (offset >= data.size()) break;
        size_t consumed = 0;
        auto peer = OnionPeerAddress::deserialize(data.data() + offset, data.size() - offset, consumed);
        if (consumed == 0) break;
        offset += consumed;
        m.peers.push_back(std::move(peer));
    }
    return m;
}

std::vector<uint8_t> InvItem::serialize() const {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(type));
    out.insert(out.end(), hash.begin(), hash.end());
    return out;
}

InvItem InvItem::deserialize(const uint8_t* data) {
    InvItem i;
    i.type = static_cast<InvType>(data[0]);
    std::memcpy(i.hash.data(), data + 1, 32);
    return i;
}

std::vector<uint8_t> InvMessage::serialize() const {
    std::vector<uint8_t> out;
    writeVarInt(out, items.size());
    for (const auto& item : items) {
        auto s = item.serialize();
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

InvMessage InvMessage::deserialize(const std::vector<uint8_t>& data) {
    InvMessage m;
    size_t offset = 0;
    uint64_t count = readVarInt(data.data(), offset);
    for (uint64_t i = 0; i < count; i++) {
        m.items.push_back(InvItem::deserialize(data.data() + offset));
        offset += 33;
    }
    return m;
}

std::vector<uint8_t> GetDataMessage::serialize() const {
    std::vector<uint8_t> out;
    writeVarInt(out, items.size());
    for (const auto& item : items) {
        auto s = item.serialize();
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

GetDataMessage GetDataMessage::deserialize(const std::vector<uint8_t>& data) {
    GetDataMessage m;
    size_t offset = 0;
    uint64_t count = readVarInt(data.data(), offset);
    for (uint64_t i = 0; i < count; i++) {
        m.items.push_back(InvItem::deserialize(data.data() + offset));
        offset += 33;
    }
    return m;
}

std::vector<uint8_t> KnowledgeMessage::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, id);
    writeString(out, question);
    writeString(out, answer);
    writeString(out, source);
    writeVarInt(out, tags.size());
    for (const auto& tag : tags) {
        writeString(out, tag);
    }
    out.insert(out.end(), author.begin(), author.end());
    writeU64(out, timestamp);
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

KnowledgeMessage KnowledgeMessage::deserialize(const std::vector<uint8_t>& data) {
    KnowledgeMessage m;
    const size_t sz = data.size();
    if (sz < 8) return m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    bool ok = true;
    m.id = readU64(p + offset); offset += 8;
    m.question = safeReadString(p, sz, offset, ok);
    if (!ok) return m;
    m.answer = safeReadString(p, sz, offset, ok);
    if (!ok) return m;
    m.source = safeReadString(p, sz, offset, ok);
    if (!ok) return m;
    uint64_t tagCount = safeReadVarInt(p, sz, offset, ok);
    if (!ok || tagCount > 10000) return m;
    for (uint64_t i = 0; i < tagCount; i++) {
        m.tags.push_back(safeReadString(p, sz, offset, ok));
        if (!ok) return m;
    }
    if (offset + 33 + 8 + 64 > sz) return m;
    std::memcpy(m.author.data(), p + offset, 33); offset += 33;
    m.timestamp = readU64(p + offset); offset += 8;
    std::memcpy(m.signature.data(), p + offset, 64);
    return m;
}

std::array<uint8_t, 32> KnowledgeMessage::hash() const {
    auto s = serialize();
    std::array<uint8_t, 32> h{};
    for (size_t i = 0; i < s.size() && i < 32; i++) h[i] = s[i];
    return h;
}

std::vector<uint8_t> TxInput::serialize() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), prevTxHash.begin(), prevTxHash.end());
    writeU32(out, outputIndex);
    writeVarInt(out, scriptSig.size());
    out.insert(out.end(), scriptSig.begin(), scriptSig.end());
    writeU32(out, sequence);
    return out;
}

TxInput TxInput::deserialize(const uint8_t* data, size_t& offset) {
    TxInput i;
    std::memcpy(i.prevTxHash.data(), data + offset, 32); offset += 32;
    i.outputIndex = readU32(data + offset); offset += 4;
    uint64_t scriptLen = readVarInt(data, offset);
    i.scriptSig.assign(data + offset, data + offset + scriptLen); offset += scriptLen;
    i.sequence = readU32(data + offset); offset += 4;
    return i;
}

std::vector<uint8_t> TxOutput::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, amount);
    writeVarInt(out, scriptPubKey.size());
    out.insert(out.end(), scriptPubKey.begin(), scriptPubKey.end());
    return out;
}

TxOutput TxOutput::deserialize(const uint8_t* data, size_t& offset) {
    TxOutput o;
    o.amount = readU64(data + offset); offset += 8;
    uint64_t scriptLen = readVarInt(data, offset);
    o.scriptPubKey.assign(data + offset, data + offset + scriptLen); offset += scriptLen;
    return o;
}

std::vector<uint8_t> TransferMessage::serialize() const {
    std::vector<uint8_t> out;
    writeU32(out, version);
    writeVarInt(out, inputs.size());
    for (const auto& input : inputs) {
        auto s = input.serialize();
        out.insert(out.end(), s.begin(), s.end());
    }
    writeVarInt(out, outputs.size());
    for (const auto& output : outputs) {
        auto s = output.serialize();
        out.insert(out.end(), s.begin(), s.end());
    }
    writeU64(out, lockTime);
    return out;
}

TransferMessage TransferMessage::deserialize(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    return deserialize(data.data(), data.size(), offset);
}

TransferMessage TransferMessage::deserialize(const uint8_t* data, size_t total, size_t& offset) {
    TransferMessage m;
    if (offset + 4 > total) return m;
    m.version = readU32(data + offset); offset += 4;
    bool ok = true;
    uint64_t inputCount = safeReadVarInt(data, total, offset, ok);
    if (!ok || inputCount > 10000) return m;
    for (uint64_t i = 0; i < inputCount; i++) {
        if (offset + 40 > total) return m;
        m.inputs.push_back(TxInput::deserialize(data, offset));
    }
    uint64_t outputCount = safeReadVarInt(data, total, offset, ok);
    if (!ok || outputCount > 10000) return m;
    for (uint64_t i = 0; i < outputCount; i++) {
        if (offset + 9 > total) return m;
        m.outputs.push_back(TxOutput::deserialize(data, offset));
    }
    if (offset + 8 > total) return m;
    m.lockTime = readU64(data + offset); offset += 8;
    return m;
}

std::array<uint8_t, 32> TransferMessage::hash() const {
    auto s = serialize();
    std::array<uint8_t, 32> h{};
    for (size_t i = 0; i < s.size() && i < 32; i++) h[i] = s[i];
    return h;
}

std::array<uint8_t, 32> TransferMessage::txid() const {
    return hash();
}

std::vector<uint8_t> BlockHeader::serialize() const {
    std::vector<uint8_t> out;
    writeU32(out, version);
    out.insert(out.end(), prevHash.begin(), prevHash.end());
    out.insert(out.end(), merkleRoot.begin(), merkleRoot.end());
    writeU64(out, timestamp);
    writeU32(out, bits);
    writeU32(out, nonce);
    return out;
}

BlockHeader BlockHeader::deserialize(const uint8_t* data) {
    BlockHeader h;
    size_t offset = 0;
    h.version = readU32(data + offset); offset += 4;
    std::memcpy(h.prevHash.data(), data + offset, 32); offset += 32;
    std::memcpy(h.merkleRoot.data(), data + offset, 32); offset += 32;
    h.timestamp = readU64(data + offset); offset += 8;
    h.bits = readU32(data + offset); offset += 4;
    h.nonce = readU32(data + offset);
    return h;
}

std::array<uint8_t, 32> BlockHeader::hash() const {
    auto s = serialize();
    std::array<uint8_t, 32> h{};
    for (size_t i = 0; i < s.size() && i < 32; i++) h[i] = s[i];
    return h;
}

std::vector<uint8_t> BlockMessage::serialize() const {
    std::vector<uint8_t> out;
    auto hdr = header.serialize();
    out.insert(out.end(), hdr.begin(), hdr.end());
    writeVarInt(out, transactions.size());
    for (const auto& tx : transactions) {
        auto s = tx.serialize();
        out.insert(out.end(), s.begin(), s.end());
    }
    writeVarInt(out, knowledge.size());
    for (const auto& k : knowledge) {
        auto s = k.serialize();
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

BlockMessage BlockMessage::deserialize(const std::vector<uint8_t>& data) {
    BlockMessage m;
    if (data.size() < 81) return m;
    m.header = BlockHeader::deserialize(data.data());
    size_t offset = 80;
    bool ok = true;
    uint64_t txCount = safeReadVarInt(data.data(), data.size(), offset, ok);
    if (!ok || txCount > 50000) return m;
    for (uint64_t i = 0; i < txCount; i++) {
        if (offset >= data.size()) break;
        m.transactions.push_back(TransferMessage::deserialize(data.data(), data.size(), offset));
    }
    return m;
}

std::vector<uint8_t> GetBlocksMessage::serialize() const {
    std::vector<uint8_t> out;
    writeU32(out, version);
    writeVarInt(out, locatorHashes.size());
    for (const auto& h : locatorHashes) {
        out.insert(out.end(), h.begin(), h.end());
    }
    out.insert(out.end(), stopHash.begin(), stopHash.end());
    return out;
}

GetBlocksMessage GetBlocksMessage::deserialize(const std::vector<uint8_t>& data) {
    GetBlocksMessage m;
    const size_t sz = data.size();
    if (sz < 4) return m;
    size_t offset = 0;
    bool ok = true;
    m.version = readU32(data.data() + offset); offset += 4;
    uint64_t count = safeReadVarInt(data.data(), sz, offset, ok);
    if (!ok || count > 10000) return m;
    for (uint64_t i = 0; i < count; i++) {
        if (offset + 32 > sz) return m;
        std::array<uint8_t, 32> h;
        std::memcpy(h.data(), data.data() + offset, 32); offset += 32;
        m.locatorHashes.push_back(h);
    }
    if (offset + 32 > sz) return m;
    std::memcpy(m.stopHash.data(), data.data() + offset, 32);
    return m;
}

std::vector<uint8_t> PingMessage::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, nonce);
    return out;
}

PingMessage PingMessage::deserialize(const std::vector<uint8_t>& data) {
    PingMessage m;
    if (data.size() < 8) return m;
    m.nonce = readU64(data.data());
    return m;
}

std::vector<uint8_t> PongMessage::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, nonce);
    return out;
}

PongMessage PongMessage::deserialize(const std::vector<uint8_t>& data) {
    PongMessage m;
    if (data.size() < 8) return m;
    m.nonce = readU64(data.data());
    return m;
}

std::vector<uint8_t> RejectMessage::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, message);
    out.push_back(static_cast<uint8_t>(code));
    writeString(out, reason);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

RejectMessage RejectMessage::deserialize(const std::vector<uint8_t>& d) {
    RejectMessage m;
    if (d.size() < 3) return m;
    size_t offset = 0;
    bool ok = true;
    m.message = safeReadString(d.data(), d.size(), offset, ok);
    if (!ok || offset >= d.size()) return m;
    m.code = static_cast<RejectCode>(d[offset++]);
    m.reason = safeReadString(d.data(), d.size(), offset, ok);
    if (!ok || offset + 32 > d.size()) return m;
    std::memcpy(m.data.data(), d.data() + offset, 32);
    return m;
}

std::vector<uint8_t> VoteMessage::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, eventId);
    out.insert(out.end(), validator.begin(), validator.end());
    out.push_back(voteType);
    uint64_t scoreBits;
    std::memcpy(&scoreBits, &score, sizeof(double));
    writeU64(out, scoreBits);
    writeU64(out, timestamp);
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

VoteMessage VoteMessage::deserialize(const std::vector<uint8_t>& data) {
    VoteMessage m;
    if (data.size() < 8 + 33 + 1 + 8 + 8 + 64) return m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    m.eventId = readU64(p + offset); offset += 8;
    std::memcpy(m.validator.data(), p + offset, 33); offset += 33;
    m.voteType = p[offset++];
    uint64_t scoreBits = readU64(p + offset); offset += 8;
    std::memcpy(&m.score, &scoreBits, sizeof(double));
    m.timestamp = readU64(p + offset); offset += 8;
    std::memcpy(m.signature.data(), p + offset, 64);
    return m;
}

std::vector<uint8_t> ModelRequestMessage::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, modelId);
    writeString(out, prompt);
    writeU32(out, maxTokens);
    uint32_t tempBits;
    std::memcpy(&tempBits, &temperature, sizeof(float));
    writeU32(out, tempBits);
    out.insert(out.end(), requester.begin(), requester.end());
    writeU64(out, timestamp);
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

ModelRequestMessage ModelRequestMessage::deserialize(const std::vector<uint8_t>& data) {
    ModelRequestMessage m;
    const size_t sz = data.size();
    if (sz < 4) return m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    bool ok = true;
    m.modelId = safeReadString(p, sz, offset, ok);
    if (!ok) return m;
    m.prompt = safeReadString(p, sz, offset, ok);
    if (!ok || offset + 4 + 4 + 33 + 8 + 64 > sz) return m;
    m.maxTokens = readU32(p + offset); offset += 4;
    uint32_t tempBits = readU32(p + offset); offset += 4;
    std::memcpy(&m.temperature, &tempBits, sizeof(float));
    std::memcpy(m.requester.data(), p + offset, 33); offset += 33;
    m.timestamp = readU64(p + offset); offset += 8;
    std::memcpy(m.signature.data(), p + offset, 64);
    return m;
}

std::vector<uint8_t> ModelResponseMessage::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, requestId);
    writeString(out, response);
    writeU32(out, tokensUsed);
    writeU64(out, latencyMs);
    out.insert(out.end(), provider.begin(), provider.end());
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

ModelResponseMessage ModelResponseMessage::deserialize(const std::vector<uint8_t>& data) {
    ModelResponseMessage m;
    const size_t sz = data.size();
    if (sz < 4) return m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    bool ok = true;
    m.requestId = safeReadString(p, sz, offset, ok);
    if (!ok) return m;
    m.response = safeReadString(p, sz, offset, ok);
    if (!ok || offset + 4 + 8 + 33 + 64 > sz) return m;
    m.tokensUsed = readU32(p + offset); offset += 4;
    m.latencyMs = readU64(p + offset); offset += 8;
    std::memcpy(m.provider.data(), p + offset, 33); offset += 33;
    std::memcpy(m.signature.data(), p + offset, 64);
    return m;
}

// -----------------------------------------------------------------------------
// Remote model routing (opt-in) — P2P wire objects
// -----------------------------------------------------------------------------

std::vector<uint8_t> RemoteModelOfferMessage::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, offerId);
    writeString(out, modelId);
    writeString(out, providerAddress);
    writeU64(out, pricePerRequestAtoms);
    writeU32(out, maxSlots);
    writeU32(out, usedSlots);
    writeU64(out, expiresAt);
    return out;
}

RemoteModelOfferMessage RemoteModelOfferMessage::deserialize(const std::vector<uint8_t>& data) {
    RemoteModelOfferMessage m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    m.offerId = readString(p, offset);
    m.modelId = readString(p, offset);
    m.providerAddress = readString(p, offset);
    m.pricePerRequestAtoms = readU64(p + offset); offset += 8;
    m.maxSlots = readU32(p + offset); offset += 4;
    m.usedSlots = readU32(p + offset); offset += 4;
    m.expiresAt = readU64(p + offset); offset += 8;
    return m;
}

std::vector<uint8_t> RemoteModelRentMessage::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, offerId);
    out.insert(out.end(), renterPubKey.begin(), renterPubKey.end());
    writeU64(out, timestamp);
    return out;
}

RemoteModelRentMessage RemoteModelRentMessage::deserialize(const std::vector<uint8_t>& data) {
    RemoteModelRentMessage m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    m.offerId = readString(p, offset);
    std::memcpy(m.renterPubKey.data(), p + offset, 33); offset += 33;
    m.timestamp = readU64(p + offset); offset += 8;
    return m;
}

std::vector<uint8_t> RemoteModelRentOkMessage::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, offerId);
    writeString(out, sessionId);
    writeString(out, providerAddress);
    writeU64(out, pricePerRequestAtoms);
    writeU64(out, expiresAt);
    return out;
}

RemoteModelRentOkMessage RemoteModelRentOkMessage::deserialize(const std::vector<uint8_t>& data) {
    RemoteModelRentOkMessage m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    m.offerId = readString(p, offset);
    m.sessionId = readString(p, offset);
    m.providerAddress = readString(p, offset);
    m.pricePerRequestAtoms = readU64(p + offset); offset += 8;
    m.expiresAt = readU64(p + offset); offset += 8;
    return m;
}

std::vector<uint8_t> RemoteModelInferMessage::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, sessionId);
    writeString(out, requestId);
    writeString(out, prompt);
    writeU32(out, maxTokens);
    writeU32(out, floatToBits(temperature));
    writeU32(out, floatToBits(topP));
    writeU32(out, topK);
    writeString(out, paymentTxidHex);
    out.insert(out.end(), renterPubKey.begin(), renterPubKey.end());
    writeU64(out, timestamp);
    return out;
}

RemoteModelInferMessage RemoteModelInferMessage::deserialize(const std::vector<uint8_t>& data) {
    RemoteModelInferMessage m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    m.sessionId = readString(p, offset);
    m.requestId = readString(p, offset);
    m.prompt = readString(p, offset);
    m.maxTokens = readU32(p + offset); offset += 4;
    m.temperature = bitsToFloat(readU32(p + offset)); offset += 4;
    m.topP = bitsToFloat(readU32(p + offset)); offset += 4;
    m.topK = readU32(p + offset); offset += 4;
    m.paymentTxidHex = readString(p, offset);
    std::memcpy(m.renterPubKey.data(), p + offset, 33); offset += 33;
    m.timestamp = readU64(p + offset); offset += 8;
    return m;
}

std::vector<uint8_t> RemoteModelOutMessage::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, requestId);
    writeString(out, text);
    writeU32(out, tokensUsed);
    writeU64(out, latencyMs);
    return out;
}

RemoteModelOutMessage RemoteModelOutMessage::deserialize(const std::vector<uint8_t>& data) {
    RemoteModelOutMessage m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    m.requestId = readString(p, offset);
    m.text = readString(p, offset);
    m.tokensUsed = readU32(p + offset); offset += 4;
    m.latencyMs = readU64(p + offset); offset += 8;
    return m;
}

std::vector<uint8_t> AlertMessage::serialize() const {
    std::vector<uint8_t> out;
    writeU32(out, version);
    writeU64(out, relayUntil);
    writeU64(out, expiration);
    writeU32(out, id);
    writeU32(out, cancel);
    writeVarInt(out, setCancel.size());
    for (uint32_t c : setCancel) {
        writeU32(out, c);
    }
    writeU32(out, minVer);
    writeU32(out, maxVer);
    writeString(out, comment);
    writeString(out, statusBar);
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

AlertMessage AlertMessage::deserialize(const std::vector<uint8_t>& data) {
    AlertMessage m;
    size_t offset = 0;
    const uint8_t* p = data.data();
    m.version = readU32(p + offset); offset += 4;
    m.relayUntil = readU64(p + offset); offset += 8;
    m.expiration = readU64(p + offset); offset += 8;
    m.id = readU32(p + offset); offset += 4;
    m.cancel = readU32(p + offset); offset += 4;
    uint64_t cancelCount = readVarInt(p, offset);
    for (uint64_t i = 0; i < cancelCount; i++) {
        m.setCancel.push_back(readU32(p + offset)); offset += 4;
    }
    m.minVer = readU32(p + offset); offset += 4;
    m.maxVer = readU32(p + offset); offset += 4;
    m.comment = readString(p, offset);
    m.statusBar = readString(p, offset);
    std::memcpy(m.signature.data(), p + offset, 64);
    return m;
}

}
