#pragma once

#include "core/update_bundle.h"
#include "crypto/crypto.h"
#include <cstdint>
#include <string>
#include <vector>

namespace synapse::core {

enum class ImplantCapability : uint16_t {
    RAW_SIGNAL = 1,
    FEATURE_STREAM = 2,
    INTENT_STREAM = 3,
    DRIVER_INSTALL = 4,
    APP_INSTALL = 5
};

struct ImplantHalFrameV1 {
    uint32_t version = 1;
    uint32_t protocolVersion = 1;
    uint32_t flags = 0;
    uint64_t timestamp = 0;
    uint64_t sequence = 0;
    uint32_t payloadSize = 0;
    bool compressed = false;
    std::vector<ImplantCapability> capabilities;
};

enum class IntentType : uint8_t {
    UNKNOWN = 0,
    POINTER = 1,
    KEY_INPUT = 2,
    COMMAND = 3
};

enum class PermissionScope : uint8_t {
    RAW_SIGNALS = 0,
    FEATURES = 1,
    INTENTS = 2,
    DRIVER_INSTALL = 3,
    APP_INSTALL = 4
};

struct IntentV1 {
    uint32_t schemaVersion = 1;
    IntentType type = IntentType::UNKNOWN;
    std::string source;
    crypto::Hash256 payloadHash{};
    std::vector<PermissionScope> requestedPermissions;
    uint64_t timestamp = 0;
};

struct PermissionModelV1 {
    bool defaultDeny = true;
    std::vector<PermissionScope> allowedPermissions;

    bool allows(PermissionScope scope) const;
};

struct ImplantCompatibilityPolicy {
    uint32_t protocolMin = 1;
    uint32_t protocolMax = 1;
    uint32_t halVersion = 1;
    uint32_t intentSchemaVersion = 1;
    bool requireSafetyGate = true;
};

struct ImplantUpdateGovernancePolicy {
    bool requireTrustedSigner = true;
    std::vector<crypto::PublicKey> trustedSigners;
    uint32_t minSignerApprovals = 1;
};

struct DetachedSignerApproval {
    crypto::PublicKey signer{};
    crypto::Signature signature{};
};

class ImplantCompatibility {
public:
    static bool validateFrame(
        const ImplantHalFrameV1& frame,
        const ImplantCompatibilityPolicy& policy,
        std::string* reason = nullptr
    );

    static bool validateIntent(
        const IntentV1& intent,
        const PermissionModelV1& permissions,
        const ImplantCompatibilityPolicy& policy,
        std::string* reason = nullptr
    );

    static bool canInstallManifest(
        const UpdateManifest& manifest,
        bool safetyGatePassed,
        const ImplantCompatibilityPolicy& policy,
        std::string* reason = nullptr
    );

    static bool canDistributeManifest(
        const UpdateManifest& manifest,
        bool safetyGatePassed,
        const ImplantCompatibilityPolicy& compatibilityPolicy,
        const ImplantUpdateGovernancePolicy& governancePolicy,
        const std::vector<DetachedSignerApproval>& detachedApprovals = {},
        std::string* reason = nullptr
    );

    static crypto::Hash256 detachedApprovalHash(const crypto::Hash256& bundleId);

    static bool verifyDetachedSignerApprovals(
        const UpdateManifest& manifest,
        const ImplantUpdateGovernancePolicy& governancePolicy,
        const std::vector<DetachedSignerApproval>& detachedApprovals,
        std::string* reason = nullptr
    );
};

}
