#ifndef SYNAPSE_CORE_WALLET_H
#define SYNAPSE_CORE_WALLET_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace synapse {
namespace core {

using AmountAtoms = uint64_t;
inline constexpr AmountAtoms NGT_ATOMS_PER_UNIT = 100000000ULL;

class Wallet {
public:
    Wallet();
    ~Wallet();
    
    bool create();
    bool restore(const std::vector<std::string>& seedWords);
    bool load(const std::string& path, const std::string& password);
    bool save(const std::string& path, const std::string& password);
    
    void lock();
    bool unlock(const std::string& password);
    bool isLocked() const;
    
    std::vector<std::string> getSeedWords() const;
    std::string getAddress() const;
    std::vector<uint8_t> getPublicKey() const;
    
    AmountAtoms getBalance() const;
    AmountAtoms getPendingBalance() const;
    AmountAtoms getStakedBalance() const;
    
    void setBalance(AmountAtoms balance);
    void setPendingBalance(AmountAtoms pending);
    void setStakedBalance(AmountAtoms staked);
    
    std::vector<uint8_t> sign(const std::vector<uint8_t>& message) const;
    static bool verify(const std::vector<uint8_t>& message, const std::vector<uint8_t>& signature,
                       const std::vector<uint8_t>& publicKey);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}

#endif
