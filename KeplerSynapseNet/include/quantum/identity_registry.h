#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Trust-on-first-use map: wallet address → hybrid identity id (ML-DSA-65 KQAS).
// Stored in <data>/identities.json. A later tx with a different PQ key is rejected.
// 2-arg verifyBinding is parse-only TOFU. 5-arg AND-verifies HybridSig first.

namespace synapse::quantum {

class IdentityRegistry {
public:
    static IdentityRegistry& instance();

    void setStoragePath(const std::string& path);

    bool registerIdentity(const std::string& address, const std::string& identityId);

    std::string lookupIdentity(const std::string& address) const;

    bool hasBinding(const std::string& address) const;

    bool verifyBinding(const std::string& address,
                       const std::vector<uint8_t>& envelopeBytes) const;

    // Parse-only TOFU plus HybridSig AND-verify over domain/payload/binding.
    bool verifyBinding(const std::string& address,
                       const std::string& domain,
                       const std::vector<uint8_t>& payload,
                       const std::vector<uint8_t>& binding,
                       const std::vector<uint8_t>& envelopeBytes) const;

    void clear();

private:
    IdentityRegistry();

    bool loadLocked() const;
    bool persistLocked() const;

    mutable std::mutex mtx_;
    mutable std::unordered_map<std::string, std::string> bindings_;
    std::string storagePath_;
    mutable bool loaded_ = false;
};

}
