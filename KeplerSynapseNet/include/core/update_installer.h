#pragma once

#include "core/update_bundle.h"
#include "crypto/crypto.h"

#include <string>

namespace synapse::core {

enum class UpdateRolloutStage : uint8_t {
    CANARY = 0,
    WIDE = 1,
    COMPLETE = 2
};

struct UpdateInstallerState {
    char activeSlot = 'A';

    bool hasSlotABundle = false;
    crypto::Hash256 slotABundle{};

    bool hasSlotBBundle = false;
    crypto::Hash256 slotBBundle{};

    bool hasPending = false;
    char pendingSlot = 'A';
    crypto::Hash256 pendingBundle{};
    UpdateRolloutStage pendingStage = UpdateRolloutStage::CANARY;

    bool hasLastKnownGood = false;
    crypto::Hash256 lastKnownGood{};
};

class UpdateInstaller {
public:
    UpdateInstaller() = default;

    bool open(const std::string& statePath, std::string* reason = nullptr);
    const UpdateInstallerState& state() const;

    struct UpdatePolicy {
        std::vector<crypto::PublicKey> allowedSigners;
        uint32_t minSignatures = 1;
    };

    bool installManifest(const UpdateManifest& manifest,
                         const UpdatePolicy& policy,
                         std::string* reason = nullptr);
    bool advanceRollout(const crypto::Hash256& bundleId, std::string* reason = nullptr);
    bool commitPending(const crypto::Hash256& bundleId, std::string* reason = nullptr);
    bool rollback(std::string* reason = nullptr);

private:
    bool loadState(std::string* reason = nullptr);
    bool saveStateAtomic(std::string* reason = nullptr) const;

    std::string statePath_;
    UpdateInstallerState state_{};
};

std::string toString(UpdateRolloutStage stage);
bool parseUpdateRolloutStage(const std::string& value, UpdateRolloutStage& out);

}
