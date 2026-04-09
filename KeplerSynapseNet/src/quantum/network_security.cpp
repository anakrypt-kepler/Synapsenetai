#include "quantum/network_security.h"
#include "crypto/crypto.h"
#include <array>
#include <algorithm>

namespace synapse {
namespace quantum {

static std::array<uint8_t, crypto::AES_KEY_SIZE> toAesKey(const std::vector<uint8_t>& key) {
    std::array<uint8_t, crypto::AES_KEY_SIZE> out{};
    if (key.size() >= out.size()) {
        std::copy_n(key.begin(), out.size(), out.begin());
        return out;
    }
    auto hash = crypto::sha256(key.data(), key.size());
    std::copy_n(hash.begin(), out.size(), out.begin());
    return out;
}

NetworkSecurity::NetworkSecurity() = default;

void NetworkSecurity::setSecurityLevel(SecurityLevel level) {
    level_ = level;
}

SecurityLevel NetworkSecurity::getSecurityLevel() const {
    return level_;
}

std::vector<uint8_t> NetworkSecurity::deriveSessionKey(const std::vector<uint8_t>& sharedSecret,
                                                       const std::vector<uint8_t>& nonce) const {
    std::vector<uint8_t> data = sharedSecret;
    data.insert(data.end(), nonce.begin(), nonce.end());
    auto hash = crypto::sha256(data.data(), data.size());
    return std::vector<uint8_t>(hash.begin(), hash.end());
}

std::vector<uint8_t> NetworkSecurity::encryptPacket(const std::vector<uint8_t>& data,
                                                    const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};
    return crypto::encryptAES(data, toAesKey(key));
}

std::vector<uint8_t> NetworkSecurity::decryptPacket(const std::vector<uint8_t>& data,
                                                    const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};
    return crypto::decryptAES(data, toAesKey(key));
}

}
}
