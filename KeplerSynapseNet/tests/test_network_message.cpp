#include "network/network.h"
#include "crypto/crypto.h"
#include <cassert>
#include <vector>
#include <cstring>

static uint32_t readChecksum(const std::vector<uint8_t>& serialized) {
    uint32_t checksum = 0;
    std::memcpy(&checksum, serialized.data() + 20, sizeof(checksum));
    return checksum;
}

static void testMessageRoundTrip() {
    synapse::network::Message msg;
    msg.type = synapse::network::MessageType::PING;
    msg.command = "ping";
    msg.payload = {1, 2, 3, 4, 5};
    msg.timestamp = 1;

    auto bytes = msg.serialize();
    assert(!bytes.empty());

    auto parsed = synapse::network::Message::deserialize(bytes);
    assert(parsed.command == "ping");
    assert(parsed.payload == msg.payload);
}

static void testSerializeRejectsInvalidCommand() {
    synapse::network::Message msg;
    msg.type = synapse::network::MessageType::PING;
    msg.command = "pi\ng";
    msg.payload = {1};
    auto bytes = msg.serialize();
    assert(bytes.empty());
}

static void testSerializeRejectsOversizedPayload() {
    synapse::network::Message msg;
    msg.type = synapse::network::MessageType::PING;
    msg.command = "ping";
    msg.payload.assign(synapse::network::MAX_MESSAGE_SIZE + 1, 0x42);
    auto bytes = msg.serialize();
    assert(bytes.empty());
}

static void testDeserializeRejectsTrailingBytes() {
    synapse::network::Message msg;
    msg.type = synapse::network::MessageType::PING;
    msg.command = "ping";
    msg.payload = {7, 8, 9};
    auto bytes = msg.serialize();
    assert(!bytes.empty());
    bytes.push_back(0x00);

    auto parsed = synapse::network::Message::deserialize(bytes);
    assert(parsed.command.empty());
    assert(parsed.payload.empty());
}

static void testDeserializeRejectsBadCommandAndChecksum() {
    synapse::network::Message msg;
    msg.type = synapse::network::MessageType::PING;
    msg.command = "ping";
    msg.payload = {7, 8, 9};
    auto bytes = msg.serialize();
    assert(!bytes.empty());

    auto badCommand = bytes;
    badCommand[4] = 0x01;
    auto parsedBadCommand = synapse::network::Message::deserialize(badCommand);
    assert(parsedBadCommand.command.empty());
    assert(parsedBadCommand.payload.empty());

    auto badChecksum = bytes;
    badChecksum[24] ^= 0x01;
    auto parsedBadChecksum = synapse::network::Message::deserialize(badChecksum);
    assert(parsedBadChecksum.command.empty());
    assert(parsedBadChecksum.payload.empty());
}

static void testHeaderChecksumDependsOnPayload() {
    synapse::network::Message msgA;
    msgA.type = synapse::network::MessageType::PING;
    msgA.command = "ping";
    msgA.payload = {0x10, 0x20, 0x30};

    synapse::network::Message msgB = msgA;
    msgB.payload = {0x10, 0x20, 0x31};

    auto bytesA = msgA.serialize();
    auto bytesB = msgB.serialize();
    assert(!bytesA.empty());
    assert(!bytesB.empty());

    uint32_t checksumA = readChecksum(bytesA);
    uint32_t checksumB = readChecksum(bytesB);
    assert(checksumA != checksumB);

    auto hashA = synapse::crypto::doubleSha256(msgA.payload.data(), msgA.payload.size());
    uint32_t expectedA = 0;
    std::memcpy(&expectedA, hashA.data(), sizeof(expectedA));
    assert(checksumA == expectedA);

    auto hashB = synapse::crypto::doubleSha256(msgB.payload.data(), msgB.payload.size());
    uint32_t expectedB = 0;
    std::memcpy(&expectedB, hashB.data(), sizeof(expectedB));
    assert(checksumB == expectedB);
}

static void testBanUnbanPeerIdAndAddressKeying() {
    synapse::network::Network net;
    assert(!net.isBanned("127.0.0.1"));
    assert(!net.isBanned("127.0.0.1:8333"));

    assert(net.banPeer("127.0.0.1:8333", 60));
    assert(net.isBanned("127.0.0.1"));
    assert(net.isBanned("127.0.0.1:8333"));

    assert(net.unbanPeer("127.0.0.1:8333"));
    assert(!net.isBanned("127.0.0.1"));
    assert(!net.isBanned("127.0.0.1:8333"));
}

int main() {
    testMessageRoundTrip();
    testSerializeRejectsInvalidCommand();
    testSerializeRejectsOversizedPayload();
    testDeserializeRejectsTrailingBytes();
    testDeserializeRejectsBadCommandAndChecksum();
    testHeaderChecksumDependsOnPayload();
    testBanUnbanPeerIdAndAddressKeying();
    return 0;
}
