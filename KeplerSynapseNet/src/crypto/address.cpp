#include "crypto/address.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace synapse {
namespace crypto {

static const char* BASE58_CHARS = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char* BECH32_CHARS = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

static bool isHexSuffix(const std::string& value, size_t start) {
    for (size_t i = start; i < value.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return true;
}

std::string Address::fromPublicKey(const PublicKey& publicKey) {
    Hash256 h160 = hash160(publicKey.data(), publicKey.size());
    return fromHash160(h160);
}

std::string Address::fromHash160(const Hash256& hash160) {
    std::vector<uint8_t> data;
    data.push_back(ADDRESS_VERSION_MAINNET);
    data.insert(data.end(), hash160.begin(), hash160.begin() + 20);
    return std::string(ADDRESS_PREFIX) + base58Encode(data).substr(0, 32);
}

bool Address::isValid(const std::string& address) {
    if (address.length() < 26 || address.length() > 42) return false;
    if (address.substr(0, 4) != ADDRESS_PREFIX) return false;
    
    for (size_t i = 4; i < address.length(); i++) {
        if (std::string(BASE58_CHARS).find(address[i]) == std::string::npos) {
            return false;
        }
    }
    return true;
}

Hash256 Address::toHash160(const std::string& address) {
    Hash256 hash{};
    if (!isValid(address)) return hash;
    
    std::string encoded = address.substr(4);
    auto decoded = base58Decode(encoded);
    if (decoded.size() >= 21) {
        std::copy(decoded.begin() + 1, decoded.begin() + 21, hash.begin());
    }
    return hash;
}

std::string Address::encodeBase58Check(const std::vector<uint8_t>& data) {
    Hash256 checksum = doubleSha256(data.data(), data.size());
    std::vector<uint8_t> dataWithCheck = data;
    dataWithCheck.insert(dataWithCheck.end(), checksum.begin(), checksum.begin() + 4);
    return base58Encode(dataWithCheck);
}

std::vector<uint8_t> Address::decodeBase58Check(const std::string& encoded) {
    std::vector<uint8_t> decoded = base58Decode(encoded);
    if (decoded.size() < 4) return {};
    
    std::vector<uint8_t> data(decoded.begin(), decoded.end() - 4);
    std::vector<uint8_t> checksum(decoded.end() - 4, decoded.end());
    
    Hash256 calcCheck = doubleSha256(data.data(), data.size());
    for (int i = 0; i < 4; i++) {
        if (checksum[i] != calcCheck[i]) return {};
    }
    
    return data;
}

std::string Address::base58Encode(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> digits(data.size() * 138 / 100 + 1, 0);
    size_t digitsLen = 1;
    
    for (size_t i = 0; i < data.size(); i++) {
        uint32_t carry = data[i];
        for (size_t j = 0; j < digitsLen; j++) {
            carry += static_cast<uint32_t>(digits[j]) << 8;
            digits[j] = carry % 58;
            carry /= 58;
        }
        while (carry > 0) {
            digits[digitsLen++] = carry % 58;
            carry /= 58;
        }
    }
    
    std::string result;
    for (size_t i = 0; i < data.size() && data[i] == 0; i++) {
        result += BASE58_CHARS[0];
    }
    for (size_t i = digitsLen; i > 0; i--) {
        result += BASE58_CHARS[digits[i - 1]];
    }
    
    return result;
}

std::vector<uint8_t> Address::base58Decode(const std::string& encoded) {
    std::vector<uint8_t> result(encoded.size() * 733 / 1000 + 1, 0);
    size_t resultLen = 1;
    
    for (char c : encoded) {
        const char* p = std::strchr(BASE58_CHARS, c);
        if (!p) return {};
        
        uint32_t carry = p - BASE58_CHARS;
        for (size_t j = 0; j < resultLen; j++) {
            carry += static_cast<uint32_t>(result[j]) * 58;
            result[j] = carry & 0xff;
            carry >>= 8;
        }
        while (carry > 0) {
            result[resultLen++] = carry & 0xff;
            carry >>= 8;
        }
    }
    
    for (size_t i = 0; i < encoded.size() && encoded[i] == BASE58_CHARS[0]; i++) {
        result[resultLen++] = 0;
    }
    
    std::reverse(result.begin(), result.begin() + resultLen);
    result.resize(resultLen);
    return result;
}

static uint32_t bech32Polymod(const std::vector<uint8_t>& values) {
    uint32_t chk = 1;
    for (uint8_t v : values) {
        uint8_t top = chk >> 25;
        chk = ((chk & 0x1ffffff) << 5) ^ v;
        if (top & 1) chk ^= 0x3b6a57b2;
        if (top & 2) chk ^= 0x26508e6d;
        if (top & 4) chk ^= 0x1ea119fa;
        if (top & 8) chk ^= 0x3d4233dd;
        if (top & 16) chk ^= 0x2a1462b3;
    }
    return chk;
}

std::string Address::encodeBech32(const std::string& hrp, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> values;
    for (char c : hrp) values.push_back(c >> 5);
    values.push_back(0);
    for (char c : hrp) values.push_back(c & 31);
    values.insert(values.end(), data.begin(), data.end());
    values.resize(values.size() + 6, 0);
    
    uint32_t polymod = bech32Polymod(values) ^ 1;
    std::vector<uint8_t> checksum(6);
    for (int i = 0; i < 6; i++) {
        checksum[i] = (polymod >> (5 * (5 - i))) & 31;
    }
    
    std::string result = hrp + "1";
    for (uint8_t d : data) result += BECH32_CHARS[d];
    for (uint8_t c : checksum) result += BECH32_CHARS[c];
    
    return result;
}

std::vector<uint8_t> Address::decodeBech32(const std::string& encoded, std::string& hrp) {
    size_t pos = encoded.rfind('1');
    if (pos == std::string::npos || pos < 1 || pos + 7 > encoded.size()) {
        return {};
    }
    
    hrp = encoded.substr(0, pos);
    std::vector<uint8_t> data;
    
    for (size_t i = pos + 1; i < encoded.size(); i++) {
        const char* p = std::strchr(BECH32_CHARS, encoded[i]);
        if (!p) return {};
        data.push_back(p - BECH32_CHARS);
    }
    
    if (data.size() < 6) return {};
    data.resize(data.size() - 6);
    
    return data;
}

std::vector<uint8_t> Address::decode(const std::string& address) {
    if (!isValid(address)) return {};
    std::string encoded = address.substr(4);
    return base58Decode(encoded);
}

std::string Address::encode(const std::vector<uint8_t>& data) {
    return std::string(ADDRESS_PREFIX) + base58Encode(data).substr(0, 32);
}

AddressType Address::getType(const std::string& address) {
    if (!isValid(address)) return AddressType::UNKNOWN;
    if (address.substr(0, 4) == "syn1") return AddressType::P2PKH;
    if (address.substr(0, 4) == "syn3") return AddressType::P2SH;
    if (address.substr(0, 4) == "synq") return AddressType::BECH32;
    return AddressType::UNKNOWN;
}

std::string Address::fromScript(const std::vector<uint8_t>& script) {
    if (script.size() == 25 && script[0] == 0x76 && script[1] == 0xa9) {
        Hash256 hash{};
        std::copy(script.begin() + 3, script.begin() + 23, hash.begin());
        return fromHash160(hash);
    }
    return "";
}

std::vector<uint8_t> Address::toScript(const std::string& address) {
    if (!isValid(address)) return {};
    
    Hash256 hash = toHash160(address);
    std::vector<uint8_t> script;
    script.push_back(0x76);
    script.push_back(0xa9);
    script.push_back(0x14);
    script.insert(script.end(), hash.begin(), hash.begin() + 20);
    script.push_back(0x88);
    script.push_back(0xac);
    return script;
}

std::string Address::createMultisig(uint8_t required, const std::vector<PublicKey>& pubkeys) {
    if (required == 0 || required > pubkeys.size() || pubkeys.size() > 15) {
        return "";
    }
    
    std::vector<uint8_t> script;
    script.push_back(0x50 + required);
    
    for (const auto& pk : pubkeys) {
        script.push_back(static_cast<uint8_t>(pk.size()));
        script.insert(script.end(), pk.begin(), pk.end());
    }
    
    script.push_back(0x50 + static_cast<uint8_t>(pubkeys.size()));
    script.push_back(0xae);
    
    Hash256 scriptHash = hash160(script.data(), script.size());
    
    std::vector<uint8_t> data;
    data.push_back(ADDRESS_VERSION_P2SH);
    data.insert(data.end(), scriptHash.begin(), scriptHash.begin() + 20);
    
    return "syn3" + base58Encode(data).substr(0, 32);
}

bool Address::verifyChecksum(const std::string& address) {
    if (!isValid(address)) return false;
    
    std::string encoded = address.substr(4);
    auto decoded = base58Decode(encoded);
    if (decoded.size() < 4) return false;
    
    std::vector<uint8_t> data(decoded.begin(), decoded.end() - 4);
    std::vector<uint8_t> checksum(decoded.end() - 4, decoded.end());
    
    Hash256 calcCheck = doubleSha256(data.data(), data.size());
    for (int i = 0; i < 4; i++) {
        if (checksum[i] != calcCheck[i]) return false;
    }
    return true;
}

std::string Address::toSegwit(const std::string& address) {
    if (!isValid(address)) return "";
    
    Hash256 hash = toHash160(address);
    std::vector<uint8_t> data;
    data.push_back(0);
    
    for (int i = 0; i < 20; i++) {
        data.push_back(hash[i]);
    }
    
    return encodeBech32("syn", data);
}

std::string Address::fromSegwit(const std::string& segwitAddr) {
    std::string hrp;
    auto data = decodeBech32(segwitAddr, hrp);
    
    if (hrp != "syn" || data.empty()) return "";
    
    Hash256 hash{};
    for (size_t i = 1; i < data.size() && i <= 20; i++) {
        hash[i - 1] = data[i];
    }
    
    return fromHash160(hash);
}

std::string canonicalWalletAddressFromPublicKey(const PublicKey& publicKey) {
    std::string hexKey = toHex(publicKey);
    if (hexKey.size() < 52) return "";
    return std::string(WALLET_ADDRESS_PREFIX) + hexKey.substr(0, 52);
}

std::string legacyWalletAddressFromPublicKey(const PublicKey& publicKey) {
    Hash256 hash = hash160(publicKey.data(), publicKey.size());
    return std::string(LEGACY_WALLET_ADDRESS_PREFIX) + toHex(hash.data(), 20);
}

std::array<std::string, 2> walletAddressAliasesFromPublicKey(const PublicKey& publicKey) {
    return {canonicalWalletAddressFromPublicKey(publicKey),
            legacyWalletAddressFromPublicKey(publicKey)};
}

bool isCanonicalWalletAddress(const std::string& address) {
    return address.size() == 56 &&
           address.rfind(WALLET_ADDRESS_PREFIX, 0) == 0 &&
           isHexSuffix(address, 4);
}

bool isLegacyWalletAddress(const std::string& address) {
    return address.size() == 42 &&
           address.rfind(LEGACY_WALLET_ADDRESS_PREFIX, 0) == 0 &&
           isHexSuffix(address, 2);
}

bool isSupportedWalletAddress(const std::string& address) {
    return isCanonicalWalletAddress(address) || isLegacyWalletAddress(address);
}

bool walletAddressMatchesPublicKey(const std::string& address, const PublicKey& publicKey) {
    auto aliases = walletAddressAliasesFromPublicKey(publicKey);
    return address == aliases[0] || address == aliases[1];
}

}
}
