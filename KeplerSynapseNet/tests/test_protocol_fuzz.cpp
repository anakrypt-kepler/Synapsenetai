#include <cassert>
#include <vector>
#include <cstdint>
#include <random>
#include <iostream>
#include <cstring>
#include "network/network.h"

using namespace synapse::network;

static std::vector<uint8_t> randomBytes(size_t n) {
    std::vector<uint8_t> v(n);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(dis(gen));
    return v;
}

int main() {
    std::cout << "Running protocol fuzz tests...\n";

    // Test 1: 1000 iterations of random byte buffers
    std::cout << "  Test 1: 1000 random byte buffers... ";
    for (int i = 0; i < 1000; ++i) {
        auto buf = randomBytes(static_cast<size_t>(rand() % 4096 + 1));
        try {
            Message m = Message::deserialize(buf);
            // Message should be invalid or empty
            assert(m.payload.empty() || m.command.empty());
        } catch (...) {
            // acceptable - fuzzy input may throw
        }
    }
    std::cout << "PASSED\n";

    // Test 2: Truncation at every byte of a valid message
    std::cout << "  Test 2: Truncated messages... ";
    // Craft minimal valid MessageHeader buffer
    MessageHeader hdr{};
    hdr.magic = 0x53594E41;  // PROTOCOL_MAGIC
    std::fill(hdr.command, hdr.command + 12, 0);
    std::strcpy(hdr.command, "version");
    hdr.length = 0;  // no payload
    hdr.checksum = 0;
    std::vector<uint8_t> validMsg(sizeof(MessageHeader));
    std::memcpy(validMsg.data(), &hdr, sizeof(hdr));
    
    // Test truncation at every byte from 1 to validMsg.size()
    for (size_t truncateAt = 1; truncateAt <= validMsg.size(); ++truncateAt) {
        std::vector<uint8_t> truncated(validMsg.begin(), validMsg.begin() + truncateAt);
        try {
            Message m = Message::deserialize(truncated);
            // Partial message should deserialize to empty/invalid
            assert(m.payload.empty());
        } catch (...) {}
    }
    std::cout << "PASSED\n";

    // Test 3: Huge length header case
    std::cout << "  Test 3: Huge length header... ";
    std::vector<uint8_t> hugeMsg(16, 0xFF);
    try {
        Message m = Message::deserialize(hugeMsg);
        // Should handle gracefully
        assert(m.payload.empty());
    } catch (...) {}
    std::cout << "PASSED\n";

    // Test 4: Empty buffer
    std::cout << "  Test 4: Empty buffer... ";
    std::vector<uint8_t> emptyBuf;
    try {
        Message m = Message::deserialize(emptyBuf);
        assert(m.payload.empty());
    } catch (...) {}
    std::cout << "PASSED\n";

    // Test 5: All-zero buffer
    std::cout << "  Test 5: All-zero buffer... ";
    std::vector<uint8_t> zeroBuf(256, 0x00);
    try {
        Message m = Message::deserialize(zeroBuf);
        assert(m.payload.empty());
    } catch (...) {}
    std::cout << "PASSED\n";

    // Test 6: All-0xFF buffer
    std::cout << "  Test 6: All-0xFF buffer... ";
    std::vector<uint8_t> ffBuf(256, 0xFF);
    try {
        Message m = Message::deserialize(ffBuf);
        assert(m.payload.empty());
    } catch (...) {}
    std::cout << "PASSED\n";

    std::cout << "All protocol fuzz tests PASSED\n";
    return 0;
}
