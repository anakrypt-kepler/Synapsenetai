#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace synapse {
namespace database {

class WriteBatch {
public:
    WriteBatch();
    ~WriteBatch();
    void put(const std::string& key, const std::vector<uint8_t>& value);
    void del(const std::string& key);
    void clear();
private:
    friend class Database;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Iterator {
public:
    Iterator();
    ~Iterator();
    void seekToFirst();
    void seekToLast();
    void seek(const std::string& key);
    bool valid() const;
    void next();
    void prev();
    std::string key() const;
    std::vector<uint8_t> value() const;
private:
    friend class Database;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Database {
public:
    Database();
    ~Database();
    
    bool open(const std::string& path);
    void close();
    bool isOpen() const;
    
    bool put(const std::string& key, const std::vector<uint8_t>& value);
    bool put(const std::string& key, const std::string& value);
    std::vector<uint8_t> get(const std::string& key) const;
    std::string getString(const std::string& key) const;
    bool del(const std::string& key);
    bool exists(const std::string& key) const;
    
    bool write(WriteBatch& batch);
    std::unique_ptr<Iterator> newIterator() const;
    
    void forEach(const std::string& prefix, std::function<bool(const std::string&, const std::vector<uint8_t>&)> fn) const;
    std::vector<std::string> keys(const std::string& prefix = "") const;
    size_t count(const std::string& prefix = "") const;
    
    bool compact();
    bool integrityCheck(std::string* details = nullptr) const;
    std::string getPath() const;
    uint64_t size() const;
    
    bool backup(const std::string& destPath);
    bool restore(const std::string& srcPath);
    
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();
    
    std::vector<std::pair<std::string, std::vector<uint8_t>>> getRange(
        const std::string& startKey, const std::string& endKey, size_t limit = 0) const;
    bool clear();
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class WalletDB {
public:
    WalletDB();
    ~WalletDB();
    
    bool open(const std::string& path);
    bool close();
    bool unlock(const std::string& password);
    bool lock();
    bool isLocked() const;
    void setLockTimeout(int seconds);
    
    bool createWallet(const std::string& address, const std::vector<uint8_t>& privateKey,
                      const std::vector<uint8_t>& publicKey, const std::string& password);
    bool importWatchOnly(const std::string& address, const std::vector<uint8_t>& publicKey,
                         const std::string& label);
    bool deleteWallet(const std::string& address);
    
    bool getPrivateKey(const std::string& address, const std::string& password,
                       std::vector<uint8_t>& privateKey);
    bool getPublicKey(const std::string& address, std::vector<uint8_t>& publicKey);
    
    uint64_t getBalance(const std::string& address);
    uint64_t getStakedBalance(const std::string& address);
    uint64_t getPendingBalance(const std::string& address);
    bool updateBalance(const std::string& address, uint64_t balance);
    bool updateStakedBalance(const std::string& address, uint64_t stakedBalance);
    
    bool setLabel(const std::string& address, const std::string& label);
    std::string getLabel(const std::string& address);
    bool setDefault(const std::string& address);
    std::string getDefault();
    std::vector<std::string> listWallets();
    
    bool addTransaction(const std::string& address, const std::string& txId,
                        const std::string& fromAddress, const std::string& toAddress,
                        uint64_t amount, uint64_t fee, const std::string& memo);
    bool updateTransactionStatus(const std::string& address, const std::string& txId,
                                 const std::string& status, int confirmations);
    std::vector<std::pair<std::string, uint64_t>> getTransactionHistory(const std::string& address,
                                                                         int limit, int offset);
    
    bool addToAddressBook(const std::string& address, const std::string& label);
    bool removeFromAddressBook(const std::string& address);
    std::vector<std::pair<std::string, std::string>> getAddressBook();
    
    bool changePassword(const std::string& address, const std::string& oldPassword,
                        const std::string& newPassword);
    bool backup(const std::string& path, const std::string& password);
    bool restore(const std::string& path, const std::string& password);
    
    uint64_t getTotalBalance();
    uint64_t getTotalStaked();
    int getWalletCount();
    bool walletExists(const std::string& address);
    bool isWatchOnly(const std::string& address);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
