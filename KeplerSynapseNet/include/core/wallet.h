#ifndef SYNAPSE_CORE_WALLET_H
#define SYNAPSE_CORE_WALLET_H

// Local NGT wallet.
// Seed is a 24-word BIP39 mnemonic. Signing uses secp256k1 plus an optional
// hybrid post-quantum keypair (classic + ML-DSA-65).
// On-disk format v4 wraps a random AES-256-GCM DEK with ML-KEM-768 (password →
// PBKDF2 → HKDF seed → deterministic Kyber). v3 is AES-256-GCM + PBKDF2 only.
// If real Kyber is unavailable we still write v3 (do not pretend). v1/v2/v3
// migrate to v4 on unlock when Kyber is real, otherwise v1/v2 migrate to v3.
// Never log the mnemonic or secret keys.

#include "quantum/quantum_security.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace synapse {
namespace core {

using AmountAtoms = uint64_t;
// 100,000,000 atoms = 1 NGT (same scale as satoshis).
inline constexpr AmountAtoms NGT_ATOMS_PER_UNIT = 100000000ULL;

class Wallet {
public:
    Wallet();
    ~Wallet();

    // Fresh mnemonic + keypair. Does not write to disk until save().
    bool create();
    // Rebuild keys from an existing 24-word seed.
    bool restore(const std::vector<std::string>& seedWords);
    bool load(const std::string& path, const std::string& password);
    bool save(const std::string& path, const std::string& password);

    void lock();
    bool unlock(const std::string& password);
    bool isLocked() const;

    std::vector<std::string> getSeedWords() const;
    std::string getAddress() const;
    std::vector<uint8_t> getPublicKey() const;

    // Hybrid identity is classic pubkey + PQC pubkey, used for PQ-signed txs/blocks.
    bool hasHybridKeyPair() const;
    quantum::HybridKeyPair getHybridKeyPair() const;
    std::vector<uint8_t> getHybridClassicPublicKey() const;
    std::vector<uint8_t> getHybridPqcPublicKey() const;
    std::string getHybridIdentityId() const;

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
