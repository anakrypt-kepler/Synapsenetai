#include "core/implant_compatibility.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace synapse::core {

namespace {

static constexpr uint32_t MAX_HAL_PAYLOAD_SIZE = 16 * 1024 * 1024;

static bool sourceCharAllowed(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
}

static bool isValidSource(const std::string& source) {
    if (source.empty() || source.size() > 64) return false;
    for (unsigned char c : source) {
        if (!sourceCharAllowed(c)) return false;
    }
    return true;
}

template<typename T>
static bool hasDuplicates(const std::vector<T>& values) {
    std::vector<T> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    return std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end();
}

}

bool PermissionModelV1::allows(PermissionScope scope) const {
    bool listed = std::find(allowedPermissions.begin(), allowedPermissions.end(), scope) != allowedPermissions.end();
    if (defaultDeny) return listed;
    if (allowedPermissions.empty()) return true;
    return listed;
}

bool ImplantCompatibility::validateFrame(
    const ImplantHalFrameV1& frame,
    const ImplantCompatibilityPolicy& policy,
    std::string* reason
) {
    if (frame.version != policy.halVersion) {
        if (reason) *reason = "unsupported_hal_version";
        return false;
    }
    if (frame.protocolVersion < policy.protocolMin || frame.protocolVersion > policy.protocolMax) {
        if (reason) *reason = "protocol_mismatch";
        return false;
    }
    if (frame.timestamp == 0) {
        if (reason) *reason = "missing_timestamp";
        return false;
    }
    if (frame.payloadSize == 0 || frame.payloadSize > MAX_HAL_PAYLOAD_SIZE) {
        if (reason) *reason = "invalid_payload_size";
        return false;
    }
    if (frame.capabilities.empty()) {
        if (reason) *reason = "missing_capabilities";
        return false;
    }
    if (hasDuplicates(frame.capabilities)) {
        if (reason) *reason = "duplicate_capabilities";
        return false;
    }
    return true;
}

bool ImplantCompatibility::validateIntent(
    const IntentV1& intent,
    const PermissionModelV1& permissions,
    const ImplantCompatibilityPolicy& policy,
    std::string* reason
) {
    if (intent.schemaVersion != policy.intentSchemaVersion) {
        if (reason) *reason = "unsupported_intent_schema";
        return false;
    }
    if (intent.type == IntentType::UNKNOWN) {
        if (reason) *reason = "unknown_intent_type";
        return false;
    }
    if (!isValidSource(intent.source)) {
        if (reason) *reason = "invalid_intent_source";
        return false;
    }
    if (intent.timestamp == 0) {
        if (reason) *reason = "missing_intent_timestamp";
        return false;
    }
    if (intent.payloadHash == crypto::Hash256{}) {
        if (reason) *reason = "missing_payload_hash";
        return false;
    }
    if (hasDuplicates(intent.requestedPermissions)) {
        if (reason) *reason = "duplicate_permissions";
        return false;
    }
    for (auto permission : intent.requestedPermissions) {
        if (!permissions.allows(permission)) {
            if (reason) *reason = "permission_denied";
            return false;
        }
    }
    return true;
}

bool ImplantCompatibility::canInstallManifest(
    const UpdateManifest& manifest,
    bool safetyGatePassed,
    const ImplantCompatibilityPolicy& policy,
    std::string* reason
) {
    if (policy.requireSafetyGate && !safetyGatePassed) {
        if (reason) *reason = "safety_gate_required";
        return false;
    }

    std::string manifestReason;
    if (!manifest.validateStrict(&manifestReason)) {
        if (reason) *reason = manifestReason;
        return false;
    }

    bool protocolOverlap = manifest.protocolMin <= policy.protocolMax && manifest.protocolMax >= policy.protocolMin;
    if (!protocolOverlap) {
        if (reason) *reason = "protocol_incompatible";
        return false;
    }

    if (manifest.target.rfind("implant/", 0) != 0) {
        if (reason) *reason = "target_not_implant";
        return false;
    }

    return true;
}

bool ImplantCompatibility::canDistributeManifest(
    const UpdateManifest& manifest,
    bool safetyGatePassed,
    const ImplantCompatibilityPolicy& compatibilityPolicy,
    const ImplantUpdateGovernancePolicy& governancePolicy,
    const std::vector<DetachedSignerApproval>& detachedApprovals,
    std::string* reason
) {
    std::string localReason;
    if (!canInstallManifest(manifest, safetyGatePassed, compatibilityPolicy, &localReason)) {
        if (reason) *reason = localReason;
        return false;
    }

    if (manifest.target != "implant/driver" && manifest.target != "implant/app") {
        if (reason) *reason = "unsupported_implant_target";
        return false;
    }

    if (!verifyDetachedSignerApprovals(manifest, governancePolicy, detachedApprovals, reason)) {
        return false;
    }

    return true;
}

crypto::Hash256 ImplantCompatibility::detachedApprovalHash(const crypto::Hash256& bundleId) {
    std::vector<uint8_t> input;
    const std::string tag = "synapsenet:implant:approval:v1";
    input.insert(input.end(), tag.begin(), tag.end());
    input.insert(input.end(), bundleId.begin(), bundleId.end());
    return crypto::sha256(input.data(), input.size());
}

bool ImplantCompatibility::verifyDetachedSignerApprovals(
    const UpdateManifest& manifest,
    const ImplantUpdateGovernancePolicy& governancePolicy,
    const std::vector<DetachedSignerApproval>& detachedApprovals,
    std::string* reason
) {
    const auto isTrusted = [&](const crypto::PublicKey& signer) {
        return std::find(
            governancePolicy.trustedSigners.begin(),
            governancePolicy.trustedSigners.end(),
            signer
        ) != governancePolicy.trustedSigners.end();
    };

    const uint32_t required = std::max<uint32_t>(1, governancePolicy.minSignerApprovals);
    if (governancePolicy.requireTrustedSigner && governancePolicy.trustedSigners.empty()) {
        if (reason) *reason = "trusted_signer_allowlist_empty";
        return false;
    }

    if (governancePolicy.requireTrustedSigner && !isTrusted(manifest.signer)) {
        if (reason) *reason = "signer_not_trusted";
        return false;
    }

    std::vector<crypto::PublicKey> approved;
    approved.reserve(detachedApprovals.size() + 1);
    approved.push_back(manifest.signer);

    const auto approvalDigest = detachedApprovalHash(manifest.bundleId);
    for (const auto& approval : detachedApprovals) {
        if (governancePolicy.requireTrustedSigner && !isTrusted(approval.signer)) {
            continue;
        }
        if (std::find(approved.begin(), approved.end(), approval.signer) != approved.end()) {
            continue;
        }
        if (!crypto::verify(approvalDigest, approval.signature, approval.signer)) {
            if (reason) *reason = "invalid_detached_approval_signature";
            return false;
        }
        approved.push_back(approval.signer);
    }

    if (approved.size() < required) {
        if (reason) *reason = "insufficient_signer_approvals";
        return false;
    }

    return true;
}

}
