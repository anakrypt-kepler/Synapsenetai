#include "utils/config.h"
#include <cstring>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstdint>

namespace synapse {
namespace utils {

class ByteBuffer {
public:
    ByteBuffer() : readPos_(0) {}
    explicit ByteBuffer(const std::vector<uint8_t>& data) : data_(data), readPos_(0) {}
    explicit ByteBuffer(size_t capacity) : readPos_(0) { data_.reserve(capacity); }
    
    void writeUint8(uint8_t value) {
        data_.push_back(value);
    }
    
    void writeUint16(uint16_t value) {
        data_.push_back(static_cast<uint8_t>(value >> 8));
        data_.push_back(static_cast<uint8_t>(value & 0xFF));
    }
    
    void writeUint32(uint32_t value) {
        data_.push_back(static_cast<uint8_t>(value >> 24));
        data_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        data_.push_back(static_cast<uint8_t>(value & 0xFF));
    }
    
    void writeUint64(uint64_t value) {
        for (int i = 7; i >= 0; i--) {
            data_.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
        }
    }
    
    void writeInt8(int8_t value) {
        writeUint8(static_cast<uint8_t>(value));
    }
    
    void writeInt16(int16_t value) {
        writeUint16(static_cast<uint16_t>(value));
    }
    
    void writeInt32(int32_t value) {
        writeUint32(static_cast<uint32_t>(value));
    }
    
    void writeInt64(int64_t value) {
        writeUint64(static_cast<uint64_t>(value));
    }
    
    void writeFloat(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        writeUint32(bits);
    }
    
    void writeDouble(double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        writeUint64(bits);
    }
    
    void writeBool(bool value) {
        writeUint8(value ? 1 : 0);
    }
    
    void writeVarInt(uint64_t value) {
        while (value >= 0x80) {
            data_.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        data_.push_back(static_cast<uint8_t>(value));
    }
    
    void writeString(const std::string& value) {
        writeVarInt(value.length());
        data_.insert(data_.end(), value.begin(), value.end());
    }
    
    void writeBytes(const std::vector<uint8_t>& value) {
        writeVarInt(value.size());
        data_.insert(data_.end(), value.begin(), value.end());
    }
    
    void writeFixedBytes(const uint8_t* data, size_t length) {
        data_.insert(data_.end(), data, data + length);
    }
    
    uint8_t readUint8() {
        checkRead(1);
        return data_[readPos_++];
    }
    
    uint16_t readUint16() {
        checkRead(2);
        uint16_t value = (static_cast<uint16_t>(data_[readPos_]) << 8) |
                         static_cast<uint16_t>(data_[readPos_ + 1]);
        readPos_ += 2;
        return value;
    }
    
    uint32_t readUint32() {
        checkRead(4);
        uint32_t value = (static_cast<uint32_t>(data_[readPos_]) << 24) |
                         (static_cast<uint32_t>(data_[readPos_ + 1]) << 16) |
                         (static_cast<uint32_t>(data_[readPos_ + 2]) << 8) |
                         static_cast<uint32_t>(data_[readPos_ + 3]);
        readPos_ += 4;
        return value;
    }
    
    uint64_t readUint64() {
        checkRead(8);
        uint64_t value = 0;
        for (int i = 0; i < 8; i++) {
            value = (value << 8) | static_cast<uint64_t>(data_[readPos_ + i]);
        }
        readPos_ += 8;
        return value;
    }
    
    int8_t readInt8() {
        return static_cast<int8_t>(readUint8());
    }
    
    int16_t readInt16() {
        return static_cast<int16_t>(readUint16());
    }
    
    int32_t readInt32() {
        return static_cast<int32_t>(readUint32());
    }
    
    int64_t readInt64() {
        return static_cast<int64_t>(readUint64());
    }
    
    float readFloat() {
        uint32_t bits = readUint32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    
    double readDouble() {
        uint64_t bits = readUint64();
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    
    bool readBool() {
        return readUint8() != 0;
    }
    
    uint64_t readVarInt() {
        uint64_t value = 0;
        int shift = 0;
        
        while (true) {
            checkRead(1);
            uint8_t byte = data_[readPos_++];
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            
            if ((byte & 0x80) == 0) break;
            shift += 7;
            
            if (shift >= 64) {
                throw std::runtime_error("VarInt overflow");
            }
        }
        
        return value;
    }
    
    std::string readString() {
        uint64_t length = readVarInt();
        if (length > 4 * 1024 * 1024) {
            throw std::runtime_error("String length exceeds maximum allowed size");
        }
        checkRead(length);

        std::string value(data_.begin() + readPos_, data_.begin() + readPos_ + length);
        readPos_ += length;
        return value;
    }

    std::vector<uint8_t> readBytes() {
        uint64_t length = readVarInt();
        if (length > 4 * 1024 * 1024) {
            throw std::runtime_error("Bytes length exceeds maximum allowed size");
        }
        checkRead(length);

        std::vector<uint8_t> value(data_.begin() + readPos_, data_.begin() + readPos_ + length);
        readPos_ += length;
        return value;
    }
    
    void readFixedBytes(uint8_t* dest, size_t length) {
        checkRead(length);
        std::memcpy(dest, data_.data() + readPos_, length);
        readPos_ += length;
    }
    
    const std::vector<uint8_t>& data() const { return data_; }
    size_t size() const { return data_.size(); }
    size_t remaining() const { return data_.size() - readPos_; }
    size_t position() const { return readPos_; }
    void seek(size_t pos) { readPos_ = pos; }
    void reset() { readPos_ = 0; }
    void clear() { data_.clear(); readPos_ = 0; }
    
private:
    std::vector<uint8_t> data_;
    size_t readPos_;
    
    void checkRead(size_t bytes) {
        if (readPos_ + bytes > data_.size()) {
            throw std::runtime_error("Buffer underflow");
        }
    }
};

class Serializer {
public:
    static std::vector<uint8_t> serializeTransaction(const std::string& from, const std::string& to,
                                                      uint64_t amount, uint64_t fee, uint64_t nonce,
                                                      const std::vector<uint8_t>& signature) {
        ByteBuffer buffer;
        
        buffer.writeUint8(0x01);
        buffer.writeString(from);
        buffer.writeString(to);
        buffer.writeUint64(amount);
        buffer.writeUint64(fee);
        buffer.writeUint64(nonce);
        buffer.writeBytes(signature);
        
        return buffer.data();
    }
    
    static bool deserializeTransaction(const std::vector<uint8_t>& data, std::string& from,
                                        std::string& to, uint64_t& amount, uint64_t& fee,
                                        uint64_t& nonce, std::vector<uint8_t>& signature) {
        try {
            ByteBuffer buffer(data);
            
            uint8_t version = buffer.readUint8();
            if (version != 0x01) return false;
            
            from = buffer.readString();
            to = buffer.readString();
            amount = buffer.readUint64();
            fee = buffer.readUint64();
            nonce = buffer.readUint64();
            signature = buffer.readBytes();
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static std::vector<uint8_t> serializeKnowledge(const std::string& submitter, const std::string& category,
                                                    const std::string& content, uint64_t timestamp,
                                                    const std::vector<uint8_t>& contentHash,
                                                    const std::vector<uint8_t>& signature) {
        ByteBuffer buffer;
        
        buffer.writeUint8(0x02);
        buffer.writeString(submitter);
        buffer.writeString(category);
        buffer.writeString(content);
        buffer.writeUint64(timestamp);
        buffer.writeBytes(contentHash);
        buffer.writeBytes(signature);
        
        return buffer.data();
    }
    
    static bool deserializeKnowledge(const std::vector<uint8_t>& data, std::string& submitter,
                                      std::string& category, std::string& content, uint64_t& timestamp,
                                      std::vector<uint8_t>& contentHash, std::vector<uint8_t>& signature) {
        try {
            ByteBuffer buffer(data);
            
            uint8_t version = buffer.readUint8();
            if (version != 0x02) return false;
            
            submitter = buffer.readString();
            category = buffer.readString();
            content = buffer.readString();
            timestamp = buffer.readUint64();
            contentHash = buffer.readBytes();
            signature = buffer.readBytes();
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static std::vector<uint8_t> serializeBlock(uint64_t height, const std::vector<uint8_t>& prevHash,
                                                uint64_t timestamp, const std::vector<uint8_t>& merkleRoot,
                                                uint32_t nonce, const std::vector<std::vector<uint8_t>>& transactions) {
        ByteBuffer buffer;
        
        buffer.writeUint8(0x03);
        buffer.writeUint64(height);
        buffer.writeBytes(prevHash);
        buffer.writeUint64(timestamp);
        buffer.writeBytes(merkleRoot);
        buffer.writeUint32(nonce);
        
        buffer.writeVarInt(transactions.size());
        for (const auto& tx : transactions) {
            buffer.writeBytes(tx);
        }
        
        return buffer.data();
    }
    
    static bool deserializeBlock(const std::vector<uint8_t>& data, uint64_t& height,
                                  std::vector<uint8_t>& prevHash, uint64_t& timestamp,
                                  std::vector<uint8_t>& merkleRoot, uint32_t& nonce,
                                  std::vector<std::vector<uint8_t>>& transactions) {
        try {
            ByteBuffer buffer(data);
            
            uint8_t version = buffer.readUint8();
            if (version != 0x03) return false;
            
            height = buffer.readUint64();
            prevHash = buffer.readBytes();
            timestamp = buffer.readUint64();
            merkleRoot = buffer.readBytes();
            nonce = buffer.readUint32();
            
            uint64_t txCount = buffer.readVarInt();
            if (txCount > 100000) {
                return false;
            }
            transactions.clear();
            transactions.reserve(txCount);
            
            for (uint64_t i = 0; i < txCount; i++) {
                transactions.push_back(buffer.readBytes());
            }
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static std::vector<uint8_t> serializePeer(const std::string& nodeId, const std::string& address,
                                               uint16_t port, uint64_t lastSeen, uint32_t reputation) {
        ByteBuffer buffer;
        
        buffer.writeUint8(0x04);
        buffer.writeString(nodeId);
        buffer.writeString(address);
        buffer.writeUint16(port);
        buffer.writeUint64(lastSeen);
        buffer.writeUint32(reputation);
        
        return buffer.data();
    }
    
    static bool deserializePeer(const std::vector<uint8_t>& data, std::string& nodeId,
                                 std::string& address, uint16_t& port, uint64_t& lastSeen,
                                 uint32_t& reputation) {
        try {
            ByteBuffer buffer(data);
            
            uint8_t version = buffer.readUint8();
            if (version != 0x04) return false;
            
            nodeId = buffer.readString();
            address = buffer.readString();
            port = buffer.readUint16();
            lastSeen = buffer.readUint64();
            reputation = buffer.readUint32();
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static std::vector<uint8_t> serializeModelInfo(const std::string& modelId, const std::string& name,
                                                    const std::string& format, uint64_t size,
                                                    const std::string& hash, const std::string& owner) {
        ByteBuffer buffer;
        
        buffer.writeUint8(0x05);
        buffer.writeString(modelId);
        buffer.writeString(name);
        buffer.writeString(format);
        buffer.writeUint64(size);
        buffer.writeString(hash);
        buffer.writeString(owner);
        
        return buffer.data();
    }
    
    static bool deserializeModelInfo(const std::vector<uint8_t>& data, std::string& modelId,
                                      std::string& name, std::string& format, uint64_t& size,
                                      std::string& hash, std::string& owner) {
        try {
            ByteBuffer buffer(data);
            
            uint8_t version = buffer.readUint8();
            if (version != 0x05) return false;
            
            modelId = buffer.readString();
            name = buffer.readString();
            format = buffer.readString();
            size = buffer.readUint64();
            hash = buffer.readString();
            owner = buffer.readString();
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static std::vector<uint8_t> serializeValidation(const std::string& knowledgeId, const std::string& validator,
                                                     uint8_t vote, uint32_t score, uint64_t timestamp,
                                                     const std::vector<uint8_t>& signature) {
        ByteBuffer buffer;
        
        buffer.writeUint8(0x06);
        buffer.writeString(knowledgeId);
        buffer.writeString(validator);
        buffer.writeUint8(vote);
        buffer.writeUint32(score);
        buffer.writeUint64(timestamp);
        buffer.writeBytes(signature);
        
        return buffer.data();
    }
    
    static bool deserializeValidation(const std::vector<uint8_t>& data, std::string& knowledgeId,
                                       std::string& validator, uint8_t& vote, uint32_t& score,
                                       uint64_t& timestamp, std::vector<uint8_t>& signature) {
        try {
            ByteBuffer buffer(data);
            
            uint8_t version = buffer.readUint8();
            if (version != 0x06) return false;
            
            knowledgeId = buffer.readString();
            validator = buffer.readString();
            vote = buffer.readUint8();
            score = buffer.readUint32();
            timestamp = buffer.readUint64();
            signature = buffer.readBytes();
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static std::vector<uint8_t> serializeStake(const std::string& address, uint64_t amount,
                                                uint64_t lockTime, uint64_t unlockTime,
                                                const std::vector<uint8_t>& signature) {
        ByteBuffer buffer;
        
        buffer.writeUint8(0x07);
        buffer.writeString(address);
        buffer.writeUint64(amount);
        buffer.writeUint64(lockTime);
        buffer.writeUint64(unlockTime);
        buffer.writeBytes(signature);
        
        return buffer.data();
    }
    
    static bool deserializeStake(const std::vector<uint8_t>& data, std::string& address,
                                  uint64_t& amount, uint64_t& lockTime, uint64_t& unlockTime,
                                  std::vector<uint8_t>& signature) {
        try {
            ByteBuffer buffer(data);
            
            uint8_t version = buffer.readUint8();
            if (version != 0x07) return false;
            
            address = buffer.readString();
            amount = buffer.readUint64();
            lockTime = buffer.readUint64();
            unlockTime = buffer.readUint64();
            signature = buffer.readBytes();
            
            return true;
        } catch (...) {
            return false;
        }
    }
};


class HexEncoder {
public:
    static std::string encode(const std::vector<uint8_t>& data) {
        std::stringstream ss;
        for (uint8_t byte : data) {
            ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
        }
        return ss.str();
    }
    
    static std::string encode(const uint8_t* data, size_t length) {
        std::stringstream ss;
        for (size_t i = 0; i < length; i++) {
            ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[i]);
        }
        return ss.str();
    }
    
    static std::vector<uint8_t> decode(const std::string& hex) {
        std::vector<uint8_t> data;
        
        if (hex.length() % 2 != 0) {
            return data;
        }
        
        data.reserve(hex.length() / 2);
        
        for (size_t i = 0; i < hex.length(); i += 2) {
            uint8_t byte = 0;
            
            char c1 = hex[i];
            char c2 = hex[i + 1];
            
            if (c1 >= '0' && c1 <= '9') byte = (c1 - '0') << 4;
            else if (c1 >= 'a' && c1 <= 'f') byte = (c1 - 'a' + 10) << 4;
            else if (c1 >= 'A' && c1 <= 'F') byte = (c1 - 'A' + 10) << 4;
            else return {};  // Invalid hex character in high nibble
            
            if (c2 >= '0' && c2 <= '9') byte |= (c2 - '0');
            else if (c2 >= 'a' && c2 <= 'f') byte |= (c2 - 'a' + 10);
            else if (c2 >= 'A' && c2 <= 'F') byte |= (c2 - 'A' + 10);
            else return {};  // Invalid hex character in low nibble
            
            data.push_back(byte);
        }
        
        return data;
    }
    
    static bool isValid(const std::string& hex) {
        if (hex.length() % 2 != 0) return false;
        
        for (char c : hex) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                return false;
            }
        }
        
        return true;
    }
};

class Base64Encoder {
public:
    static std::string encode(const std::vector<uint8_t>& data) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        std::string result;
        result.reserve((data.size() + 2) / 3 * 4);
        
        size_t i = 0;
        while (i < data.size()) {
            uint32_t octet_a = i < data.size() ? data[i++] : 0;
            uint32_t octet_b = i < data.size() ? data[i++] : 0;
            uint32_t octet_c = i < data.size() ? data[i++] : 0;
            
            uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
            
            result += chars[(triple >> 18) & 0x3F];
            result += chars[(triple >> 12) & 0x3F];
            result += (i > data.size() + 1) ? '=' : chars[(triple >> 6) & 0x3F];
            result += (i > data.size()) ? '=' : chars[triple & 0x3F];
        }
        
        return result;
    }
    
    static std::vector<uint8_t> decode(const std::string& encoded) {
        static const int decodeTable[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
        };
        
        std::vector<uint8_t> result;
        
        if (encoded.length() % 4 != 0) return result;
        
        size_t padding = 0;
        if (!encoded.empty() && encoded[encoded.length() - 1] == '=') padding++;
        if (encoded.length() > 1 && encoded[encoded.length() - 2] == '=') padding++;
        
        result.reserve((encoded.length() / 4) * 3 - padding);
        
        for (size_t i = 0; i < encoded.length(); i += 4) {
            int a = decodeTable[static_cast<unsigned char>(encoded[i])];
            int b = decodeTable[static_cast<unsigned char>(encoded[i + 1])];
            int c = decodeTable[static_cast<unsigned char>(encoded[i + 2])];
            int d = decodeTable[static_cast<unsigned char>(encoded[i + 3])];
            
            // First two base64 chars must be valid; return empty on decode error
            if (a == -1 || b == -1) return {};
            
            uint32_t triple = (a << 18) + (b << 12);
            
            if (c != -1) triple += (c << 6);
            if (d != -1) triple += d;
            
            result.push_back((triple >> 16) & 0xFF);
            if (encoded[i + 2] != '=') result.push_back((triple >> 8) & 0xFF);
            if (encoded[i + 3] != '=') result.push_back(triple & 0xFF);
        }
        
        return result;
    }
    
    static bool isValid(const std::string& encoded) {
        if (encoded.length() % 4 != 0) return false;
        
        for (size_t i = 0; i < encoded.length(); i++) {
            char c = encoded[i];
            bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') || c == '+' || c == '/';
            
            if (!valid && c == '=' && i >= encoded.length() - 2) continue;
            if (!valid) return false;
        }
        
        return true;
    }
};


class Base58Encoder {
public:
    static std::string encode(const std::vector<uint8_t>& data) {
        static const char* chars = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        
        std::vector<uint8_t> digits;
        digits.push_back(0);
        
        for (uint8_t byte : data) {
            int carry = byte;
            for (size_t i = 0; i < digits.size(); i++) {
                carry += digits[i] * 256;
                digits[i] = carry % 58;
                carry /= 58;
            }
            while (carry > 0) {
                digits.push_back(carry % 58);
                carry /= 58;
            }
        }
        
        std::string result;
        
        for (size_t i = 0; i < data.size() && data[i] == 0; i++) {
            result += '1';
        }
        
        for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
            result += chars[*it];
        }
        
        return result;
    }
    
    static std::vector<uint8_t> decode(const std::string& encoded) {
        static const int decodeTable[128] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8,-1,-1,-1,-1,-1,-1,
            -1, 9,10,11,12,13,14,15,16,-1,17,18,19,20,21,-1,
            22,23,24,25,26,27,28,29,30,31,32,-1,-1,-1,-1,-1,
            -1,33,34,35,36,37,38,39,40,41,42,43,-1,44,45,46,
            47,48,49,50,51,52,53,54,55,56,57,-1,-1,-1,-1,-1
        };
        
        std::vector<uint8_t> bytes;
        bytes.push_back(0);
        
        for (char c : encoded) {
            uint8_t uc = static_cast<uint8_t>(c);
            if (decodeTable[uc] == -1) {
                // Invalid character in base58 encoding
                return {};
            }
            
            int carry = decodeTable[uc];
            for (size_t i = 0; i < bytes.size(); i++) {
                carry += bytes[i] * 58;
                bytes[i] = carry % 256;
                carry /= 256;
            }
            while (carry > 0) {
                bytes.push_back(carry % 256);
                carry /= 256;
            }
        }
        
        std::vector<uint8_t> result;
        
        for (size_t i = 0; i < encoded.size() && encoded[i] == '1'; i++) {
            result.push_back(0);
        }
        
        for (auto it = bytes.rbegin(); it != bytes.rend(); ++it) {
            result.push_back(*it);
        }
        
        return result;
    }
    
    static bool isValid(const std::string& encoded) {
        for (char c : encoded) {
            bool valid = (c >= '1' && c <= '9') || (c >= 'A' && c <= 'H') ||
                         (c >= 'J' && c <= 'N') || (c >= 'P' && c <= 'Z') ||
                         (c >= 'a' && c <= 'k') || (c >= 'm' && c <= 'z');
            if (!valid) return false;
        }
        return true;
    }
};


class MessagePacker {
public:
    static std::vector<uint8_t> packHandshake(uint32_t version, const std::string& nodeId,
                                               uint16_t port, uint64_t timestamp) {
        ByteBuffer buffer;
        buffer.writeUint8(0x10);
        buffer.writeUint32(version);
        buffer.writeString(nodeId);
        buffer.writeUint16(port);
        buffer.writeUint64(timestamp);
        return buffer.data();
    }
    
    static std::vector<uint8_t> packPing(uint64_t nonce) {
        ByteBuffer buffer;
        buffer.writeUint8(0x11);
        buffer.writeUint64(nonce);
        return buffer.data();
    }
    
    static std::vector<uint8_t> packPong(uint64_t nonce) {
        ByteBuffer buffer;
        buffer.writeUint8(0x12);
        buffer.writeUint64(nonce);
        return buffer.data();
    }
    
    static std::vector<uint8_t> packGetPeers() {
        ByteBuffer buffer;
        buffer.writeUint8(0x13);
        return buffer.data();
    }
    
    static std::vector<uint8_t> packPeers(const std::vector<std::pair<std::string, uint16_t>>& peers) {
        ByteBuffer buffer;
        buffer.writeUint8(0x14);
        buffer.writeVarInt(peers.size());
        for (const auto& peer : peers) {
            buffer.writeString(peer.first);
            buffer.writeUint16(peer.second);
        }
        return buffer.data();
    }
    
    static std::vector<uint8_t> packGetBlocks(uint64_t startHeight, uint32_t count) {
        ByteBuffer buffer;
        buffer.writeUint8(0x15);
        buffer.writeUint64(startHeight);
        buffer.writeUint32(count);
        return buffer.data();
    }
    
    static std::vector<uint8_t> packBlocks(const std::vector<std::vector<uint8_t>>& blocks) {
        ByteBuffer buffer;
        buffer.writeUint8(0x16);
        buffer.writeVarInt(blocks.size());
        for (const auto& block : blocks) {
            buffer.writeBytes(block);
        }
        return buffer.data();
    }
    
    static std::vector<uint8_t> packTransaction(const std::vector<uint8_t>& tx) {
        ByteBuffer buffer;
        buffer.writeUint8(0x17);
        buffer.writeBytes(tx);
        return buffer.data();
    }
    
    static std::vector<uint8_t> packKnowledge(const std::vector<uint8_t>& knowledge) {
        ByteBuffer buffer;
        buffer.writeUint8(0x18);
        buffer.writeBytes(knowledge);
        return buffer.data();
    }
    
    static std::vector<uint8_t> packValidation(const std::vector<uint8_t>& validation) {
        ByteBuffer buffer;
        buffer.writeUint8(0x19);
        buffer.writeBytes(validation);
        return buffer.data();
    }
    
    static std::vector<uint8_t> packModelRequest(const std::string& modelId, const std::string& query) {
        ByteBuffer buffer;
        buffer.writeUint8(0x1A);
        buffer.writeString(modelId);
        buffer.writeString(query);
        return buffer.data();
    }
    
    static std::vector<uint8_t> packModelResponse(const std::string& modelId, const std::string& response) {
        ByteBuffer buffer;
        buffer.writeUint8(0x1B);
        buffer.writeString(modelId);
        buffer.writeString(response);
        return buffer.data();
    }
};


class MessageUnpacker {
public:
    static uint8_t getMessageType(const std::vector<uint8_t>& data) {
        if (data.empty()) return 0;
        return data[0];
    }
    
    static bool unpackHandshake(const std::vector<uint8_t>& data, uint32_t& version,
                                 std::string& nodeId, uint16_t& port, uint64_t& timestamp) {
        try {
            ByteBuffer buffer(data);
            if (buffer.readUint8() != 0x10) return false;
            version = buffer.readUint32();
            nodeId = buffer.readString();
            port = buffer.readUint16();
            timestamp = buffer.readUint64();
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static bool unpackPing(const std::vector<uint8_t>& data, uint64_t& nonce) {
        try {
            ByteBuffer buffer(data);
            if (buffer.readUint8() != 0x11) return false;
            nonce = buffer.readUint64();
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static bool unpackPong(const std::vector<uint8_t>& data, uint64_t& nonce) {
        try {
            ByteBuffer buffer(data);
            if (buffer.readUint8() != 0x12) return false;
            nonce = buffer.readUint64();
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static bool unpackPeers(const std::vector<uint8_t>& data,
                            std::vector<std::pair<std::string, uint16_t>>& peers) {
        try {
            ByteBuffer buffer(data);
            if (buffer.readUint8() != 0x14) return false;
            
            uint64_t count = buffer.readVarInt();
            peers.clear();
            peers.reserve(count);
            
            for (uint64_t i = 0; i < count; i++) {
                std::string addr = buffer.readString();
                uint16_t port = buffer.readUint16();
                peers.push_back({addr, port});
            }
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static bool unpackGetBlocks(const std::vector<uint8_t>& data, uint64_t& startHeight,
                                 uint32_t& count) {
        try {
            ByteBuffer buffer(data);
            if (buffer.readUint8() != 0x15) return false;
            startHeight = buffer.readUint64();
            count = buffer.readUint32();
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static bool unpackBlocks(const std::vector<uint8_t>& data,
                              std::vector<std::vector<uint8_t>>& blocks) {
        try {
            ByteBuffer buffer(data);
            if (buffer.readUint8() != 0x16) return false;
            
            uint64_t count = buffer.readVarInt();
            blocks.clear();
            blocks.reserve(count);
            
            for (uint64_t i = 0; i < count; i++) {
                blocks.push_back(buffer.readBytes());
            }
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static bool unpackTransaction(const std::vector<uint8_t>& data, std::vector<uint8_t>& tx) {
        try {
            ByteBuffer buffer(data);
            if (buffer.readUint8() != 0x17) return false;
            tx = buffer.readBytes();
            return true;
        } catch (...) {
            return false;
        }
    }
    
    static bool unpackKnowledge(const std::vector<uint8_t>& data, std::vector<uint8_t>& knowledge) {
        try {
            ByteBuffer buffer(data);
            if (buffer.readUint8() != 0x18) return false;
            knowledge = buffer.readBytes();
            return true;
        } catch (...) {
            return false;
        }
    }
};

}
}