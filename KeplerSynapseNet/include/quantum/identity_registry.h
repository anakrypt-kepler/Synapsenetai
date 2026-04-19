#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
