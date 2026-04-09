#pragma once

#include <cstring>
#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>

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

}
}
