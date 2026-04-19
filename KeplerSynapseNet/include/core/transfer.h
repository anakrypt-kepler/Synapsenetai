#pragma once

#include "crypto/crypto.h"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace synapse {
namespace core {

enum class TxStatus : uint8_t {
    PENDING = 0,
    CONFIRMED = 1,
    REJECTED = 2
};

struct TxInput {
    crypto::Hash256 prevTxHash;
    uint32_t outputIndex;
    crypto::Signature signature;
    crypto::PublicKey pubKey;
    
    std::vector<uint8_t> serialize() const;
    static TxInput deserialize(const std::vector<uint8_t>& data);
};

struct TxOutput {
    uint64_t amount;
    std::string address;
    
    std::vector<uint8_t> serialize() const;
    static TxOutput deserialize(const std::vector<uint8_t>& data);
};

struct Transaction {
    crypto::Hash256 txid;
    uint64_t timestamp;
    std::vector<TxInput> inputs;
    std::vector<TxOutput> outputs;
    uint64_t fee;
    TxStatus status;
    
    std::vector<uint8_t> serialize() const;
    static Transaction deserialize(const std::vector<uint8_t>& data);
    crypto::Hash256 computeHash() const;
    uint64_t totalInput() const;
    uint64_t totalOutput() const;
    bool verify() const;
};

struct UTXO {
    crypto::Hash256 txHash;
    uint32_t outputIndex;
    uint64_t amount;
    std::string address;
    bool spent;
};

struct TransferStats {
    uint64_t totalTransactions;
    uint64_t pendingTransactions;
    uint64_t totalSupply;
    uint64_t totalVolume;
};

class TransferManager {
public:
    TransferManager();
    ~TransferManager();
    
    bool open(const std::string& dbPath);
    void close();
    
    Transaction createTransaction(
        const std::string& from,
        const std::string& to,
        uint64_t amount,
        uint64_t fee = 1
    );
    
    bool signTransaction(Transaction& tx, const crypto::PrivateKey& key);
    bool submitTransaction(const Transaction& tx);
    bool confirmTransaction(const crypto::Hash256& txid);
    bool rejectTransaction(const crypto::Hash256& txid);
    
    Transaction getTransaction(const crypto::Hash256& txid) const;
    std::vector<Transaction> getPending() const;
    std::vector<Transaction> getByAddress(const std::string& address, size_t limit = 100) const;
    std::vector<Transaction> getRecent(size_t limit = 50) const;
    
    uint64_t getBalance(const std::string& address) const;
    uint64_t getPendingBalance(const std::string& address) const;
    std::vector<UTXO> getUTXOs(const std::string& address) const;
    std::vector<UTXO> getUTXOs(const std::string& address, uint64_t minAmount) const;
    size_t getUTXOCount(const std::string& address) const;
    
    bool verifyTransaction(const Transaction& tx) const;
    bool hasSufficientBalance(const std::string& address, uint64_t amount) const;
    bool hasTransaction(const crypto::Hash256& txHash) const;

    bool verifyTransactionsInBlockOrder(const std::vector<Transaction>& txs) const;
    bool applyBlockTransactionsFromBlock(const std::vector<Transaction>& txs, uint64_t blockHeight, const crypto::Hash256& blockHash);
    bool rollbackBlockTransactions(uint64_t blockHeight, const crypto::Hash256& blockHash);

    bool creditRewardDeterministic(const std::string& address, const crypto::Hash256& rewardId, uint64_t amount);
    void onNewTransaction(std::function<void(const Transaction&)> callback);
    void onConfirmation(std::function<void(const crypto::Hash256&)> callback);
    
    uint64_t totalSupply() const;
    uint64_t circulatingSupply() const;
    size_t transactionCount() const;
    
    TransferStats getStats() const;
    std::vector<Transaction> getRecentTransactions(size_t count) const;
    uint64_t estimateFee(size_t txSize) const;
    void setMinFee(uint64_t feePerKB);
    void setMaxMempoolSize(size_t maxTx);
    void pruneMempool();
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
