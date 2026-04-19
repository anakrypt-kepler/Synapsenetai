#include "core/implant_compatibility.h"
#include "core/update_bundle.h"
#include "crypto/crypto.h"
#include <cassert>
#include <string>
#include <vector>

static synapse::core::UpdateManifest makeValidManifestWithSigner(
    uint32_t protocolMin,
    uint32_t protocolMax,
    const std::string& target,
    const synapse::crypto::PrivateKey& signer
) {
    synapse::core::UpdateManifest manifest;
    synapse::core::UpdateChunk c1;
    c1.hash = synapse::crypto::sha256("implant_chunk_1");
    c1.size = 1024;
    synapse::core::UpdateChunk c2;
    c2.hash = synapse::crypto::sha256("implant_chunk_2");
    c2.size = 2048;
    manifest.chunks = {c1, c2};
    manifest.target = target;
    manifest.protocolMin = protocolMin;
    manifest.protocolMax = protocolMax;
    bool ok = synapse::core::signUpdateManifest(manifest, signer, nullptr);
    assert(ok);
    return manifest;
}

static synapse::core::UpdateManifest makeValidManifest(uint32_t protocolMin, uint32_t protocolMax) {
    auto keyPair = synapse::crypto::generateKeyPair();
    return makeValidManifestWithSigner(protocolMin, protocolMax, "implant/driver", keyPair.privateKey);
}

static synapse::core::DetachedSignerApproval makeApproval(
    const synapse::crypto::Hash256& bundleId,
    const synapse::crypto::PrivateKey& signer
) {
    synapse::core::DetachedSignerApproval out;
    out.signer = synapse::crypto::derivePublicKey(signer);
    auto digest = synapse::core::ImplantCompatibility::detachedApprovalHash(bundleId);
    out.signature = synapse::crypto::sign(digest, signer);
    return out;
}

static void testFrameValidation() {
    synapse::core::ImplantCompatibilityPolicy policy;
    policy.protocolMin = 1;
    policy.protocolMax = 3;
    policy.halVersion = 1;

    synapse::core::ImplantHalFrameV1 frame;
    frame.version = 1;
    frame.protocolVersion = 2;
    frame.timestamp = 100;
    frame.sequence = 1;
    frame.payloadSize = 4096;
    frame.capabilities = {
        synapse::core::ImplantCapability::FEATURE_STREAM,
        synapse::core::ImplantCapability::INTENT_STREAM
    };

    std::string reason;
    assert(synapse::core::ImplantCompatibility::validateFrame(frame, policy, &reason));

    frame.protocolVersion = 7;
    assert(!synapse::core::ImplantCompatibility::validateFrame(frame, policy, &reason));
    assert(reason == "protocol_mismatch");

    frame.protocolVersion = 2;
    frame.version = 2;
    assert(!synapse::core::ImplantCompatibility::validateFrame(frame, policy, &reason));
    assert(reason == "unsupported_hal_version");
}

static void testIntentValidationAndPermissionModel() {
    synapse::core::ImplantCompatibilityPolicy policy;
    policy.intentSchemaVersion = 1;

    synapse::core::PermissionModelV1 permissions;
    permissions.defaultDeny = true;
    permissions.allowedPermissions = {
        synapse::core::PermissionScope::INTENTS,
        synapse::core::PermissionScope::FEATURES
    };

    synapse::core::IntentV1 intent;
    intent.schemaVersion = 1;
    intent.type = synapse::core::IntentType::COMMAND;
    intent.source = "implant_hub";
    intent.payloadHash = synapse::crypto::sha256("intent_payload");
    intent.requestedPermissions = {synapse::core::PermissionScope::INTENTS};
    intent.timestamp = 200;

    std::string reason;
    assert(synapse::core::ImplantCompatibility::validateIntent(intent, permissions, policy, &reason));

    intent.requestedPermissions = {synapse::core::PermissionScope::DRIVER_INSTALL};
    assert(!synapse::core::ImplantCompatibility::validateIntent(intent, permissions, policy, &reason));
    assert(reason == "permission_denied");

    intent.requestedPermissions = {synapse::core::PermissionScope::INTENTS};
    intent.schemaVersion = 2;
    assert(!synapse::core::ImplantCompatibility::validateIntent(intent, permissions, policy, &reason));
    assert(reason == "unsupported_intent_schema");
}

static void testInstallCompatibilityMatrix() {
    synapse::core::ImplantCompatibilityPolicy policy;
    policy.protocolMin = 2;
    policy.protocolMax = 4;
    policy.requireSafetyGate = true;

    std::string reason;
    auto compatibleManifest = makeValidManifest(3, 4);
    assert(synapse::core::ImplantCompatibility::canInstallManifest(compatibleManifest, true, policy, &reason));

    assert(!synapse::core::ImplantCompatibility::canInstallManifest(compatibleManifest, false, policy, &reason));
    assert(reason == "safety_gate_required");

    auto incompatibleManifest = makeValidManifest(5, 6);
    assert(!synapse::core::ImplantCompatibility::canInstallManifest(incompatibleManifest, true, policy, &reason));
    assert(reason == "protocol_incompatible");

    auto lowerBoundaryManifest = makeValidManifest(1, 2);
    assert(synapse::core::ImplantCompatibility::canInstallManifest(lowerBoundaryManifest, true, policy, &reason));

    auto upperBoundaryManifest = makeValidManifest(4, 7);
    assert(synapse::core::ImplantCompatibility::canInstallManifest(upperBoundaryManifest, true, policy, &reason));
}

static void testSignedDriverAppDistributionPolicy() {
    auto trusted = synapse::crypto::generateKeyPair();
    auto untrusted = synapse::crypto::generateKeyPair();

    synapse::core::ImplantCompatibilityPolicy compatibility;
    compatibility.protocolMin = 1;
    compatibility.protocolMax = 3;
    compatibility.requireSafetyGate = true;

    synapse::core::ImplantUpdateGovernancePolicy governance;
    governance.requireTrustedSigner = true;
    governance.trustedSigners = {trusted.publicKey};

    std::string reason;

    auto trustedDriver = makeValidManifestWithSigner(1, 2, "implant/driver", trusted.privateKey);
    assert(synapse::core::ImplantCompatibility::canDistributeManifest(trustedDriver, true, compatibility, governance, {}, &reason));

    auto trustedApp = makeValidManifestWithSigner(1, 2, "implant/app", trusted.privateKey);
    assert(synapse::core::ImplantCompatibility::canDistributeManifest(trustedApp, true, compatibility, governance, {}, &reason));

    auto wrongTarget = makeValidManifestWithSigner(1, 2, "implant/kernel", trusted.privateKey);
    assert(!synapse::core::ImplantCompatibility::canDistributeManifest(wrongTarget, true, compatibility, governance, {}, &reason));
    assert(reason == "unsupported_implant_target");

    auto untrustedDriver = makeValidManifestWithSigner(1, 2, "implant/driver", untrusted.privateKey);
    assert(!synapse::core::ImplantCompatibility::canDistributeManifest(untrustedDriver, true, compatibility, governance, {}, &reason));
    assert(reason == "signer_not_trusted");

    assert(!synapse::core::ImplantCompatibility::canDistributeManifest(trustedDriver, false, compatibility, governance, {}, &reason));
    assert(reason == "safety_gate_required");

    governance.requireTrustedSigner = false;
    assert(synapse::core::ImplantCompatibility::canDistributeManifest(untrustedDriver, true, compatibility, governance, {}, &reason));
}

static void testDetachedSignerThresholdPolicy() {
    auto k1 = synapse::crypto::generateKeyPair();
    auto k2 = synapse::crypto::generateKeyPair();
    auto k3 = synapse::crypto::generateKeyPair();
    auto outsider = synapse::crypto::generateKeyPair();

    synapse::core::ImplantCompatibilityPolicy compatibility;
    compatibility.protocolMin = 1;
    compatibility.protocolMax = 3;
    compatibility.requireSafetyGate = true;

    synapse::core::ImplantUpdateGovernancePolicy governance;
    governance.requireTrustedSigner = true;
    governance.trustedSigners = {k1.publicKey, k2.publicKey, k3.publicKey};
    governance.minSignerApprovals = 2;

    std::string reason;
    auto manifest = makeValidManifestWithSigner(1, 2, "implant/driver", k1.privateKey);

    assert(!synapse::core::ImplantCompatibility::canDistributeManifest(
        manifest,
        true,
        compatibility,
        governance,
        {},
        &reason
    ));
    assert(reason == "insufficient_signer_approvals");

    std::vector<synapse::core::DetachedSignerApproval> approvals;
    approvals.push_back(makeApproval(manifest.bundleId, k2.privateKey));
    assert(synapse::core::ImplantCompatibility::canDistributeManifest(
        manifest,
        true,
        compatibility,
        governance,
        approvals,
        &reason
    ));

    approvals.clear();
    auto invalid = makeApproval(manifest.bundleId, k2.privateKey);
    invalid.signature[0] ^= 0xFF;
    approvals.push_back(invalid);
    assert(!synapse::core::ImplantCompatibility::canDistributeManifest(
        manifest,
        true,
        compatibility,
        governance,
        approvals,
        &reason
    ));
    assert(reason == "invalid_detached_approval_signature");

    approvals.clear();
    approvals.push_back(makeApproval(manifest.bundleId, outsider.privateKey));
    assert(!synapse::core::ImplantCompatibility::canDistributeManifest(
        manifest,
        true,
        compatibility,
        governance,
        approvals,
        &reason
    ));
    assert(reason == "insufficient_signer_approvals");
}

static void testTrustedSignerAllowlistRequired() {
    auto signer = synapse::crypto::generateKeyPair();

    synapse::core::ImplantCompatibilityPolicy compatibility;
    compatibility.protocolMin = 1;
    compatibility.protocolMax = 3;
    compatibility.requireSafetyGate = true;

    synapse::core::ImplantUpdateGovernancePolicy governance;
    governance.requireTrustedSigner = true;
    governance.minSignerApprovals = 1;

    std::string reason;
    auto manifest = makeValidManifestWithSigner(1, 2, "implant/driver", signer.privateKey);
    assert(!synapse::core::ImplantCompatibility::canDistributeManifest(
        manifest,
        true,
        compatibility,
        governance,
        {},
        &reason
    ));
    assert(reason == "trusted_signer_allowlist_empty");
}

int main() {
    testFrameValidation();
    testIntentValidationAndPermissionModel();
    testInstallCompatibilityMatrix();
    testSignedDriverAppDistributionPolicy();
    testDetachedSignerThresholdPolicy();
    testTrustedSignerAllowlistRequired();
    return 0;
}
