#pragma once

#include <vector>
#include <cstdint>
#include <array>
#include <memory>

namespace synapse {
namespace network {

class ChannelCipher {
public:
    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t NONCE_SIZE = 12;
    static constexpr size_t TAG_SIZE = 16;

    ChannelCipher();
    ~ChannelCipher();

    bool init(const std::vector<uint8_t>& sessionKey, bool isInitiator);

    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext);

    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext);

    bool isReady() const;

    void rotateKeys();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
