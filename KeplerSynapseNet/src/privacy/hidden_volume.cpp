#include "privacy/privacy.h"
#include "crypto/crypto.h"
#include <mutex>
#include <fstream>
#include <random>
#include <cstring>
#include <algorithm>
#include <openssl/evp.h>

namespace synapse {
namespace privacy {

struct HiddenVolume::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    std::string currentPath;
    std::vector<uint8_t> outerKey;
    std::vector<uint8_t> hiddenKey;
    size_t outerOffset = 0;
    size_t hiddenOffset = 0;
    size_t volumeSize = 0;
    bool mounted = false;
    bool hiddenMounted = false;
    
    Impl() : rng(std::random_device{}()) {}
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
    
    std::vector<uint8_t> deriveKey(const std::string& password, const std::vector<uint8_t>& salt) {
        std::vector<uint8_t> out(32);
        if (!PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()), 100000, EVP_sha256(), static_cast<int>(out.size()), out.data())) {
            std::fill(out.begin(), out.end(), 0);
        }
        return out;
    }
    
    std::vector<uint8_t> xorKeystream(const std::vector<uint8_t>& data,
                                      const std::vector<uint8_t>& key,
                                      uint64_t offset) {
        if (key.empty()) return {};
        std::vector<uint8_t> result(data.size());
        size_t produced = 0;
        uint64_t block = offset / 32;
        size_t blockOffset = static_cast<size_t>(offset % 32);
        while (produced < data.size()) {
            std::vector<uint8_t> input;
            input.reserve(key.size() + 8);
            input.insert(input.end(), key.begin(), key.end());
            for (int i = 0; i < 8; i++) {
                input.push_back(static_cast<uint8_t>((block >> (i * 8)) & 0xff));
            }
            auto hash = crypto::sha256(input.data(), input.size());
            for (size_t i = blockOffset; i < hash.size() && produced < data.size(); i++) {
                result[produced] = data[produced] ^ hash[i];
                produced++;
            }
            block++;
            blockOffset = 0;
        }
        return result;
    }
};

HiddenVolume::HiddenVolume() : impl_(std::make_unique<Impl>()) {}
HiddenVolume::~HiddenVolume() { unmount(); }

bool HiddenVolume::create(const std::string& path, size_t size,
                          const std::string& outerPassword,
                          const std::string& hiddenPassword) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> container(size);
    impl_->fillRandom(container.data(), size);
    
    std::vector<uint8_t> outerSalt(32);
    std::vector<uint8_t> hiddenSalt(32);
    impl_->fillRandom(outerSalt.data(), 32);
    impl_->fillRandom(hiddenSalt.data(), 32);
    
    std::memcpy(container.data(), outerSalt.data(), 32);
    std::memcpy(container.data() + size / 2, hiddenSalt.data(), 32);
    
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    
    file.write(reinterpret_cast<char*>(container.data()), size);
    return file.good();
}

bool HiddenVolume::mountOuter(const std::string& path, const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    impl_->volumeSize = file.tellg();
    file.seekg(0);
    
    std::vector<uint8_t> salt(32);
    file.read(reinterpret_cast<char*>(salt.data()), 32);
    
    impl_->outerKey = impl_->deriveKey(password, salt);
    impl_->currentPath = path;
    impl_->outerOffset = 32;
    impl_->mounted = true;
    impl_->hiddenMounted = false;
    
    return true;
}

bool HiddenVolume::mountHidden(const std::string& path, const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    impl_->volumeSize = file.tellg();
    file.seekg(impl_->volumeSize / 2);
    
    std::vector<uint8_t> salt(32);
    file.read(reinterpret_cast<char*>(salt.data()), 32);
    
    impl_->hiddenKey = impl_->deriveKey(password, salt);
    impl_->currentPath = path;
    impl_->hiddenOffset = impl_->volumeSize / 2 + 32;
    impl_->mounted = true;
    impl_->hiddenMounted = true;
    
    return true;
}

void HiddenVolume::unmount() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->outerKey.clear();
    impl_->hiddenKey.clear();
    impl_->mounted = false;
    impl_->hiddenMounted = false;
}

std::vector<uint8_t> HiddenVolume::read(size_t offset, size_t size, bool hidden) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->mounted) return {};
    
    std::ifstream file(impl_->currentPath, std::ios::binary);
    if (!file.is_open()) return {};
    
    size_t baseOffset = hidden ? impl_->hiddenOffset : impl_->outerOffset;
    file.seekg(baseOffset + offset);
    
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    const auto& key = hidden ? impl_->hiddenKey : impl_->outerKey;
    return impl_->xorKeystream(data, key, static_cast<uint64_t>(baseOffset + offset));
}

bool HiddenVolume::write(size_t offset, const std::vector<uint8_t>& data, bool hidden) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->mounted) return false;
    
    const auto& key = hidden ? impl_->hiddenKey : impl_->outerKey;
    auto encrypted = impl_->xorKeystream(data, key, static_cast<uint64_t>((hidden ? impl_->hiddenOffset : impl_->outerOffset) + offset));
    
    std::fstream file(impl_->currentPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) return false;
    
    size_t baseOffset = hidden ? impl_->hiddenOffset : impl_->outerOffset;
    file.seekp(baseOffset + offset);
    file.write(reinterpret_cast<char*>(encrypted.data()), encrypted.size());
    
    return file.good();
}

}
}
