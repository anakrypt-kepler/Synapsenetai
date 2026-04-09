#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>

namespace synapse {

constexpr uint32_t PROTOCOL_MAGIC = 0x53594E41;

enum class MessageType : uint8_t {
    VERSION = 0,
    VERACK = 1,
    PING = 2,
    PONG = 3,
    GETPEERS = 4,
    PEERS = 5,
    KNOWLEDGE = 6,
    TRANSFER = 7,
    BLOCK = 8,
    GETBLOCKS = 9,
    INV = 10,
    GETDATA = 11,
    NOTFOUND = 12,
    REJECT = 13,
    CONSENSUS = 14,
    VOTE = 15,
    MODELREQ = 16,
    MODELRESP = 17,
    ALERT = 18
};

enum class InvType : uint8_t {
    TX = 1,
    BLOCK = 2,
    KNOWLEDGE = 3,
    MODEL = 4,
    POE_ENTRY = 5,
    POE_VOTE = 6,
    POE_EPOCH = 7,
    UPDATE_BUNDLE = 8
};

enum class RejectCode : uint8_t {
    MALFORMED = 0x01,
    INVALID = 0x10,
    OBSOLETE = 0x11,
    DUPLICATE = 0x12,
    NONSTANDARD = 0x40,
    DUST = 0x41,
    INSUFFICIENT_FEE = 0x42,
    CHECKPOINT = 0x43
};

struct MessageHeader {
    uint32_t magic;
    char command[12];
    uint32_t length;
    uint32_t checksum;
    
    std::vector<uint8_t> serialize() const;
    static MessageHeader deserialize(const uint8_t* data);
};

struct VersionMessage {
    uint32_t version;
    uint64_t services;
    uint64_t timestamp;
    std::array<uint8_t, 16> addrRecv;
    uint16_t portRecv;
    std::array<uint8_t, 16> addrFrom;
    uint16_t portFrom;
    uint64_t nonce;
    std::string userAgent;
    uint64_t startHeight;
    bool relay;
    
    std::vector<uint8_t> serialize() const;
    static VersionMessage deserialize(const std::vector<uint8_t>& data);
};

struct PeerAddress {
    uint64_t services;
    std::array<uint8_t, 16> addr;
    uint16_t port;
    uint64_t timestamp;
    
    std::vector<uint8_t> serialize() const;
    static PeerAddress deserialize(const uint8_t* data);
};

struct PeersMessage {
    std::vector<PeerAddress> peers;
    
    std::vector<uint8_t> serialize() const;
    static PeersMessage deserialize(const std::vector<uint8_t>& data);
};

struct InvItem {
    InvType type;
    std::array<uint8_t, 32> hash;
    
    std::vector<uint8_t> serialize() const;
    static InvItem deserialize(const uint8_t* data);
};

struct InvMessage {
    std::vector<InvItem> items;
    
    std::vector<uint8_t> serialize() const;
    static InvMessage deserialize(const std::vector<uint8_t>& data);
};

struct GetDataMessage {
    std::vector<InvItem> items;
    
    std::vector<uint8_t> serialize() const;
    static GetDataMessage deserialize(const std::vector<uint8_t>& data);
};

struct KnowledgeMessage {
    uint64_t id;
    std::string question;
    std::string answer;
    std::string source;
    std::vector<std::string> tags;
    std::array<uint8_t, 33> author;
    uint64_t timestamp;
    std::array<uint8_t, 64> signature;
    
    std::vector<uint8_t> serialize() const;
    static KnowledgeMessage deserialize(const std::vector<uint8_t>& data);
    std::array<uint8_t, 32> hash() const;
};

struct TxInput {
    std::array<uint8_t, 32> prevTxHash;
    uint32_t outputIndex;
    std::vector<uint8_t> scriptSig;
    uint32_t sequence;
    
    std::vector<uint8_t> serialize() const;
    static TxInput deserialize(const uint8_t* data, size_t& offset);
};

struct TxOutput {
    uint64_t amount;
    std::vector<uint8_t> scriptPubKey;
    
    std::vector<uint8_t> serialize() const;
    static TxOutput deserialize(const uint8_t* data, size_t& offset);
};

struct TransferMessage {
    uint32_t version;
    std::vector<TxInput> inputs;
    std::vector<TxOutput> outputs;
    uint64_t lockTime;
    
    std::vector<uint8_t> serialize() const;
    static TransferMessage deserialize(const std::vector<uint8_t>& data);
    std::array<uint8_t, 32> hash() const;
    std::array<uint8_t, 32> txid() const;
};

struct BlockHeader {
    uint32_t version;
    std::array<uint8_t, 32> prevHash;
    std::array<uint8_t, 32> merkleRoot;
    uint64_t timestamp;
    uint32_t bits;
    uint32_t nonce;
    
    std::vector<uint8_t> serialize() const;
    static BlockHeader deserialize(const uint8_t* data);
    std::array<uint8_t, 32> hash() const;
};

struct BlockMessage {
    BlockHeader header;
    std::vector<TransferMessage> transactions;
    std::vector<KnowledgeMessage> knowledge;
    
    std::vector<uint8_t> serialize() const;
    static BlockMessage deserialize(const std::vector<uint8_t>& data);
};

struct GetBlocksMessage {
    uint32_t version;
    std::vector<std::array<uint8_t, 32>> locatorHashes;
    std::array<uint8_t, 32> stopHash;
    
    std::vector<uint8_t> serialize() const;
    static GetBlocksMessage deserialize(const std::vector<uint8_t>& data);
};

struct PingMessage {
    uint64_t nonce;
    
    std::vector<uint8_t> serialize() const;
    static PingMessage deserialize(const std::vector<uint8_t>& data);
};

struct PongMessage {
    uint64_t nonce;
    
    std::vector<uint8_t> serialize() const;
    static PongMessage deserialize(const std::vector<uint8_t>& data);
};

struct RejectMessage {
    std::string message;
    RejectCode code;
    std::string reason;
    std::array<uint8_t, 32> data;
    
    std::vector<uint8_t> serialize() const;
    static RejectMessage deserialize(const std::vector<uint8_t>& data);
};

struct VoteMessage {
    uint64_t eventId;
    std::array<uint8_t, 33> validator;
    uint8_t voteType;
    double score;
    uint64_t timestamp;
    std::array<uint8_t, 64> signature;
    
    std::vector<uint8_t> serialize() const;
    static VoteMessage deserialize(const std::vector<uint8_t>& data);
};

struct ModelRequestMessage {
    std::string modelId;
    std::string prompt;
    uint32_t maxTokens;
    float temperature;
    std::array<uint8_t, 33> requester;
    uint64_t timestamp;
    std::array<uint8_t, 64> signature;
    
    std::vector<uint8_t> serialize() const;
    static ModelRequestMessage deserialize(const std::vector<uint8_t>& data);
};

struct ModelResponseMessage {
    std::string requestId;
    std::string response;
    uint32_t tokensUsed;
    uint64_t latencyMs;
    std::array<uint8_t, 33> provider;
    std::array<uint8_t, 64> signature;
    
    std::vector<uint8_t> serialize() const;
    static ModelResponseMessage deserialize(const std::vector<uint8_t>& data);
};

struct AlertMessage {
    uint32_t version;
    uint64_t relayUntil;
    uint64_t expiration;
    uint32_t id;
    uint32_t cancel;
    std::vector<uint32_t> setCancel;
    uint32_t minVer;
    uint32_t maxVer;
    std::string comment;
    std::string statusBar;
    std::array<uint8_t, 64> signature;
    
    std::vector<uint8_t> serialize() const;
    static AlertMessage deserialize(const std::vector<uint8_t>& data);
};

// -----------------------------------------------------------------------------
// Remote model routing (opt-in) — P2P wire objects
//
// These are used by synapsed's P2P layer via `network::Message.command`:
//   - "m_offer"  (RemoteModelOfferMessage)
//   - "m_rent"   (RemoteModelRentMessage)
//   - "m_rentok" (RemoteModelRentOkMessage)
//   - "m_infer"  (RemoteModelInferMessage)
//   - "m_out"    (RemoteModelOutMessage)
//
// Pricing is integers-only (atoms) to keep consensus-style determinism.
// -----------------------------------------------------------------------------

struct RemoteModelOfferMessage {
    std::string offerId;         // opaque id (provider generated)
    std::string modelId;         // provider-local model name/id
    std::string providerAddress; // NGT address to pay (e.g., ngt1...)
    uint64_t pricePerRequestAtoms = 0;
    uint32_t maxSlots = 0;
    uint32_t usedSlots = 0;
    uint64_t expiresAt = 0;      // unix seconds (0 = no expiry)

    std::vector<uint8_t> serialize() const;
    static RemoteModelOfferMessage deserialize(const std::vector<uint8_t>& data);
};

struct RemoteModelRentMessage {
    std::string offerId;
    std::array<uint8_t, 33> renterPubKey{};
    uint64_t timestamp = 0; // unix seconds

    std::vector<uint8_t> serialize() const;
    static RemoteModelRentMessage deserialize(const std::vector<uint8_t>& data);
};

struct RemoteModelRentOkMessage {
    std::string offerId;
    std::string sessionId;       // provider generated
    std::string providerAddress; // echoed for renter convenience
    uint64_t pricePerRequestAtoms = 0;
    uint64_t expiresAt = 0;      // unix seconds (0 = no expiry)

    std::vector<uint8_t> serialize() const;
    static RemoteModelRentOkMessage deserialize(const std::vector<uint8_t>& data);
};

struct RemoteModelInferMessage {
    std::string sessionId;
    std::string requestId;       // renter generated unique id
    std::string prompt;
    uint32_t maxTokens = 512;
    float temperature = 0.7f;
    float topP = 0.9f;
    uint32_t topK = 40;
    std::string paymentTxidHex;  // hex txid paying provider (mempool/confirmed)
    std::array<uint8_t, 33> renterPubKey{};
    uint64_t timestamp = 0;      // unix seconds

    std::vector<uint8_t> serialize() const;
    static RemoteModelInferMessage deserialize(const std::vector<uint8_t>& data);
};

struct RemoteModelOutMessage {
    std::string requestId;
    std::string text;
    uint32_t tokensUsed = 0;
    uint64_t latencyMs = 0;

    std::vector<uint8_t> serialize() const;
    static RemoteModelOutMessage deserialize(const std::vector<uint8_t>& data);
};

}
