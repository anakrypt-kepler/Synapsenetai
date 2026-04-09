#include "core/transfer.h"
#include "crypto/address.h"
#include "database/database.h"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <cctype>

namespace synapse {
namespace core {

static constexpr uint64_t TRANSFER_DB_VERSION = 2;

static void writeU64(std::vector<uint8_t>& out, uint64_t val) {
    for (int i = 0; i < 8; i++) out.push_back((val >> (i * 8)) & 0xff);
}

static uint64_t readU64(const uint8_t* p) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) val |= static_cast<uint64_t>(p[i]) << (i * 8);
    return val;
}

static void writeU32(std::vector<uint8_t>& out, uint32_t val) {
    for (int i = 0; i < 4; i++) out.push_back((val >> (i * 8)) & 0xff);
}

static uint32_t readU32(const uint8_t* p) {
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) val |= static_cast<uint32_t>(p[i]) << (i * 8);
    return val;
}

static void writeString(std::vector<uint8_t>& out, const std::string& s) {
    writeU32(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

static bool readStringSafe(const uint8_t*& p, const uint8_t* end, std::string& out) {
    if (static_cast<size_t>(end - p) < 4) return false;
    uint32_t len = readU32(p);
    p += 4;
    if (static_cast<size_t>(end - p) < static_cast<size_t>(len)) return false;
    out.assign(reinterpret_cast<const char*>(p), len);
    p += len;
    return true;
}

static bool parseUtxoKey(const std::string& key, crypto::Hash256& txHashOut, uint32_t& outputIndexOut) {
    size_t colon = key.find(':');
    if (colon == std::string::npos) return false;
    if (colon != 64) return false;
    std::string hex = key.substr(0, colon);
    for (unsigned char c : hex) {
        if (!std::isxdigit(c)) return false;
    }
    std::string idxStr = key.substr(colon + 1);
    if (idxStr.empty()) return false;
    for (unsigned char c : idxStr) {
        if (!std::isdigit(c)) return false;
    }
    unsigned long long idx = 0;
    try {
        idx = std::stoull(idxStr);
    } catch (...) {
        return false;
    }
    if (idx > 0xffffffffULL) return false;
    auto bytes = crypto::fromHex(hex);
    if (bytes.size() != txHashOut.size()) return false;
    std::memcpy(txHashOut.data(), bytes.data(), txHashOut.size());
    outputIndexOut = static_cast<uint32_t>(idx);
    return true;
}

static std::string addressFromPubKey(const crypto::PublicKey& pubKey) {
    return crypto::canonicalWalletAddressFromPublicKey(pubKey);
}

static std::array<std::string, 2> addressAliasesFromPubKey(const crypto::PublicKey& pubKey) {
    return crypto::walletAddressAliasesFromPublicKey(pubKey);
}

static bool isValidWalletAddress(const std::string& address) {
    return crypto::isSupportedWalletAddress(address);
}

static bool matchesAddressAlias(const std::array<std::string, 2>& aliases, const std::string& address) {
    return address == aliases[0] || address == aliases[1];
}

static bool safeAddU64(uint64_t a, uint64_t b, uint64_t& out) {
    if (UINT64_MAX - a < b) return false;
    out = a + b;
    return true;
}

static constexpr size_t MAX_TX_INPUTS = 1024;
static constexpr size_t MAX_TX_OUTPUTS = 1024;
static constexpr uint64_t MAX_TX_FUTURE_SKEW_SECONDS = 2 * 60 * 60;

static bool verifyTransactionLocked(
    const Transaction& tx,
    const std::unordered_map<std::string, std::vector<UTXO>>& utxoSet,
    const std::unordered_map<std::string, Transaction>& mempool,
    const std::vector<Transaction>& pending,
    uint64_t minFeePerKB);

std::vector<uint8_t> TxInput::serialize() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), prevTxHash.begin(), prevTxHash.end());
    writeU32(out, outputIndex);
    out.insert(out.end(), signature.begin(), signature.end());
    out.insert(out.end(), pubKey.begin(), pubKey.end());
    return out;
}

TxInput TxInput::deserialize(const std::vector<uint8_t>& data) {
    TxInput inp;
    if (data.size() < 32 + 4 + 64 + 33) return inp;
    const uint8_t* p = data.data();
    std::memcpy(inp.prevTxHash.data(), p, 32); p += 32;
    inp.outputIndex = readU32(p); p += 4;
    std::memcpy(inp.signature.data(), p, 64); p += 64;
    std::memcpy(inp.pubKey.data(), p, 33);
    return inp;
}

std::vector<uint8_t> TxOutput::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, amount);
    writeString(out, address);
    return out;
}

TxOutput TxOutput::deserialize(const std::vector<uint8_t>& data) {
    TxOutput outp;
    if (data.size() < 12) return outp;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    outp.amount = readU64(p);
    p += 8;
    if (!readStringSafe(p, end, outp.address)) return TxOutput{};
    if (p != end) return TxOutput{};
    return outp;
}

std::vector<uint8_t> Transaction::serialize() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), txid.begin(), txid.end());
    writeU64(out, timestamp);
    out.push_back(static_cast<uint8_t>(status));
    writeU64(out, fee);
    
    writeU32(out, inputs.size());
    for (const auto& inp : inputs) {
        auto inpData = inp.serialize();
        writeU32(out, inpData.size());
        out.insert(out.end(), inpData.begin(), inpData.end());
    }
    
    writeU32(out, outputs.size());
    for (const auto& outp : outputs) {
        auto outpData = outp.serialize();
        writeU32(out, outpData.size());
        out.insert(out.end(), outpData.begin(), outpData.end());
    }
    
    return out;
}

Transaction Transaction::deserialize(const std::vector<uint8_t>& data) {
    Transaction tx;
    if (data.size() < 32 + 8 + 1 + 8 + 4) return tx;

    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    auto need = [&](size_t n) -> bool {
        return static_cast<size_t>(end - p) >= n;
    };

    if (!need(32)) return Transaction{};
    std::memcpy(tx.txid.data(), p, 32);
    p += 32;

    if (!need(8 + 1 + 8 + 4)) return Transaction{};
    tx.timestamp = readU64(p);
    p += 8;
    tx.status = static_cast<TxStatus>(*p++);
    tx.fee = readU64(p);
    p += 8;

    uint32_t inputCount = readU32(p);
    p += 4;
    if (inputCount > MAX_TX_INPUTS) return Transaction{};
    tx.inputs.reserve(inputCount);
    for (uint32_t i = 0; i < inputCount; i++) {
        if (!need(4)) return Transaction{};
        uint32_t inpLen = readU32(p);
        p += 4;
        if (inpLen != 32 + 4 + 64 + 33) return Transaction{};
        if (!need(inpLen)) return Transaction{};
        std::vector<uint8_t> inpData(p, p + inpLen);
        p += inpLen;
        tx.inputs.push_back(TxInput::deserialize(inpData));
    }

    if (!need(4)) return Transaction{};
    uint32_t outputCount = readU32(p);
    p += 4;
    if (outputCount > MAX_TX_OUTPUTS) return Transaction{};
    tx.outputs.reserve(outputCount);
    for (uint32_t i = 0; i < outputCount; i++) {
        if (!need(4)) return Transaction{};
        uint32_t outpLen = readU32(p);
        p += 4;
        if (outpLen < 12 || outpLen > 4096) return Transaction{};
        if (!need(outpLen)) return Transaction{};
        std::vector<uint8_t> outpData(p, p + outpLen);
        p += outpLen;
        TxOutput outp = TxOutput::deserialize(outpData);
        if (outp.address.empty()) return Transaction{};
        tx.outputs.push_back(std::move(outp));
    }

    if (p != end) return Transaction{};
    return tx;
}

crypto::Hash256 Transaction::computeHash() const {
    std::vector<uint8_t> buf;
    writeU64(buf, timestamp);
    writeU64(buf, fee);
    for (const auto& inp : inputs) {
        buf.insert(buf.end(), inp.prevTxHash.begin(), inp.prevTxHash.end());
        writeU32(buf, inp.outputIndex);
    }
    for (const auto& outp : outputs) {
        writeU64(buf, outp.amount);
        buf.insert(buf.end(), outp.address.begin(), outp.address.end());
    }
    return crypto::doubleSha256(buf.data(), buf.size());
}

uint64_t Transaction::totalInput() const {
    return 0;
}

uint64_t Transaction::totalOutput() const {
    uint64_t total = 0;
    for (const auto& outp : outputs) {
        if (UINT64_MAX - total < outp.amount) return UINT64_MAX;
        total += outp.amount;
    }
    return total;
}

bool Transaction::verify() const {
    for (const auto& inp : inputs) {
        crypto::Hash256 sigHash = computeHash();
        if (!crypto::verify(sigHash, inp.signature, inp.pubKey)) {
            return false;
        }
    }
    return true;
}

struct TransferManager::Impl {
    database::Database db;
    std::unordered_map<std::string, std::vector<UTXO>> utxoSet;
    std::vector<Transaction> pending;
    std::vector<Transaction> confirmed;
    std::unordered_map<std::string, Transaction> mempool;
    std::vector<Transaction> recentTxs;
    uint64_t txCounter = 0;
    uint64_t totalSupply_ = 0;
    mutable std::mutex mtx;
    std::function<void(const Transaction&)> newTxCallback;
    std::function<void(const crypto::Hash256&)> confirmCallback;
    struct {
        uint64_t minFeePerKB = 1000;
        size_t maxMempoolSize = 10000;
        uint64_t mempoolExpiry = 86400;
    } config;
};

TransferManager::TransferManager() : impl_(std::make_unique<Impl>()) {}
TransferManager::~TransferManager() { close(); }

bool TransferManager::open(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.open(dbPath)) return false;

    uint64_t dbVersion = 0;
    auto verData = impl_->db.get("meta:transfer_db_version");
    if (!verData.empty()) dbVersion = readU64(verData.data());
    if (dbVersion != TRANSFER_DB_VERSION) {
        impl_->db.clear();
        std::vector<uint8_t> vbuf;
        writeU64(vbuf, TRANSFER_DB_VERSION);
        impl_->db.put("meta:transfer_db_version", vbuf);
        impl_->txCounter = 0;
        impl_->totalSupply_ = 0;
        std::vector<uint8_t> z;
        writeU64(z, 0);
        impl_->db.put("meta:txCounter", z);
        impl_->db.put("meta:totalSupply", z);
        impl_->utxoSet.clear();
        impl_->pending.clear();
        impl_->confirmed.clear();
        impl_->mempool.clear();
        impl_->recentTxs.clear();
        return true;
    }

    auto counterData = impl_->db.get("meta:txCounter");
    if (!counterData.empty()) {
        impl_->txCounter = readU64(counterData.data());
    }
    
    auto supplyData = impl_->db.get("meta:totalSupply");
    if (!supplyData.empty()) {
        impl_->totalSupply_ = readU64(supplyData.data());
    }

    impl_->pending.clear();
    impl_->confirmed.clear();
    impl_->mempool.clear();
    impl_->recentTxs.clear();

    impl_->utxoSet.clear();
    impl_->db.forEach("", [this](const std::string& key, const std::vector<uint8_t>& value) {
        if (key.rfind("meta:", 0) == 0) return true;
        if (key.rfind("tx:", 0) == 0) return true;

        crypto::Hash256 txHash{};
        uint32_t outIdx = 0;
        if (!parseUtxoKey(key, txHash, outIdx)) return true;
        if (value.size() < 12) return true;
        const uint8_t* p = value.data();
        uint64_t amount = readU64(p); p += 8;
        uint32_t addrLen = readU32(p); p += 4;
        if (amount == 0) return true;
        if (value.size() < 12 + static_cast<size_t>(addrLen)) return true;
        std::string address(reinterpret_cast<const char*>(p), addrLen);
        if (!isValidWalletAddress(address)) return true;

        UTXO utxo;
        utxo.txHash = txHash;
        utxo.outputIndex = outIdx;
        utxo.amount = amount;
        utxo.address = address;
        utxo.spent = false;
        impl_->utxoSet[address].push_back(utxo);
        return true;
    });

    uint64_t rebuiltSupply = 0;
    bool supplyOverflow = false;
    for (const auto& [_, utxos] : impl_->utxoSet) {
        for (const auto& utxo : utxos) {
            if (!safeAddU64(rebuiltSupply, utxo.amount, rebuiltSupply)) {
                supplyOverflow = true;
                break;
            }
        }
        if (supplyOverflow) break;
    }
    if (supplyOverflow) return false;
    if (impl_->totalSupply_ != rebuiltSupply) {
        impl_->totalSupply_ = rebuiltSupply;
        std::vector<uint8_t> supplyBuf;
        writeU64(supplyBuf, impl_->totalSupply_);
        impl_->db.put("meta:totalSupply", supplyBuf);
    }

    std::vector<Transaction> loadedTxs;
    impl_->db.forEach("tx:", [&loadedTxs](const std::string& key, const std::vector<uint8_t>& value) {
        (void)key;
        Transaction tx = Transaction::deserialize(value);
        if (tx.txid == crypto::Hash256{}) return true;
        if (tx.computeHash() != tx.txid) return true;
        if (tx.status != TxStatus::PENDING &&
            tx.status != TxStatus::CONFIRMED &&
            tx.status != TxStatus::REJECTED) {
            return true;
        }
        loadedTxs.push_back(std::move(tx));
        return true;
    });

    if (!loadedTxs.empty()) {
        std::sort(loadedTxs.begin(), loadedTxs.end(), [](const Transaction& a, const Transaction& b) {
            if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
            return crypto::toHex(a.txid) < crypto::toHex(b.txid);
        });

        for (const auto& tx : loadedTxs) {
            if (tx.status == TxStatus::CONFIRMED) impl_->confirmed.push_back(tx);
        }

        if (loadedTxs.size() > 1000) {
            impl_->recentTxs.assign(loadedTxs.end() - 1000, loadedTxs.end());
        } else {
            impl_->recentTxs = std::move(loadedTxs);
        }
    }

    std::unordered_set<std::string> utxoTxIds;
    for (const auto& [_, utxos] : impl_->utxoSet) {
        for (const auto& utxo : utxos) {
            utxoTxIds.insert(crypto::toHex(utxo.txHash));
        }
    }

    uint64_t minCounter = static_cast<uint64_t>(impl_->confirmed.size() + impl_->pending.size());
    minCounter = std::max<uint64_t>(minCounter, static_cast<uint64_t>(utxoTxIds.size()));
    if (impl_->txCounter < minCounter) {
        impl_->txCounter = minCounter;
        std::vector<uint8_t> counterBuf;
        writeU64(counterBuf, impl_->txCounter);
        impl_->db.put("meta:txCounter", counterBuf);
    }
    
    return true;
}

void TransferManager::close() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->db.close();
}

Transaction TransferManager::createTransaction(
    const std::string& from,
    const std::string& to,
    uint64_t amount,
    uint64_t fee
) {
    Transaction tx;
    tx.timestamp = std::time(nullptr);
    tx.fee = fee;
    tx.status = TxStatus::PENDING;
    
    auto utxos = getUTXOs(from);
    uint64_t collected = 0;
    for (const auto& utxo : utxos) {
        if (collected >= amount + fee) break;
        TxInput inp;
        inp.prevTxHash = utxo.txHash;
        inp.outputIndex = utxo.outputIndex;
        tx.inputs.push_back(inp);
        collected += utxo.amount;
    }
    
    TxOutput toOutput;
    toOutput.amount = amount;
    toOutput.address = to;
    tx.outputs.push_back(toOutput);
    
    if (collected > amount + fee) {
        TxOutput changeOutput;
        changeOutput.amount = collected - amount - fee;
        changeOutput.address = from;
        tx.outputs.push_back(changeOutput);
    }
    
    tx.txid = tx.computeHash();
    return tx;
}

bool TransferManager::signTransaction(Transaction& tx, const crypto::PrivateKey& key) {
    crypto::Hash256 sigHash = tx.computeHash();
    crypto::PublicKey pubKey = crypto::derivePublicKey(key);
    crypto::Signature sig = crypto::sign(sigHash, key);
    
    for (auto& inp : tx.inputs) {
        inp.signature = sig;
        inp.pubKey = pubKey;
    }
    
    tx.txid = tx.computeHash();
    return true;
}

bool TransferManager::submitTransaction(const Transaction& tx) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    const uint64_t now = std::time(nullptr);
    if (!impl_->mempool.empty()) {
        std::vector<std::pair<std::string, Transaction>> expired;
        expired.reserve(8);
        for (const auto& it : impl_->mempool) {
            const auto& mtx = it.second;
            if (now > mtx.timestamp && (now - mtx.timestamp) > impl_->config.mempoolExpiry) {
                expired.emplace_back(it.first, mtx);
            }
        }
        if (!expired.empty()) {
            for (const auto& e : expired) {
                impl_->mempool.erase(e.first);
                auto pit = std::find_if(impl_->pending.begin(), impl_->pending.end(),
                                        [&](const Transaction& p) { return p.txid == e.second.txid; });
                if (pit != impl_->pending.end()) {
                    Transaction rej = *pit;
                    rej.status = TxStatus::REJECTED;
                    impl_->db.put("tx:" + e.first, rej.serialize());
                    impl_->pending.erase(pit);
                }
            }
        }
    }

    const std::string hex = crypto::toHex(tx.txid);
    if (impl_->mempool.find(hex) != impl_->mempool.end()) return false;
    if (!verifyTransactionLocked(tx, impl_->utxoSet, impl_->mempool, impl_->pending, impl_->config.minFeePerKB)) return false;

    if (impl_->mempool.size() >= impl_->config.maxMempoolSize) {
        auto feeRateLess = [](const Transaction& lhs, const Transaction& rhs) -> bool {
            const size_t lhsSize = std::max<size_t>(1, lhs.serialize().size());
            const size_t rhsSize = std::max<size_t>(1, rhs.serialize().size());
            unsigned __int128 lhsRate = static_cast<unsigned __int128>(lhs.fee) * static_cast<unsigned __int128>(rhsSize);
            unsigned __int128 rhsRate = static_cast<unsigned __int128>(rhs.fee) * static_cast<unsigned __int128>(lhsSize);
            if (lhsRate != rhsRate) return lhsRate < rhsRate;
            if (lhs.fee != rhs.fee) return lhs.fee < rhs.fee;
            if (lhs.timestamp != rhs.timestamp) return lhs.timestamp < rhs.timestamp;
            return crypto::toHex(lhs.txid) < crypto::toHex(rhs.txid);
        };
        auto hasStrictlyHigherFeeRate = [](const Transaction& lhs, const Transaction& rhs) -> bool {
            const size_t lhsSize = std::max<size_t>(1, lhs.serialize().size());
            const size_t rhsSize = std::max<size_t>(1, rhs.serialize().size());
            unsigned __int128 lhsRate = static_cast<unsigned __int128>(lhs.fee) * static_cast<unsigned __int128>(rhsSize);
            unsigned __int128 rhsRate = static_cast<unsigned __int128>(rhs.fee) * static_cast<unsigned __int128>(lhsSize);
            return lhsRate > rhsRate;
        };

        auto worstIt = impl_->mempool.begin();
        for (auto it = impl_->mempool.begin(); it != impl_->mempool.end(); ++it) {
            const auto& a = it->second;
            const auto& b = worstIt->second;
            if (feeRateLess(a, b)) worstIt = it;
        }

        if (!hasStrictlyHigherFeeRate(tx, worstIt->second)) return false;

        Transaction dropped = worstIt->second;
        std::string dropHex = worstIt->first;
        impl_->mempool.erase(worstIt);
        auto pit = std::find_if(impl_->pending.begin(), impl_->pending.end(),
                                [&](const Transaction& p) { return p.txid == dropped.txid; });
        if (pit != impl_->pending.end()) {
            Transaction rej = *pit;
            rej.status = TxStatus::REJECTED;
            impl_->db.put("tx:" + dropHex, rej.serialize());
            impl_->pending.erase(pit);
        }
    }

    impl_->pending.push_back(tx);
    impl_->mempool[hex] = tx;
    impl_->txCounter++;
    
    std::string key = "tx:" + hex;
    impl_->db.put(key, tx.serialize());
    
    std::vector<uint8_t> counterBuf;
    writeU64(counterBuf, impl_->txCounter);
    impl_->db.put("meta:txCounter", counterBuf);
    
    if (impl_->newTxCallback) impl_->newTxCallback(tx);
    
    return true;
}

bool TransferManager::confirmTransaction(const crypto::Hash256& txid) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = std::find_if(impl_->pending.begin(), impl_->pending.end(),
        [&txid](const Transaction& tx) { return tx.txid == txid; });
    
    if (it == impl_->pending.end()) return false;
    
    Transaction tx = *it;
    tx.status = TxStatus::CONFIRMED;
    impl_->pending.erase(it);
    impl_->confirmed.push_back(tx);
    impl_->mempool.erase(crypto::toHex(tx.txid));
    
    for (const auto& inp : tx.inputs) {
        std::string utxoKey = crypto::toHex(inp.prevTxHash) + ":" + std::to_string(inp.outputIndex);
        impl_->db.del(utxoKey);
        for (auto& [addr, utxos] : impl_->utxoSet) {
            for (auto& utxo : utxos) {
                if (!utxo.spent && utxo.txHash == inp.prevTxHash && utxo.outputIndex == inp.outputIndex) {
                    utxo.spent = true;
                }
            }
        }
    }
    
    for (uint32_t i = 0; i < tx.outputs.size(); i++) {
        UTXO utxo;
        utxo.txHash = tx.txid;
        utxo.outputIndex = i;
        utxo.amount = tx.outputs[i].amount;
        utxo.address = tx.outputs[i].address;
        utxo.spent = false;
        impl_->utxoSet[utxo.address].push_back(utxo);
        
        std::string utxoKey = crypto::toHex(tx.txid) + ":" + std::to_string(i);
        std::vector<uint8_t> utxoData;
        writeU64(utxoData, utxo.amount);
        writeString(utxoData, utxo.address);
        impl_->db.put(utxoKey, utxoData);
    }
    
    std::string key = "tx:" + crypto::toHex(tx.txid);
    impl_->db.put(key, tx.serialize());
    impl_->recentTxs.push_back(tx);
    if (impl_->recentTxs.size() > 1000) {
        impl_->recentTxs.erase(impl_->recentTxs.begin());
    }
    
    if (impl_->confirmCallback) impl_->confirmCallback(txid);
    
    return true;
}

bool TransferManager::rejectTransaction(const crypto::Hash256& txid) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = std::find_if(impl_->pending.begin(), impl_->pending.end(),
        [&txid](const Transaction& tx) { return tx.txid == txid; });
    
    if (it == impl_->pending.end()) return false;
    
    it->status = TxStatus::REJECTED;
    std::string key = "tx:" + crypto::toHex(it->txid);
    impl_->db.put(key, it->serialize());
    impl_->mempool.erase(crypto::toHex(it->txid));
    impl_->pending.erase(it);
    
    return true;
}

Transaction TransferManager::getTransaction(const crypto::Hash256& txid) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string key = "tx:" + crypto::toHex(txid);
    auto data = impl_->db.get(key);
    if (data.empty()) return Transaction{};
    return Transaction::deserialize(data);
}

std::vector<Transaction> TransferManager::getPending() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->pending;
}

std::vector<Transaction> TransferManager::getByAddress(const std::string& address, size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<Transaction> result;
    for (const auto& tx : impl_->confirmed) {
        if (result.size() >= limit) break;
        for (const auto& outp : tx.outputs) {
            if (outp.address == address) {
                result.push_back(tx);
                break;
            }
        }
    }
    return result;
}

std::vector<Transaction> TransferManager::getRecent(size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<Transaction> result;
    size_t start = impl_->confirmed.size() > limit ? impl_->confirmed.size() - limit : 0;
    for (size_t i = impl_->confirmed.size(); i > start; i--) {
        result.push_back(impl_->confirmed[i - 1]);
    }
    return result;
}

uint64_t TransferManager::getBalance(const std::string& address) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t balance = 0;
    auto it = impl_->utxoSet.find(address);
    if (it != impl_->utxoSet.end()) {
        for (const auto& utxo : it->second) {
            if (!utxo.spent) balance += utxo.amount;
        }
    }
    return balance;
}

uint64_t TransferManager::getPendingBalance(const std::string& address) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t pending = 0;
    for (const auto& tx : impl_->pending) {
        for (const auto& outp : tx.outputs) {
            if (outp.address == address) pending += outp.amount;
        }
    }
    return pending;
}

std::vector<UTXO> TransferManager::getUTXOs(const std::string& address) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<UTXO> result;
    
    auto it = impl_->utxoSet.find(address);
    if (it != impl_->utxoSet.end()) {
        for (const auto& utxo : it->second) {
            if (!utxo.spent) result.push_back(utxo);
        }
    }
    // Return empty vector if address not found or no unspent UTXOs
    return result;
}

std::vector<UTXO> TransferManager::getUTXOs(const std::string& address, uint64_t minAmount) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<UTXO> result;
    auto it = impl_->utxoSet.find(address);
    if (it == impl_->utxoSet.end()) return result;
    
    for (const auto& utxo : it->second) {
        if (!utxo.spent && utxo.amount >= minAmount) {
            result.push_back(utxo);
        }
    }
    return result;
}

size_t TransferManager::getUTXOCount(const std::string& address) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->utxoSet.find(address);
    if (it == impl_->utxoSet.end()) return 0;
    
    size_t count = 0;
    for (const auto& utxo : it->second) {
        if (!utxo.spent) count++;
    }
    return count;
}

static bool verifyTransactionLocked(
    const Transaction& tx,
    const std::unordered_map<std::string, std::vector<UTXO>>& utxoSet,
    const std::unordered_map<std::string, Transaction>& mempool,
    const std::vector<Transaction>& pending,
    uint64_t minFeePerKB) {
    if (tx.inputs.empty() || tx.outputs.empty()) return false;
    if (tx.inputs.size() > MAX_TX_INPUTS || tx.outputs.size() > MAX_TX_OUTPUTS) return false;
    if (tx.timestamp == 0) return false;
    uint64_t nowTs = static_cast<uint64_t>(std::time(nullptr));
    if (tx.timestamp > nowTs + MAX_TX_FUTURE_SKEW_SECONDS) return false;
    if (tx.computeHash() != tx.txid) return false;
    if (!tx.verify()) return false;
    if (tx.fee < (tx.serialize().size() / 1000 + 1) * minFeePerKB) return false;

    std::unordered_set<std::string> seenInputs;
    std::unordered_set<std::string> pendingInputs;
    for (const auto& [_, pendingTx] : mempool) {
        for (const auto& inp : pendingTx.inputs) {
            pendingInputs.insert(crypto::toHex(inp.prevTxHash) + ":" + std::to_string(inp.outputIndex));
        }
    }
    for (const auto& pendingTx : pending) {
        for (const auto& inp : pendingTx.inputs) {
            pendingInputs.insert(crypto::toHex(inp.prevTxHash) + ":" + std::to_string(inp.outputIndex));
        }
    }

    uint64_t totalInput = 0;
    uint64_t totalOutput = 0;

    for (const auto& inp : tx.inputs) {
        std::string key = crypto::toHex(inp.prevTxHash) + ":" + std::to_string(inp.outputIndex);
        if (seenInputs.count(key) > 0) return false;
        if (pendingInputs.count(key) > 0) return false;
        seenInputs.insert(key);

        const auto ownerAliases = addressAliasesFromPubKey(inp.pubKey);
        if (ownerAliases[0].empty()) return false;

        bool found = false;
        for (const auto& ownerAddr : ownerAliases) {
            if (ownerAddr.empty()) continue;
            auto it = utxoSet.find(ownerAddr);
            if (it == utxoSet.end()) continue;
            for (const auto& utxo : it->second) {
                if (!utxo.spent && utxo.txHash == inp.prevTxHash && utxo.outputIndex == inp.outputIndex) {
                    if (UINT64_MAX - totalInput < utxo.amount) return false;
                    totalInput += utxo.amount;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) return false;
    }

    for (const auto& outp : tx.outputs) {
        if (outp.amount == 0) return false;
        if (!isValidWalletAddress(outp.address)) return false;
        if (!safeAddU64(totalOutput, outp.amount, totalOutput)) return false;
    }

    uint64_t required = 0;
    if (!safeAddU64(totalOutput, tx.fee, required)) return false;
    if (totalInput != required) return false;

    return true;
}

bool TransferManager::verifyTransaction(const Transaction& tx) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return verifyTransactionLocked(tx, impl_->utxoSet, impl_->mempool, impl_->pending, impl_->config.minFeePerKB);
}

namespace {

struct SimUtxo {
    uint64_t amount = 0;
    std::string address;
    bool spent = false;
};

static bool verifyTransactionForBlock(
    const synapse::core::Transaction& tx,
    const std::unordered_map<std::string, std::vector<synapse::core::UTXO>>& utxoSet,
    uint64_t minFeePerKB,
    std::unordered_set<std::string>& spentOutpoints,
    std::unordered_map<std::string, SimUtxo>& createdOutpoints) {

    if (tx.inputs.empty() || tx.outputs.empty()) return false;
    if (tx.computeHash() != tx.txid) return false;
    if (!tx.verify()) return false;
    if (tx.fee < (tx.serialize().size() / 1000 + 1) * minFeePerKB) return false;

    std::unordered_set<std::string> seenInputs;
    uint64_t totalInput = 0;
    uint64_t totalOutput = 0;

    for (const auto& inp : tx.inputs) {
        std::string key = synapse::crypto::toHex(inp.prevTxHash) + ":" + std::to_string(inp.outputIndex);
        if (seenInputs.count(key) > 0) return false;
        if (spentOutpoints.count(key) > 0) return false;
        seenInputs.insert(key);

        const auto ownerAliases = addressAliasesFromPubKey(inp.pubKey);
        if (ownerAliases[0].empty()) return false;

        uint64_t amount = 0;
        auto createdIt = createdOutpoints.find(key);
        if (createdIt != createdOutpoints.end()) {
            if (createdIt->second.spent) return false;
            if (!matchesAddressAlias(ownerAliases, createdIt->second.address)) return false;
            amount = createdIt->second.amount;
            createdIt->second.spent = true;
        } else {
            bool found = false;
            for (const auto& ownerAddr : ownerAliases) {
                if (ownerAddr.empty()) continue;
                auto it = utxoSet.find(ownerAddr);
                if (it == utxoSet.end()) continue;
                for (const auto& utxo : it->second) {
                    if (!utxo.spent && utxo.txHash == inp.prevTxHash && utxo.outputIndex == inp.outputIndex) {
                        amount = utxo.amount;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (!found) return false;
        }

        uint64_t tmp = 0;
        if (!safeAddU64(totalInput, amount, tmp)) return false;
        totalInput = tmp;
        spentOutpoints.insert(key);
    }

    for (uint32_t i = 0; i < tx.outputs.size(); ++i) {
        const auto& outp = tx.outputs[i];
        if (outp.amount == 0) return false;
        if (!isValidWalletAddress(outp.address)) return false;

        uint64_t tmp = 0;
        if (!safeAddU64(totalOutput, outp.amount, tmp)) return false;
        totalOutput = tmp;

        std::string outKey = synapse::crypto::toHex(tx.txid) + ":" + std::to_string(i);
        SimUtxo su;
        su.amount = outp.amount;
        su.address = outp.address;
        createdOutpoints[outKey] = std::move(su);
    }

    uint64_t required = 0;
    if (!safeAddU64(totalOutput, tx.fee, required)) return false;
    if (totalInput != required) return false;

    return true;
}

} // namespace

bool TransferManager::verifyTransactionsInBlockOrder(const std::vector<Transaction>& txs) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::unordered_set<std::string> spent;
    std::unordered_map<std::string, SimUtxo> created;
    spent.reserve(txs.size() * 8);
    created.reserve(txs.size() * 4);
    for (const auto& tx : txs) {
        if (!verifyTransactionForBlock(tx, impl_->utxoSet, impl_->config.minFeePerKB, spent, created)) return false;
    }
    return true;
}

bool TransferManager::applyBlockTransactionsFromBlock(
    const std::vector<Transaction>& txs,
    uint64_t blockHeight,
    const crypto::Hash256& blockHash
) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (txs.empty()) return true;

    const std::string undoKey = "blockundo:" + std::to_string(blockHeight);
    auto existingUndo = impl_->db.get(undoKey);
    if (!existingUndo.empty()) {
        if (existingUndo.size() >= 32) {
            crypto::Hash256 seen{};
            std::memcpy(seen.data(), existingUndo.data(), 32);
            if (blockHash != crypto::Hash256{} && seen == blockHash) return true;
        }
        return false;
    }

    std::unordered_set<std::string> spent;
    std::unordered_map<std::string, SimUtxo> created;
    if (!verifyTransactionForBlock(txs.front(), impl_->utxoSet, impl_->config.minFeePerKB, spent, created)) {
        return false;
    }
    for (size_t i = 1; i < txs.size(); ++i) {
        if (!verifyTransactionForBlock(txs[i], impl_->utxoSet, impl_->config.minFeePerKB, spent, created)) return false;
    }

    std::unordered_set<std::string> blockTxIds;
    blockTxIds.reserve(txs.size() * 2);
    for (const auto& tx : txs) blockTxIds.insert(synapse::crypto::toHex(tx.txid));

    std::unordered_set<std::string> blockSpentInputs;
    blockSpentInputs.reserve(spent.size() * 2);
    for (const auto& tx : txs) {
        for (const auto& inp : tx.inputs) {
            blockSpentInputs.insert(synapse::crypto::toHex(inp.prevTxHash) + ":" + std::to_string(inp.outputIndex));
        }
    }

    if (!impl_->db.beginTransaction()) return false;

    struct Snapshot {
        std::unordered_map<std::string, std::vector<UTXO>> utxoSet;
        std::vector<Transaction> pending;
        std::vector<Transaction> confirmed;
        std::unordered_map<std::string, Transaction> mempool;
        std::vector<Transaction> recentTxs;
        uint64_t txCounter = 0;
        uint64_t totalSupply = 0;
    };

    Snapshot snapshot;
    snapshot.utxoSet = impl_->utxoSet;
    snapshot.pending = impl_->pending;
    snapshot.confirmed = impl_->confirmed;
    snapshot.mempool = impl_->mempool;
    snapshot.recentTxs = impl_->recentTxs;
    snapshot.txCounter = impl_->txCounter;
    snapshot.totalSupply = impl_->totalSupply_;

    auto rollback = [&]() {
        impl_->db.rollbackTransaction();
        impl_->utxoSet = std::move(snapshot.utxoSet);
        impl_->pending = std::move(snapshot.pending);
        impl_->confirmed = std::move(snapshot.confirmed);
        impl_->mempool = std::move(snapshot.mempool);
        impl_->recentTxs = std::move(snapshot.recentTxs);
        impl_->txCounter = snapshot.txCounter;
        impl_->totalSupply_ = snapshot.totalSupply;
    };

    struct UndoSpent {
        crypto::Hash256 txHash{};
        uint32_t outputIndex = 0;
        uint64_t amount = 0;
        std::string address;
    };

    struct UndoCreated {
        crypto::Hash256 txHash{};
        uint32_t outputIndex = 0;
        uint64_t amount = 0;
        std::string address;
    };

    std::vector<UndoSpent> undoSpent;
    std::vector<UndoCreated> undoCreated;
    undoSpent.reserve(txs.size() * 2);
    undoCreated.reserve(txs.size() * 3);

    std::vector<Transaction> newPending;
    newPending.reserve(impl_->pending.size());
    for (auto& ptx : impl_->pending) {
        std::string phex = synapse::crypto::toHex(ptx.txid);
        bool inBlock = (blockTxIds.count(phex) > 0);
        bool conflict = false;
        for (const auto& inp : ptx.inputs) {
            std::string key = synapse::crypto::toHex(inp.prevTxHash) + ":" + std::to_string(inp.outputIndex);
            if (blockSpentInputs.count(key) > 0) {
                conflict = true;
                break;
            }
        }
        if (!conflict) {
            newPending.push_back(ptx);
            continue;
        }

        impl_->mempool.erase(phex);
        if (!inBlock) {
            ptx.status = TxStatus::REJECTED;
            impl_->db.put("tx:" + phex, ptx.serialize());
        }
    }
    impl_->pending = std::move(newPending);

    for (const auto& incoming : txs) {
        Transaction tx = incoming;
        tx.status = TxStatus::CONFIRMED;

        std::string hex = synapse::crypto::toHex(tx.txid);
        std::string txKey = "tx:" + hex;

        bool hadRecord = impl_->db.exists(txKey);
        if (!hadRecord) {
            impl_->txCounter++;
            std::vector<uint8_t> counterBuf;
            writeU64(counterBuf, impl_->txCounter);
            impl_->db.put("meta:txCounter", counterBuf);
        }

        uint64_t totalInput = 0;
        uint64_t totalOutput = 0;
        std::unordered_set<std::string> seenInputs;

        for (const auto& inp : tx.inputs) {
            std::string key = synapse::crypto::toHex(inp.prevTxHash) + ":" + std::to_string(inp.outputIndex);
            if (seenInputs.count(key) > 0) {
                rollback();
                return false;
            }
            seenInputs.insert(key);

            const auto ownerAliases = addressAliasesFromPubKey(inp.pubKey);
            if (ownerAliases[0].empty()) {
                rollback();
                return false;
            }

            bool found = false;
            uint64_t amount = 0;
            for (const auto& ownerAddr : ownerAliases) {
                if (ownerAddr.empty()) continue;
                auto it = impl_->utxoSet.find(ownerAddr);
                if (it == impl_->utxoSet.end()) continue;
                for (auto& utxo : it->second) {
                    if (!utxo.spent && utxo.txHash == inp.prevTxHash && utxo.outputIndex == inp.outputIndex) {
                        UndoSpent us;
                        us.txHash = inp.prevTxHash;
                        us.outputIndex = inp.outputIndex;
                        us.amount = utxo.amount;
                        us.address = ownerAddr;
                        undoSpent.push_back(std::move(us));
                        utxo.spent = true;
                        amount = utxo.amount;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (!found) {
                rollback();
                return false;
            }

            uint64_t tmp = 0;
            if (!safeAddU64(totalInput, amount, tmp)) {
                rollback();
                return false;
            }
            totalInput = tmp;

            impl_->db.del(key);
        }

        for (uint32_t i = 0; i < tx.outputs.size(); ++i) {
            const auto& outp = tx.outputs[i];
            if (outp.amount == 0 || !isValidWalletAddress(outp.address)) {
                rollback();
                return false;
            }

            uint64_t tmp = 0;
            if (!safeAddU64(totalOutput, outp.amount, tmp)) {
                rollback();
                return false;
            }
            totalOutput = tmp;

            UTXO utxo;
            utxo.txHash = tx.txid;
            utxo.outputIndex = i;
            utxo.amount = outp.amount;
            utxo.address = outp.address;
            utxo.spent = false;
            impl_->utxoSet[utxo.address].push_back(utxo);
            UndoCreated uc;
            uc.txHash = tx.txid;
            uc.outputIndex = i;
            uc.amount = outp.amount;
            uc.address = outp.address;
            undoCreated.push_back(std::move(uc));

            std::string utxoKey = synapse::crypto::toHex(tx.txid) + ":" + std::to_string(i);
            std::vector<uint8_t> utxoData;
            writeU64(utxoData, utxo.amount);
            writeString(utxoData, utxo.address);
            impl_->db.put(utxoKey, utxoData);
        }

        uint64_t required = 0;
        if (!safeAddU64(totalOutput, tx.fee, required) || totalInput != required) {
            rollback();
            return false;
        }

        impl_->db.put(txKey, tx.serialize());
        impl_->mempool.erase(hex);
        impl_->confirmed.push_back(tx);
        impl_->recentTxs.push_back(tx);
        if (impl_->recentTxs.size() > 1000) impl_->recentTxs.erase(impl_->recentTxs.begin());
    }

    std::vector<uint8_t> undoBuf;
    undoBuf.insert(undoBuf.end(), blockHash.begin(), blockHash.end());
    writeU32(undoBuf, static_cast<uint32_t>(undoSpent.size()));
    for (const auto& s : undoSpent) {
        undoBuf.insert(undoBuf.end(), s.txHash.begin(), s.txHash.end());
        writeU32(undoBuf, s.outputIndex);
        writeU64(undoBuf, s.amount);
        writeString(undoBuf, s.address);
    }
    writeU32(undoBuf, static_cast<uint32_t>(undoCreated.size()));
    for (const auto& c : undoCreated) {
        undoBuf.insert(undoBuf.end(), c.txHash.begin(), c.txHash.end());
        writeU32(undoBuf, c.outputIndex);
        writeU64(undoBuf, c.amount);
        writeString(undoBuf, c.address);
    }
    writeU32(undoBuf, static_cast<uint32_t>(txs.size()));
    for (const auto& tx : txs) {
        undoBuf.insert(undoBuf.end(), tx.txid.begin(), tx.txid.end());
    }
    impl_->db.put(undoKey, undoBuf);

    if (!impl_->db.commitTransaction()) {
        rollback();
        return false;
    }

    return true;
}

bool TransferManager::rollbackBlockTransactions(uint64_t blockHeight, const crypto::Hash256& blockHash) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    const std::string undoKey = "blockundo:" + std::to_string(blockHeight);
    auto data = impl_->db.get(undoKey);
    if (data.empty()) return false;

    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();

    auto need = [&](size_t n) -> bool {
        return static_cast<size_t>(end - p) >= n;
    };

    crypto::Hash256 storedHash{};
    if (!need(storedHash.size())) return false;
    std::memcpy(storedHash.data(), p, storedHash.size());
    p += storedHash.size();
    if (blockHash != crypto::Hash256{} && storedHash != blockHash) return false;

    struct UndoSpent {
        crypto::Hash256 txHash{};
        uint32_t outputIndex = 0;
        uint64_t amount = 0;
        std::string address;
    };
    struct UndoCreated {
        crypto::Hash256 txHash{};
        uint32_t outputIndex = 0;
        uint64_t amount = 0;
        std::string address;
    };

    auto readHash = [&](crypto::Hash256& out) -> bool {
        if (!need(out.size())) return false;
        std::memcpy(out.data(), p, out.size());
        p += out.size();
        return true;
    };
    auto readU32Safe = [&](uint32_t& out) -> bool {
        if (!need(4)) return false;
        out = readU32(p);
        p += 4;
        return true;
    };
    auto readU64Safe = [&](uint64_t& out) -> bool {
        if (!need(8)) return false;
        out = readU64(p);
        p += 8;
        return true;
    };
    auto readStringSafe = [&](std::string& out) -> bool {
        uint32_t len = 0;
        if (!readU32Safe(len)) return false;
        if (!need(len)) return false;
        out.assign(reinterpret_cast<const char*>(p), len);
        p += len;
        return true;
    };

    uint32_t spentCount = 0;
    if (!readU32Safe(spentCount)) return false;
    std::vector<UndoSpent> spent;
    spent.reserve(spentCount);
    for (uint32_t i = 0; i < spentCount; ++i) {
        UndoSpent s;
        if (!readHash(s.txHash)) return false;
        if (!readU32Safe(s.outputIndex)) return false;
        if (!readU64Safe(s.amount)) return false;
        if (!readStringSafe(s.address)) return false;
        spent.push_back(std::move(s));
    }

    uint32_t createdCount = 0;
    if (!readU32Safe(createdCount)) return false;
    std::vector<UndoCreated> created;
    created.reserve(createdCount);
    for (uint32_t i = 0; i < createdCount; ++i) {
        UndoCreated c;
        if (!readHash(c.txHash)) return false;
        if (!readU32Safe(c.outputIndex)) return false;
        if (!readU64Safe(c.amount)) return false;
        if (!readStringSafe(c.address)) return false;
        created.push_back(std::move(c));
    }

    uint32_t txCount = 0;
    if (!readU32Safe(txCount)) return false;
    std::vector<crypto::Hash256> txids;
    txids.reserve(txCount);
    for (uint32_t i = 0; i < txCount; ++i) {
        crypto::Hash256 h{};
        if (!readHash(h)) return false;
        txids.push_back(h);
    }

    std::unordered_set<std::string> createdKeys;
    createdKeys.reserve(created.size() * 2);
    for (const auto& c : created) {
        createdKeys.insert(crypto::toHex(c.txHash) + ":" + std::to_string(c.outputIndex));
    }

    if (!impl_->db.beginTransaction()) return false;

    struct Snapshot {
        std::unordered_map<std::string, std::vector<UTXO>> utxoSet;
        std::vector<Transaction> pending;
        std::vector<Transaction> confirmed;
        std::unordered_map<std::string, Transaction> mempool;
        std::vector<Transaction> recentTxs;
    };
    Snapshot snapshot;
    snapshot.utxoSet = impl_->utxoSet;
    snapshot.pending = impl_->pending;
    snapshot.confirmed = impl_->confirmed;
    snapshot.mempool = impl_->mempool;
    snapshot.recentTxs = impl_->recentTxs;

    auto rollback = [&]() {
        impl_->db.rollbackTransaction();
        impl_->utxoSet = std::move(snapshot.utxoSet);
        impl_->pending = std::move(snapshot.pending);
        impl_->confirmed = std::move(snapshot.confirmed);
        impl_->mempool = std::move(snapshot.mempool);
        impl_->recentTxs = std::move(snapshot.recentTxs);
    };

    for (const auto& c : created) {
        std::string key = crypto::toHex(c.txHash) + ":" + std::to_string(c.outputIndex);
        impl_->db.del(key);
        auto it = impl_->utxoSet.find(c.address);
        if (it != impl_->utxoSet.end()) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const UTXO& u) {
                return u.txHash == c.txHash && u.outputIndex == c.outputIndex;
            }), vec.end());
            if (vec.empty()) impl_->utxoSet.erase(it);
        }
    }

    for (const auto& s : spent) {
        std::string key = crypto::toHex(s.txHash) + ":" + std::to_string(s.outputIndex);
        if (createdKeys.count(key) > 0) continue;

        std::vector<uint8_t> utxoData;
        writeU64(utxoData, s.amount);
        writeString(utxoData, s.address);
        impl_->db.put(key, utxoData);

        auto& vec = impl_->utxoSet[s.address];
        bool found = false;
        for (auto& u : vec) {
            if (u.txHash == s.txHash && u.outputIndex == s.outputIndex) {
                u.amount = s.amount;
                u.address = s.address;
                u.spent = false;
                found = true;
                break;
            }
        }
        if (!found) {
            UTXO u;
            u.txHash = s.txHash;
            u.outputIndex = s.outputIndex;
            u.amount = s.amount;
            u.address = s.address;
            u.spent = false;
            vec.push_back(std::move(u));
        }
    }

    for (const auto& txid : txids) {
        std::string hex = crypto::toHex(txid);
        std::string txKey = "tx:" + hex;
        auto txData = impl_->db.get(txKey);
        if (txData.empty()) continue;
        Transaction tx = Transaction::deserialize(txData);
        if (tx.txid == crypto::Hash256{}) continue;
        tx.status = TxStatus::PENDING;
        impl_->db.put(txKey, tx.serialize());
    }

    impl_->db.del(undoKey);

    if (!impl_->db.commitTransaction()) {
        rollback();
        return false;
    }

    std::unordered_set<std::string> existingPending;
    existingPending.reserve(impl_->pending.size() * 2);
    for (const auto& tx : impl_->pending) {
        existingPending.insert(crypto::toHex(tx.txid));
    }

    for (const auto& txid : txids) {
        std::string hex = crypto::toHex(txid);
        std::string txKey = "tx:" + hex;
        auto txData = impl_->db.get(txKey);
        if (txData.empty()) continue;
        Transaction tx = Transaction::deserialize(txData);
        if (tx.txid == crypto::Hash256{}) continue;
        if (tx.status != TxStatus::PENDING) continue;
        if (existingPending.count(hex) > 0) continue;
        if (!verifyTransactionLocked(tx, impl_->utxoSet, impl_->mempool, impl_->pending, impl_->config.minFeePerKB)) continue;
        impl_->pending.push_back(tx);
        impl_->mempool[hex] = tx;
        existingPending.insert(hex);
    }

    impl_->confirmed.erase(std::remove_if(impl_->confirmed.begin(), impl_->confirmed.end(),
        [&](const Transaction& tx) {
            return std::find(txids.begin(), txids.end(), tx.txid) != txids.end();
        }), impl_->confirmed.end());

    impl_->recentTxs.erase(std::remove_if(impl_->recentTxs.begin(), impl_->recentTxs.end(),
        [&](const Transaction& tx) {
            return std::find(txids.begin(), txids.end(), tx.txid) != txids.end();
        }), impl_->recentTxs.end());

    return true;
}

bool TransferManager::hasSufficientBalance(const std::string& address, uint64_t amount) const {
    return getBalance(address) >= amount;
}

bool TransferManager::hasTransaction(const crypto::Hash256& txHash) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->mempool.find(crypto::toHex(txHash)) != impl_->mempool.end()) return true;
    std::string key = "tx:" + crypto::toHex(txHash.data(), txHash.size());
    return impl_->db.exists(key);
}

bool TransferManager::creditRewardDeterministic(const std::string& address, const crypto::Hash256& rewardId, uint64_t amount) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (address.empty() || amount == 0 || !isValidWalletAddress(address)) return false;

    Transaction tx;
    tx.timestamp = 0;
    tx.fee = 0;
    tx.status = TxStatus::CONFIRMED;
    tx.outputs.push_back(TxOutput{amount, address});
    tx.txid = rewardId;

    std::string utxoKey = crypto::toHex(tx.txid) + ":0";
    std::string txKey = "tx:" + crypto::toHex(tx.txid);
    if (impl_->db.exists(utxoKey) || impl_->db.exists(txKey)) return false;

    UTXO utxo;
    utxo.txHash = tx.txid;
    utxo.outputIndex = 0;
    utxo.amount = amount;
    utxo.address = address;
    utxo.spent = false;

    impl_->utxoSet[address].push_back(utxo);
    impl_->confirmed.push_back(tx);
    impl_->recentTxs.push_back(tx);
    if (impl_->recentTxs.size() > 1000) impl_->recentTxs.erase(impl_->recentTxs.begin());

    std::vector<uint8_t> utxoData;
    writeU64(utxoData, utxo.amount);
    writeString(utxoData, utxo.address);
    impl_->db.put(crypto::toHex(tx.txid) + ":0", utxoData);

    impl_->db.put("tx:" + crypto::toHex(tx.txid), tx.serialize());

    impl_->txCounter++;
    std::vector<uint8_t> counterBuf;
    writeU64(counterBuf, impl_->txCounter);
    impl_->db.put("meta:txCounter", counterBuf);

    impl_->totalSupply_ += amount;
    std::vector<uint8_t> supplyBuf;
    writeU64(supplyBuf, impl_->totalSupply_);
    impl_->db.put("meta:totalSupply", supplyBuf);

    return true;
}

void TransferManager::onNewTransaction(std::function<void(const Transaction&)> callback) {
    impl_->newTxCallback = callback;
}

void TransferManager::onConfirmation(std::function<void(const crypto::Hash256&)> callback) {
    impl_->confirmCallback = callback;
}

uint64_t TransferManager::totalSupply() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->totalSupply_;
}

uint64_t TransferManager::circulatingSupply() const {
    return totalSupply();
}

size_t TransferManager::transactionCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->txCounter;
}

TransferStats TransferManager::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    TransferStats stats{};
    stats.totalTransactions = impl_->txCounter;
    stats.pendingTransactions = impl_->mempool.size();
    stats.totalSupply = impl_->totalSupply_;
    stats.totalVolume = 0;
    for (const auto& [addr, utxos] : impl_->utxoSet) {
        for (const auto& utxo : utxos) {
            if (!utxo.spent) stats.totalVolume += utxo.amount;
        }
    }
    return stats;
}

std::vector<Transaction> TransferManager::getRecentTransactions(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<Transaction> result;
    size_t start = impl_->recentTxs.size() > count ? impl_->recentTxs.size() - count : 0;
    for (size_t i = impl_->recentTxs.size(); i > start; i--) {
        result.push_back(impl_->recentTxs[i - 1]);
    }
    return result;
}

uint64_t TransferManager::estimateFee(size_t txSize) const {
    return (txSize / 1000 + 1) * impl_->config.minFeePerKB;
}

void TransferManager::setMinFee(uint64_t feePerKB) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.minFeePerKB = feePerKB;
}

void TransferManager::setMaxMempoolSize(size_t maxTx) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.maxMempoolSize = maxTx;
}

void TransferManager::pruneMempool() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t now = std::time(nullptr);
    
    for (auto it = impl_->mempool.begin(); it != impl_->mempool.end(); ) {
        if (now > it->second.timestamp && (now - it->second.timestamp) > impl_->config.mempoolExpiry) {
            Transaction dropped = it->second;
            std::string dropHex = it->first;
            it = impl_->mempool.erase(it);

            auto pit = std::find_if(impl_->pending.begin(), impl_->pending.end(),
                                    [&](const Transaction& p) { return p.txid == dropped.txid; });
            if (pit != impl_->pending.end()) {
                Transaction rej = *pit;
                rej.status = TxStatus::REJECTED;
                impl_->db.put("tx:" + dropHex, rej.serialize());
                impl_->pending.erase(pit);
            }
        } else {
            ++it;
        }
    }
}

}
}
