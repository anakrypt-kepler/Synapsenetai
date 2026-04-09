#include "quantum/knowledge_crypto.h"
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

KnowledgeCrypto::KnowledgeCrypto() = default;

void KnowledgeCrypto::setSecurityLevel(SecurityLevel level) {
    level_ = level;
}

SecurityLevel KnowledgeCrypto::getSecurityLevel() const {
    return level_;
}

std::vector<uint8_t> KnowledgeCrypto::deriveKey(const std::vector<uint8_t>& seed,
                                                const std::string& context) const {
    std::vector<uint8_t> data = seed;
    data.insert(data.end(), context.begin(), context.end());
    auto hash = crypto::sha256(data.data(), data.size());
    return std::vector<uint8_t>(hash.begin(), hash.end());
}

std::vector<uint8_t> KnowledgeCrypto::encrypt(const std::vector<uint8_t>& data,
                                              const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};
    return crypto::encryptAES(data, toAesKey(key));
}

std::vector<uint8_t> KnowledgeCrypto::decrypt(const std::vector<uint8_t>& data,
                                              const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};
    return crypto::decryptAES(data, toAesKey(key));
}

}
}
