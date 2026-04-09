#include "quantum/model_inference_crypto.h"
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

ModelInferenceCrypto::ModelInferenceCrypto() = default;

void ModelInferenceCrypto::setSecurityLevel(SecurityLevel level) {
    level_ = level;
}

SecurityLevel ModelInferenceCrypto::getSecurityLevel() const {
    return level_;
}

std::vector<uint8_t> ModelInferenceCrypto::encryptRequest(const std::vector<uint8_t>& data,
                                                          const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};
    return crypto::encryptAES(data, toAesKey(key));
}

std::vector<uint8_t> ModelInferenceCrypto::decryptRequest(const std::vector<uint8_t>& data,
                                                          const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};
    return crypto::decryptAES(data, toAesKey(key));
}

std::vector<uint8_t> ModelInferenceCrypto::encryptResponse(const std::vector<uint8_t>& data,
                                                           const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};
    return crypto::encryptAES(data, toAesKey(key));
}

std::vector<uint8_t> ModelInferenceCrypto::decryptResponse(const std::vector<uint8_t>& data,
                                                           const std::vector<uint8_t>& key) const {
    if (data.empty()) return {};
    return crypto::decryptAES(data, toAesKey(key));
}

}
}
