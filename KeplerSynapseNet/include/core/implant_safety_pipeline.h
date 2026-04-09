#pragma once

#include "crypto/crypto.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace synapse::core {

struct ImplantSafetyRecord {
    bool deterministicTestsPassed = false;
    bool sandboxBoundariesPassed = false;
    bool canaryHealthPassed = false;
    bool wideHealthPassed = false;
    uint64_t updatedAt = 0;
};

class ImplantSafetyPipeline {
public:
    ImplantSafetyPipeline() = default;

    bool open(const std::string& statePath, std::string* reason = nullptr);

    bool markPrepare(
        const crypto::Hash256& bundleId,
        bool deterministicTestsPassed,
        bool sandboxBoundariesPassed,
        std::string* reason = nullptr
    );

    bool markCanaryHealth(
        const crypto::Hash256& bundleId,
        bool canaryHealthPassed,
        std::string* reason = nullptr
    );

    bool markWideHealth(
        const crypto::Hash256& bundleId,
        bool wideHealthPassed,
        std::string* reason = nullptr
    );

    bool canCommit(const crypto::Hash256& bundleId, std::string* reason = nullptr) const;

    std::optional<ImplantSafetyRecord> getRecord(const crypto::Hash256& bundleId) const;
    bool clearRecord(const crypto::Hash256& bundleId, std::string* reason = nullptr);

private:
    bool loadState(std::string* reason = nullptr);
    bool saveStateAtomic(std::string* reason = nullptr) const;

    std::string statePath_;
    std::unordered_map<std::string, ImplantSafetyRecord> recordsByBundleId_;
};

}
