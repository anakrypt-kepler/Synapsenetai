#include "database/database.h"
#include "crypto/crypto.h"
#include "crypto/keys.h"
#include <cstring>
#include <fstream>
#include <mutex>
#include <map>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace synapse {
namespace database {

// legacy environment helpers removed — legacy XOR fallback is no longer supported

struct WalletRecord {
    std::string address;
    std::vector<uint8_t> encryptedPrivateKey;
    std::vector<uint8_t> publicKey;
    std::vector<uint8_t> salt;
    std::vector<uint8_t> iv;
    uint64_t balance;
    uint64_t stakedBalance;
    uint64_t pendingBalance;
    uint64_t createdAt;
    uint64_t lastActivity;
    std::string label;
    bool isDefault;
    bool isWatchOnly;
    int keyDerivationIterations;
};

struct TransactionRecord {
    std::string txId;
    std::string fromAddress;
    std::string toAddress;
    uint64_t amount;
    uint64_t fee;
    uint64_t timestamp;
    int confirmations;
    std::string status;
    std::string memo;
};

struct AddressBookEntry {
    std::string address;
    std::string label;
    uint64_t addedAt;
    uint64_t lastUsed;
    int useCount;
};

struct WalletDB::Impl {
    std::string dbPath;
    std::map<std::string, WalletRecord> wallets;
    std::map<std::string, std::vector<TransactionRecord>> transactions;
    std::map<std::string, AddressBookEntry> addressBook;
    mutable std::mutex mtx;
    std::vector<uint8_t> masterKey;
    bool isLocked;
    int lockTimeout;
    std::chrono::steady_clock::time_point lastAccess;
    
    bool deriveKey(const std::string& password, const std::vector<uint8_t>& salt,
                   std::vector<uint8_t>& key, int iterations);
    bool encryptData(const std::vector<uint8_t>& plaintext, const std::vector<uint8_t>& key,
                     std::vector<uint8_t>& ivOut, std::vector<uint8_t>& ciphertext);
    bool decryptData(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key,
                     const std::vector<uint8_t>& iv, std::vector<uint8_t>& plaintext);
    std::string generateTxId();
    bool saveToFile();
    bool loadFromFile();
    void checkAutoLock();
};

bool WalletDB::Impl::deriveKey(const std::string& password, const std::vector<uint8_t>& salt,
                                std::vector<uint8_t>& key, int iterations) {
    auto derived = crypto::deriveKey(password, salt);
    key.assign(derived.begin(), derived.end());
    return true;
}

bool WalletDB::Impl::encryptData(const std::vector<uint8_t>& plaintext, const std::vector<uint8_t>& key,
                                  std::vector<uint8_t>& ivOut, std::vector<uint8_t>& ciphertext) {
    std::array<uint8_t, crypto::AES_KEY_SIZE> keyArr{};
    std::memcpy(keyArr.data(), key.data(), std::min(key.size(), keyArr.size()));
    ciphertext = crypto::encryptAES(plaintext, keyArr);
    if (ciphertext.empty()) return false;
    const size_t GCM_IV_SIZE = 12;
    if (ciphertext.size() < GCM_IV_SIZE) return false;
    ivOut.assign(ciphertext.begin(), ciphertext.begin() + GCM_IV_SIZE);
    return true;
}

bool WalletDB::Impl::decryptData(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key,
                                  const std::vector<uint8_t>& iv, std::vector<uint8_t>& plaintext) {
    (void)iv;
    plaintext.clear();
    std::array<uint8_t, crypto::AES_KEY_SIZE> keyArr{};
    std::memcpy(keyArr.data(), key.data(), std::min(key.size(), keyArr.size()));
    plaintext = crypto::decryptAES(ciphertext, keyArr);
    return !plaintext.empty();
}

std::string WalletDB::Impl::generateTxId() {
    auto random = crypto::randomBytes(32);
    auto hash = crypto::sha256(random.data(), random.size());
    return crypto::toHex(hash);
}

bool WalletDB::Impl::saveToFile() {
    std::ofstream file(dbPath, std::ios::binary);
    if (!file) return false;
    
    uint32_t walletCount = wallets.size();
    file.write(reinterpret_cast<char*>(&walletCount), sizeof(walletCount));
    
    for (const auto& pair : wallets) {
        const WalletRecord& w = pair.second;
        
        uint32_t addrLen = w.address.length();
        file.write(reinterpret_cast<char*>(&addrLen), sizeof(addrLen));
        file.write(w.address.c_str(), addrLen);
        
        uint32_t encKeyLen = w.encryptedPrivateKey.size();
        file.write(reinterpret_cast<char*>(&encKeyLen), sizeof(encKeyLen));
        file.write(reinterpret_cast<const char*>(w.encryptedPrivateKey.data()), encKeyLen);
        
        uint32_t pubKeyLen = w.publicKey.size();
        file.write(reinterpret_cast<char*>(&pubKeyLen), sizeof(pubKeyLen));
        file.write(reinterpret_cast<const char*>(w.publicKey.data()), pubKeyLen);
        
        uint32_t saltLen = w.salt.size();
        file.write(reinterpret_cast<char*>(&saltLen), sizeof(saltLen));
        file.write(reinterpret_cast<const char*>(w.salt.data()), saltLen);
        
        uint32_t ivLen = w.iv.size();
        file.write(reinterpret_cast<char*>(&ivLen), sizeof(ivLen));
        file.write(reinterpret_cast<const char*>(w.iv.data()), ivLen);
        
        file.write(reinterpret_cast<const char*>(&w.balance), sizeof(w.balance));
        file.write(reinterpret_cast<const char*>(&w.stakedBalance), sizeof(w.stakedBalance));
        file.write(reinterpret_cast<const char*>(&w.pendingBalance), sizeof(w.pendingBalance));
        file.write(reinterpret_cast<const char*>(&w.createdAt), sizeof(w.createdAt));
        file.write(reinterpret_cast<const char*>(&w.lastActivity), sizeof(w.lastActivity));
        
        uint32_t labelLen = w.label.length();
        file.write(reinterpret_cast<char*>(&labelLen), sizeof(labelLen));
        file.write(w.label.c_str(), labelLen);
        
        file.write(reinterpret_cast<const char*>(&w.isDefault), sizeof(w.isDefault));
        file.write(reinterpret_cast<const char*>(&w.isWatchOnly), sizeof(w.isWatchOnly));
        file.write(reinterpret_cast<const char*>(&w.keyDerivationIterations), sizeof(w.keyDerivationIterations));
    }
    
    return true;
}

bool WalletDB::Impl::loadFromFile() {
    std::ifstream file(dbPath, std::ios::binary);
    if (!file) return true;
    
    uint32_t walletCount = 0;
    file.read(reinterpret_cast<char*>(&walletCount), sizeof(walletCount));
    
    for (uint32_t i = 0; i < walletCount; i++) {
        WalletRecord w;
        
        uint32_t addrLen = 0;
        file.read(reinterpret_cast<char*>(&addrLen), sizeof(addrLen));
        w.address.resize(addrLen);
        file.read(&w.address[0], addrLen);
        
        uint32_t encKeyLen = 0;
        file.read(reinterpret_cast<char*>(&encKeyLen), sizeof(encKeyLen));
        w.encryptedPrivateKey.resize(encKeyLen);
        file.read(reinterpret_cast<char*>(w.encryptedPrivateKey.data()), encKeyLen);
        
        uint32_t pubKeyLen = 0;
        file.read(reinterpret_cast<char*>(&pubKeyLen), sizeof(pubKeyLen));
        w.publicKey.resize(pubKeyLen);
        file.read(reinterpret_cast<char*>(w.publicKey.data()), pubKeyLen);
        
        uint32_t saltLen = 0;
        file.read(reinterpret_cast<char*>(&saltLen), sizeof(saltLen));
        w.salt.resize(saltLen);
        file.read(reinterpret_cast<char*>(w.salt.data()), saltLen);
        
        uint32_t ivLen = 0;
        file.read(reinterpret_cast<char*>(&ivLen), sizeof(ivLen));
        w.iv.resize(ivLen);
        file.read(reinterpret_cast<char*>(w.iv.data()), ivLen);
        
        file.read(reinterpret_cast<char*>(&w.balance), sizeof(w.balance));
        file.read(reinterpret_cast<char*>(&w.stakedBalance), sizeof(w.stakedBalance));
        file.read(reinterpret_cast<char*>(&w.pendingBalance), sizeof(w.pendingBalance));
        file.read(reinterpret_cast<char*>(&w.createdAt), sizeof(w.createdAt));
        file.read(reinterpret_cast<char*>(&w.lastActivity), sizeof(w.lastActivity));
        
        uint32_t labelLen = 0;
        file.read(reinterpret_cast<char*>(&labelLen), sizeof(labelLen));
        w.label.resize(labelLen);
        file.read(&w.label[0], labelLen);
        
        file.read(reinterpret_cast<char*>(&w.isDefault), sizeof(w.isDefault));
        file.read(reinterpret_cast<char*>(&w.isWatchOnly), sizeof(w.isWatchOnly));
        file.read(reinterpret_cast<char*>(&w.keyDerivationIterations), sizeof(w.keyDerivationIterations));
        
        wallets[w.address] = w;
    }
    
    return true;
}

void WalletDB::Impl::checkAutoLock() {
    if (lockTimeout > 0 && !isLocked) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastAccess).count();
        if (elapsed >= lockTimeout) {
            isLocked = true;
            masterKey.clear();
        }
    }
}

WalletDB::WalletDB() : impl_(std::make_unique<Impl>()) {
    impl_->isLocked = true;
    impl_->lockTimeout = 300;
    impl_->lastAccess = std::chrono::steady_clock::now();
}

WalletDB::~WalletDB() {
    lock();
}

bool WalletDB::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->dbPath = path;
    return impl_->loadFromFile();
}

bool WalletDB::close() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    bool result = impl_->saveToFile();
    impl_->wallets.clear();
    impl_->transactions.clear();
    impl_->addressBook.clear();
    impl_->masterKey.clear();
    impl_->isLocked = true;
    return result;
}

bool WalletDB::unlock(const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto salt = crypto::randomBytes(16);
    auto derived = crypto::deriveKey(password, salt);
    impl_->masterKey.assign(derived.begin(), derived.end());
    impl_->isLocked = false;
    impl_->lastAccess = std::chrono::steady_clock::now();
    return true;
}

bool WalletDB::lock() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    crypto::secureZero(impl_->masterKey.data(), impl_->masterKey.size());
    impl_->masterKey.clear();
    impl_->isLocked = true;
    return true;
}

bool WalletDB::isLocked() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->checkAutoLock();
    return impl_->isLocked;
}

void WalletDB::setLockTimeout(int seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->lockTimeout = seconds;
}

bool WalletDB::createWallet(const std::string& address, const std::vector<uint8_t>& privateKey,
                             const std::vector<uint8_t>& publicKey, const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->wallets.find(address) != impl_->wallets.end()) return false;
    
    WalletRecord record;
    record.address = address;
    record.publicKey = publicKey;
    record.salt = crypto::randomBytes(16);
    record.iv = crypto::randomBytes(12);
    record.keyDerivationIterations = 100000;
    
    std::vector<uint8_t> key;
    impl_->deriveKey(password, record.salt, key, record.keyDerivationIterations);
    impl_->encryptData(privateKey, key, record.iv, record.encryptedPrivateKey);
    
    record.balance = 0;
    record.stakedBalance = 0;
    record.pendingBalance = 0;
    record.createdAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    record.lastActivity = record.createdAt;
    record.isDefault = impl_->wallets.empty();
    record.isWatchOnly = false;
    
    impl_->wallets[address] = record;
    return impl_->saveToFile();
}

bool WalletDB::importWatchOnly(const std::string& address, const std::vector<uint8_t>& publicKey,
                                const std::string& label) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->wallets.find(address) != impl_->wallets.end()) return false;
    
    WalletRecord record;
    record.address = address;
    record.publicKey = publicKey;
    record.balance = 0;
    record.stakedBalance = 0;
    record.pendingBalance = 0;
    record.createdAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    record.lastActivity = record.createdAt;
    record.label = label;
    record.isDefault = false;
    record.isWatchOnly = true;
    
    impl_->wallets[address] = record;
    return impl_->saveToFile();
}

bool WalletDB::deleteWallet(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    if (it == impl_->wallets.end()) return false;
    impl_->wallets.erase(it);
    impl_->transactions.erase(address);
    return impl_->saveToFile();
}

bool WalletDB::getPrivateKey(const std::string& address, const std::string& password,
                              std::vector<uint8_t>& privateKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    if (it == impl_->wallets.end() || it->second.isWatchOnly) return false;
    
    WalletRecord& record = it->second;
    std::vector<uint8_t> key;
    impl_->deriveKey(password, record.salt, key, record.keyDerivationIterations);
    if (!impl_->decryptData(record.encryptedPrivateKey, key, record.iv, privateKey)) return false;
    if (privateKey.size() != crypto::PRIVATE_KEY_SIZE) return false;

    crypto::Keys derived;
    if (!derived.fromPrivateKey(privateKey)) return false;
    if (!record.publicKey.empty() && derived.getPublicKey() != record.publicKey) return false;
    return true;
}

bool WalletDB::getPublicKey(const std::string& address, std::vector<uint8_t>& publicKey) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    if (it == impl_->wallets.end()) return false;
    publicKey = it->second.publicKey;
    return true;
}

uint64_t WalletDB::getBalance(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    return (it != impl_->wallets.end()) ? it->second.balance : 0;
}

uint64_t WalletDB::getStakedBalance(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    return (it != impl_->wallets.end()) ? it->second.stakedBalance : 0;
}

uint64_t WalletDB::getPendingBalance(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    return (it != impl_->wallets.end()) ? it->second.pendingBalance : 0;
}

bool WalletDB::updateBalance(const std::string& address, uint64_t balance) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    if (it == impl_->wallets.end()) return false;
    it->second.balance = balance;
    return impl_->saveToFile();
}

bool WalletDB::updateStakedBalance(const std::string& address, uint64_t stakedBalance) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    if (it == impl_->wallets.end()) return false;
    it->second.stakedBalance = stakedBalance;
    return impl_->saveToFile();
}

bool WalletDB::setLabel(const std::string& address, const std::string& label) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    if (it == impl_->wallets.end()) return false;
    it->second.label = label;
    return impl_->saveToFile();
}

std::string WalletDB::getLabel(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    return (it != impl_->wallets.end()) ? it->second.label : "";
}

bool WalletDB::setDefault(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    if (it == impl_->wallets.end()) return false;
    for (auto& pair : impl_->wallets) pair.second.isDefault = false;
    it->second.isDefault = true;
    return impl_->saveToFile();
}

std::string WalletDB::getDefault() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    for (const auto& pair : impl_->wallets) {
        if (pair.second.isDefault) return pair.first;
    }
    return impl_->wallets.empty() ? "" : impl_->wallets.begin()->first;
}

std::vector<std::string> WalletDB::listWallets() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> addresses;
    for (const auto& pair : impl_->wallets) addresses.push_back(pair.first);
    return addresses;
}

bool WalletDB::addTransaction(const std::string& address, const std::string& txId,
                               const std::string& fromAddress, const std::string& toAddress,
                               uint64_t amount, uint64_t fee, const std::string& memo) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    TransactionRecord tx;
    tx.txId = txId;
    tx.fromAddress = fromAddress;
    tx.toAddress = toAddress;
    tx.amount = amount;
    tx.fee = fee;
    tx.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    tx.confirmations = 0;
    tx.status = "pending";
    tx.memo = memo;
    impl_->transactions[address].push_back(tx);
    return true;
}

bool WalletDB::updateTransactionStatus(const std::string& address, const std::string& txId,
                                        const std::string& status, int confirmations) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->transactions.find(address);
    if (it == impl_->transactions.end()) return false;
    for (auto& tx : it->second) {
        if (tx.txId == txId) {
            tx.status = status;
            tx.confirmations = confirmations;
            return true;
        }
    }
    return false;
}

std::vector<std::pair<std::string, uint64_t>> WalletDB::getTransactionHistory(const std::string& address,
                                                                               int limit, int offset) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::pair<std::string, uint64_t>> history;
    auto it = impl_->transactions.find(address);
    if (it == impl_->transactions.end()) return history;
    
    int count = 0, skipped = 0;
    for (auto rit = it->second.rbegin(); rit != it->second.rend() && count < limit; ++rit) {
        if (skipped++ < offset) continue;
        history.push_back({rit->txId, rit->amount});
        count++;
    }
    return history;
}

bool WalletDB::addToAddressBook(const std::string& address, const std::string& label) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    AddressBookEntry entry;
    entry.address = address;
    entry.label = label;
    entry.addedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    impl_->addressBook[address] = entry;
    return true;
}

bool WalletDB::removeFromAddressBook(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->addressBook.erase(address) > 0;
}

std::vector<std::pair<std::string, std::string>> WalletDB::getAddressBook() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::pair<std::string, std::string>> entries;
    for (const auto& pair : impl_->addressBook) {
        entries.push_back({pair.first, pair.second.label});
    }
    return entries;
}

bool WalletDB::changePassword(const std::string& address, const std::string& oldPassword,
                               const std::string& newPassword) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    if (it == impl_->wallets.end() || it->second.isWatchOnly) return false;
    
    WalletRecord& record = it->second;
    std::vector<uint8_t> oldKey, privateKey;
    impl_->deriveKey(oldPassword, record.salt, oldKey, record.keyDerivationIterations);
    if (!impl_->decryptData(record.encryptedPrivateKey, oldKey, record.iv, privateKey)) return false;
    
    record.salt = crypto::randomBytes(16);
    record.iv = crypto::randomBytes(12);
    std::vector<uint8_t> newKey;
    impl_->deriveKey(newPassword, record.salt, newKey, record.keyDerivationIterations);
    impl_->encryptData(privateKey, newKey, record.iv, record.encryptedPrivateKey);
    crypto::secureZero(privateKey.data(), privateKey.size());
    
    return impl_->saveToFile();
}

bool WalletDB::backup(const std::string& path, const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::ifstream src(impl_->dbPath, std::ios::binary);
    if (!src) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    src.close();
    
    auto salt = crypto::randomBytes(16);
    auto key = crypto::deriveKey(password, salt);
    std::array<uint8_t, crypto::AES_KEY_SIZE> keyArr;
    std::memcpy(keyArr.data(), key.data(), keyArr.size());
    auto encrypted = crypto::encryptAES(data, keyArr);
    
    std::ofstream dst(path, std::ios::binary);
    if (!dst) return false;
    dst.write(reinterpret_cast<char*>(salt.data()), salt.size());
    uint32_t encLen = encrypted.size();
    dst.write(reinterpret_cast<char*>(&encLen), sizeof(encLen));
    dst.write(reinterpret_cast<char*>(encrypted.data()), encrypted.size());
    return true;
}

bool WalletDB::restore(const std::string& path, const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::ifstream src(path, std::ios::binary);
    if (!src) return false;
    
    std::vector<uint8_t> salt(16);
    src.read(reinterpret_cast<char*>(salt.data()), 16);
    uint32_t encLen = 0;
    src.read(reinterpret_cast<char*>(&encLen), sizeof(encLen));
    std::vector<uint8_t> encrypted(encLen);
    src.read(reinterpret_cast<char*>(encrypted.data()), encLen);
    src.close();
    
    auto key = crypto::deriveKey(password, salt);
    std::array<uint8_t, crypto::AES_KEY_SIZE> keyArr;
    std::memcpy(keyArr.data(), key.data(), keyArr.size());
    auto decrypted = crypto::decryptAES(encrypted, keyArr);
    if (decrypted.empty()) return false; // authentication failed or corrupt

    std::ofstream dst(impl_->dbPath, std::ios::binary);
    if (!dst) return false;
    dst.write(reinterpret_cast<char*>(decrypted.data()), decrypted.size());
    dst.close();
    
    return impl_->loadFromFile();
}

uint64_t WalletDB::getTotalBalance() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t total = 0;
    for (const auto& pair : impl_->wallets) total += pair.second.balance;
    return total;
}

uint64_t WalletDB::getTotalStaked() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t total = 0;
    for (const auto& pair : impl_->wallets) total += pair.second.stakedBalance;
    return total;
}

int WalletDB::getWalletCount() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->wallets.size();
}

bool WalletDB::walletExists(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->wallets.find(address) != impl_->wallets.end();
}

bool WalletDB::isWatchOnly(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->wallets.find(address);
    return (it != impl_->wallets.end()) ? it->second.isWatchOnly : false;
}

}
}
