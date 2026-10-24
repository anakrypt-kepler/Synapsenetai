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

static constexpr uint64_t kMaxItemCount = 1000;
static constexpr uint64_t kMaxStringBytes = 1024ull * 1024ull;
static constexpr uint64_t kMaxScriptBytes = 1024ull * 1024ull;
static constexpr size_t kInvItemBytes = 33;
static constexpr uint64_t kMaxOnionHostBytes = 256;

// Cursor-only deserializers publish the surrounding frame for this thread.
// A script copy is refused when that frame is absent or the declared length
// does not fit in the unread bytes.
struct ActiveFrame {
    const uint8_t* data;
    size_t total;
};

static thread_local ActiveFrame g_activeFrame{nullptr, 0};

class ActiveFrameScope {
public:
    explicit ActiveFrameScope(const uint8_t* data, size_t total)
        : previous_(g_activeFrame) {
        g_activeFrame = ActiveFrame{data, total};
    }
    ~ActiveFrameScope() { g_activeFrame = previous_; }
    ActiveFrameScope(const ActiveFrameScope&) = delete;
    ActiveFrameScope& operator=(const ActiveFrameScope&) = delete;
private:
    ActiveFrame previous_;
};

static bool fits(size_t offset, size_t total, size_t count) {
    return offset <= total && count <= total - offset;
}

static bool frameLimit(const uint8_t* data, size_t& total) {
    if (data == nullptr || g_activeFrame.data == nullptr || g_activeFrame.data != data) return false;
    total = g_activeFrame.total;
    return true;
}

static uint64_t safeReadVarInt(const uint8_t* p, size_t total, size_t& offset, bool& ok) {
    if (!ok) return 0;
    if (p == nullptr || offset >= total) { ok = false; return 0; }
    const uint8_t first = p[offset++];
    if (first < 0xfd) return first;
    const size_t extra = (first == 0xfd) ? 2u : (first == 0xfe) ? 4u : 8u;
    if (!fits(offset, total, extra)) { ok = false; return 0; }
    uint64_t value = 0;
    if (extra == 2) value = readU16(p + offset);
    else if (extra == 4) value = readU32(p + offset);
    else value = readU64(p + offset);
    offset += extra;
    return value;
}

static std::string readString(const uint8_t* p, size_t& offset) {
    uint64_t len = readVarInt(p, offset);
    std::string s(reinterpret_cast<const char*>(p + offset), len);
    offset += len;
    return s;
}

static std::string safeReadString(const uint8_t* p, size_t total, size_t& offset, bool& ok,
                                  uint64_t maxLen = 4ull * 1024ull * 1024ull) {
    const uint64_t len = safeReadVarInt(p, total, offset, ok);
    if (!ok) return {};
    if (len > maxLen || offset > total || len > static_cast<uint64_t>(total - offset)) {
        ok = false;
        return {};
    }
    std::string s(reinterpret_cast<const char*>(p + offset), static_cast<size_t>(len));
    offset += static_cast<size_t>(len);
    return s;
}

static bool readCheckedString(const uint8_t* p, size_t total, size_t& offset, std::string& out) {
    bool ok = true;
    out = safeReadString(p, total, offset, ok, kMaxStringBytes);
    return ok;
}

static bool readU32Checked(const uint8_t* p, size_t total, size_t& offset, uint32_t& value) {
    if (p == nullptr || !fits(offset, total, 4)) return false;
    value = readU32(p + offset);
    offset += 4;
    return true;
}

static bool readU64Checked(const uint8_t* p, size_t total, size_t& offset, uint64_t& value) {
    if (p == nullptr || !fits(offset, total, 8)) return false;
    value = readU64(p + offset);
    offset += 8;
    return true;
}

static bool readCopy(const uint8_t* p, size_t total, size_t& offset, void* dest, size_t count) {
    if (p == nullptr || dest == nullptr || !fits(offset, total, count)) return false;
    std::memcpy(dest, p + offset, count);
    offset += count;
    return true;
}

static bool readTxInput(const uint8_t* data, size_t total, size_t& offset, TxInput& in) {
    if (data == nullptr || !fits(offset, total, 36)) return false;
    std::memcpy(in.prevTxHash.data(), data + offset, 32);
    offset += 32;
    in.outputIndex = readU32(data + offset);
    offset += 4;
    bool ok = true;
    const uint64_t scriptLen = safeReadVarInt(data, total, offset, ok);
    if (!ok || scriptLen > kMaxScriptBytes) return false;
    const size_t scriptBytes = static_cast<size_t>(scriptLen);
    if (!fits(offset, total, scriptBytes)) return false;
    const size_t afterScript = offset + scriptBytes;
    if (!fits(afterScript, total, 4)) return false;
    in.scriptSig.assign(data + offset, data + offset + scriptBytes);
    offset += scriptBytes;
    in.sequence = readU32(data + offset);
    offset += 4;
    return true;
}

static bool readTxOutput(const uint8_t* data, size_t total, size_t& offset, TxOutput& out) {
    if (data == nullptr || !fits(offset, total, 8)) return false;
    out.amount = readU64(data + offset);
    offset += 8;
    bool ok = true;
    const uint64_t scriptLen = safeReadVarInt(data, total, offset, ok);
    if (!ok || scriptLen > kMaxScriptBytes) return false;
    const size_t scriptBytes = static_cast<size_t>(scriptLen);
    if (!fits(offset, total, scriptBytes)) return false;
    out.scriptPubKey.assign(data + offset, data + offset + scriptBytes);
    offset += scriptBytes;
    return true;
}

template <typename Message>
static Message readItemList(const std::vector<uint8_t>& data) {
    Message message;
    const size_t total = data.size();
    const uint8_t* bytes = data.data();
    size_t offset = 0;
    bool ok = true;
    const uint64_t count = safeReadVarInt(bytes, total, offset, ok);
    if (!ok || count > kMaxItemCount) return {};
    message.items.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        if (!fits(offset, total, kInvItemBytes)) return {};
        message.items.push_back(InvItem::deserialize(bytes + offset));
        offset += kInvItemBytes;
    }
    return message;
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
    consumed = 0;
    size_t offset = 0;
    bool ok = true;
    const uint64_t hostLen = safeReadVarInt(data, len, offset, ok);
    if (!ok || hostLen > kMaxOnionHostBytes || hostLen > kMaxStringBytes) return a;
    if (offset > len || hostLen > static_cast<uint64_t>(len - offset)) return a;
    a.onionHost.assign(reinterpret_cast<const char*>(data + offset), static_cast<size_t>(hostLen));
    offset += static_cast<size_t>(hostLen);
    if (!fits(offset, len, 18)) return a;
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
    bool ok = true;
    const uint64_t count = safeReadVarInt(data.data(), data.size(), offset, ok);
    if (!ok || count > kMaxItemCount) return {};
    m.peers.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        if (offset > data.size()) return {};
        size_t consumed = 0;
        OnionPeerAddress peer = OnionPeerAddress::deserialize(
            data.data() + offset, data.size() - offset, consumed);
        if (consumed == 0 || consumed > data.size() - offset) return {};
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
    return readItemList<InvMessage>(data);
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
    return readItemList<GetDataMessage>(data);
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
    const size_t mark = offset;
    TxInput in;
    size_t total = 0;
    if (!frameLimit(data, total) || !readTxInput(data, total, offset, in)) {
        offset = mark;
        return {};
    }
    return in;
}

std::vector<uint8_t> TxOutput::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, amount);
    writeVarInt(out, scriptPubKey.size());
    out.insert(out.end(), scriptPubKey.begin(), scriptPubKey.end());
    return out;
}

TxOutput TxOutput::deserialize(const uint8_t* data, size_t& offset) {
    const size_t mark = offset;
    TxOutput out;
    size_t total = 0;
    if (!frameLimit(data, total) || !readTxOutput(data, total, offset, out)) {
        offset = mark;
        return {};
    }
    return out;
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
    const size_t start = offset;
    auto fail = [&]() -> TransferMessage {
        offset = start;
        return {};
    };
    if (data == nullptr || !fits(offset, total, 4)) return fail();
    [[maybe_unused]] ActiveFrameScope frameScope(data, total);
    TransferMessage m;
    m.version = readU32(data + offset);
    offset += 4;
    bool ok = true;
    const uint64_t inputCount = safeReadVarInt(data, total, offset, ok);
    if (!ok || inputCount > 10000) return fail();
    m.inputs.reserve(static_cast<size_t>(inputCount));
    for (uint64_t i = 0; i < inputCount; ++i) {
        const size_t mark = offset;
        TxInput in = TxInput::deserialize(data, offset);
        if (offset == mark) return fail();
        m.inputs.push_back(std::move(in));
    }
    const uint64_t outputCount = safeReadVarInt(data, total, offset, ok);
    if (!ok || outputCount > 10000) return fail();
    m.outputs.reserve(static_cast<size_t>(outputCount));
    for (uint64_t i = 0; i < outputCount; ++i) {
        const size_t mark = offset;
        TxOutput out = TxOutput::deserialize(data, offset);
        if (offset == mark) return fail();
        m.outputs.push_back(std::move(out));
    }
    if (!fits(offset, total, 8)) return fail();
    m.lockTime = readU64(data + offset);
    offset += 8;
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
    for (uint64_t i = 0; i < txCount; ++i) {
        if (offset >= data.size()) return {};
        const size_t before = offset;
        TransferMessage tx = TransferMessage::deserialize(data.data(), data.size(), offset);
        if (offset == before) return {};
        m.transactions.push_back(std::move(tx));
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
    const size_t total = data.size();
    const uint8_t* p = data.data();
    size_t offset = 0;
    if (!readCheckedString(p, total, offset, m.offerId)) return {};
    if (!readCheckedString(p, total, offset, m.modelId)) return {};
    if (!readCheckedString(p, total, offset, m.providerAddress)) return {};
    if (!readU64Checked(p, total, offset, m.pricePerRequestAtoms)) return {};
    if (!readU32Checked(p, total, offset, m.maxSlots)) return {};
    if (!readU32Checked(p, total, offset, m.usedSlots)) return {};
    if (!readU64Checked(p, total, offset, m.expiresAt)) return {};
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
    const size_t total = data.size();
    const uint8_t* p = data.data();
    size_t offset = 0;
    if (!readCheckedString(p, total, offset, m.offerId)) return {};
    if (!readCopy(p, total, offset, m.renterPubKey.data(), m.renterPubKey.size())) return {};
    if (!readU64Checked(p, total, offset, m.timestamp)) return {};
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
    const size_t total = data.size();
    const uint8_t* p = data.data();
    size_t offset = 0;
    if (!readCheckedString(p, total, offset, m.offerId)) return {};
    if (!readCheckedString(p, total, offset, m.sessionId)) return {};
    if (!readCheckedString(p, total, offset, m.providerAddress)) return {};
    if (!readU64Checked(p, total, offset, m.pricePerRequestAtoms)) return {};
    if (!readU64Checked(p, total, offset, m.expiresAt)) return {};
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
    const size_t total = data.size();
    const uint8_t* p = data.data();
    size_t offset = 0;
    if (!readCheckedString(p, total, offset, m.sessionId)) return {};
    if (!readCheckedString(p, total, offset, m.requestId)) return {};
    if (!readCheckedString(p, total, offset, m.prompt)) return {};
    if (!readU32Checked(p, total, offset, m.maxTokens)) return {};
    uint32_t temperatureBits = 0;
    uint32_t topPBits = 0;
    if (!readU32Checked(p, total, offset, temperatureBits)) return {};
    if (!readU32Checked(p, total, offset, topPBits)) return {};
    m.temperature = bitsToFloat(temperatureBits);
    m.topP = bitsToFloat(topPBits);
    if (!readU32Checked(p, total, offset, m.topK)) return {};
    if (!readCheckedString(p, total, offset, m.paymentTxidHex)) return {};
    if (!readCopy(p, total, offset, m.renterPubKey.data(), m.renterPubKey.size())) return {};
    if (!readU64Checked(p, total, offset, m.timestamp)) return {};
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
    const size_t total = data.size();
    const uint8_t* p = data.data();
    size_t offset = 0;
    if (!readCheckedString(p, total, offset, m.requestId)) return {};
    if (!readCheckedString(p, total, offset, m.text)) return {};
    if (!readU32Checked(p, total, offset, m.tokensUsed)) return {};
    if (!readU64Checked(p, total, offset, m.latencyMs)) return {};
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
