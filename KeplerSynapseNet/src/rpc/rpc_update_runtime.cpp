#include "rpc/rpc_update_runtime.h"

#include "core/implant_compatibility.h"

#include <algorithm>
#include <ctime>
#include <stdexcept>

namespace synapse::rpc {

using json = nlohmann::json;

namespace {

void requireCallback(bool available, const char* name) {
    if (!available) {
        throw std::runtime_error(name);
    }
}

json parseRpcParams(const std::string& paramsJson) {
    if (paramsJson.empty()) {
        return json::object();
    }
    json parsed = json::parse(paramsJson, nullptr, false);
    if (parsed.is_discarded()) {
        throw std::runtime_error("Invalid JSON params");
    }
    if (parsed.is_array()) {
        if (parsed.empty()) {
            return json::object();
        }
        if (!parsed.front().is_object()) {
            throw std::runtime_error("Expected object params");
        }
        return parsed.front();
    }
    if (!parsed.is_object()) {
        throw std::runtime_error("Expected object params");
    }
    return parsed;
}

crypto::Hash256 parseHash256Hex(const std::string& hex) {
    crypto::Hash256 out{};
    auto bytes = crypto::fromHex(hex);
    if (bytes.size() != out.size()) {
        throw std::runtime_error("Expected 32-byte hex string");
    }
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return out;
}

struct BundleRef {
    crypto::Hash256 bundleId{};
    std::string bundleIdHex;
};

BundleRef parseBundleRef(const json& params) {
    std::string bundleIdHex = params.value("bundleId", "");
    if (bundleIdHex.empty()) {
        bundleIdHex = params.value("id", "");
    }
    if (bundleIdHex.empty()) {
        throw std::runtime_error("bundleId required");
    }
    BundleRef out;
    out.bundleId = parseHash256Hex(bundleIdHex);
    out.bundleIdHex = crypto::toHex(out.bundleId);
    return out;
}

bool readBoolAlias(const json& params, const char* primary, const char* alias) {
    bool value = params.value(primary, false);
    if (!params.contains(primary)) {
        value = params.value(alias, false);
    }
    return value;
}

bool parsePublicKeyHex(const std::string& hex, crypto::PublicKey& out) {
    const auto bytes = crypto::fromHex(hex);
    if (bytes.size() != out.size()) {
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return true;
}

bool parseSignatureHex(const std::string& hex, crypto::Signature& out) {
    const auto bytes = crypto::fromHex(hex);
    if (bytes.size() != out.size()) {
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return true;
}

void emitRuntimeSecurityEvent(
    const RpcUpdateRuntimeInputs& inputs,
    const std::string& kind,
    const std::string& subject,
    const std::string& bundleIdHex,
    const std::string& reason) {
    requireCallback(static_cast<bool>(inputs.emitSecurityEvent), "invalid_rpc_update_runtime_inputs");
    json details;
    details["bundleId"] = bundleIdHex;
    details["reason"] = reason;
    inputs.emitSecurityEvent(
        static_cast<uint64_t>(std::time(nullptr)),
        kind,
        "high",
        subject,
        details);
}

core::UpdateManifest fetchStoredManifestOrThrow(
    const RpcUpdateRuntimeInputs& inputs,
    const crypto::Hash256& bundleId) {
    requireCallback(
        static_cast<bool>(inputs.fetchStoredUpdateManifest),
        "invalid_rpc_update_runtime_inputs");
    core::UpdateManifest manifest;
    if (!inputs.fetchStoredUpdateManifest(bundleId, manifest)) {
        throw std::runtime_error("manifest_not_found");
    }
    return manifest;
}

RpcSecurityPolicyHashes buildPolicyHashesOrThrow(const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.buildPolicyHashes),
        "invalid_rpc_update_runtime_inputs");
    return inputs.buildPolicyHashes();
}

json buildPolicyJsonOrThrow(const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.buildPolicyJson),
        "invalid_rpc_update_runtime_inputs");
    return inputs.buildPolicyJson();
}

std::vector<core::UpdateManifest> listStoredUpdateManifestsOrThrow(
    const RpcUpdateRuntimeInputs& inputs,
    size_t limit) {
    requireCallback(
        static_cast<bool>(inputs.listStoredUpdateManifests),
        "invalid_rpc_update_runtime_inputs");
    return inputs.listStoredUpdateManifests(limit);
}

} // namespace

std::string runRpcUpdateManifestSubmit(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.acceptUpdateManifest),
        "invalid_rpc_update_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    std::string manifestHex = params.value("manifest", "");
    if (manifestHex.empty()) {
        manifestHex = params.value("manifestHex", "");
    }
    if (manifestHex.empty()) {
        throw std::runtime_error("manifest hex required");
    }

    const auto bytes = crypto::fromHex(manifestHex);
    if (bytes.empty()) {
        throw std::runtime_error("manifest hex decode failed");
    }

    auto manifestOpt = core::UpdateManifest::deserialize(bytes);
    if (!manifestOpt) {
        throw std::runtime_error("manifest_deserialize_failed");
    }

    std::string reason;
    const auto status = inputs.acceptUpdateManifest(*manifestOpt, true, &reason);
    if (status == RpcUpdateManifestAccept::Rejected) {
        throw std::runtime_error("invalid_manifest: " + reason);
    }

    json out = updateManifestToJson(*manifestOpt, true);
    out["status"] =
        (status == RpcUpdateManifestAccept::Duplicate) ? "duplicate" : "accepted";
    out["reason"] = reason;
    out["serialized"] = crypto::toHex(manifestOpt->serialize());
    return out.dump();
}

std::string runRpcUpdateManifestFetch(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    const auto params = parseRpcParams(paramsJson);
    const auto bundleRef = parseBundleRef(params);
    const auto manifest = fetchStoredManifestOrThrow(inputs, bundleRef.bundleId);

    json out = updateManifestToJson(manifest, true);
    out["status"] = "ok";
    out["serialized"] = crypto::toHex(manifest.serialize());
    return out.dump();
}

std::string runRpcUpdateManifestList(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    const auto params = parseRpcParams(paramsJson);
    size_t limit = 25;
    if (params.contains("limit")) {
        limit = static_cast<size_t>(std::max(1, params.value("limit", 25)));
    }

    const auto manifests = listStoredUpdateManifestsOrThrow(inputs, limit);
    json out = json::array();
    for (const auto& manifest : manifests) {
        out.push_back(updateManifestToJson(manifest, false));
    }
    return out.dump();
}

std::string runRpcUpdateManifestApprove(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.upsertDetachedSignerApproval),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.validateUpdateSignerThreshold),
        "invalid_rpc_update_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    const auto bundleRef = parseBundleRef(params);

    const std::string signerHex = params.value("signer", "");
    if (signerHex.empty()) {
        throw std::runtime_error("signer required");
    }
    const std::string signatureHex = params.value("signature", "");
    if (signatureHex.empty()) {
        throw std::runtime_error("signature required");
    }

    const auto manifest = fetchStoredManifestOrThrow(inputs, bundleRef.bundleId);

    core::DetachedSignerApproval approval;
    if (!parsePublicKeyHex(signerHex, approval.signer)) {
        throw std::runtime_error("invalid_signer");
    }
    if (!parseSignatureHex(signatureHex, approval.signature)) {
        throw std::runtime_error("invalid_signature");
    }

    const auto digest = core::ImplantCompatibility::detachedApprovalHash(bundleRef.bundleId);
    if (!crypto::verify(digest, approval.signature, approval.signer)) {
        throw std::runtime_error("invalid_detached_approval_signature");
    }

    inputs.upsertDetachedSignerApproval(bundleRef.bundleId, approval);

    std::string governanceReason;
    const bool governanceReady =
        inputs.validateUpdateSignerThreshold(manifest, &governanceReason);

    requireCallback(
        static_cast<bool>(inputs.buildDetachedSignerApprovalsJson),
        "invalid_rpc_update_runtime_inputs");
    json out = inputs.buildDetachedSignerApprovalsJson(bundleRef.bundleId);
    out["status"] = "ok";
    out["governanceReady"] = governanceReady;
    out["governanceReason"] = governanceReason;

    if (inputs.emitSecurityEvent) {
        json details;
        details["bundleId"] = bundleRef.bundleIdHex;
        details["signer"] = crypto::toHex(approval.signer);
        details["governanceReady"] = governanceReady;
        details["governanceReason"] = governanceReason;
        inputs.emitSecurityEvent(
            static_cast<uint64_t>(std::time(nullptr)),
            "detached_signer_approval",
            "high",
            "update_manifest",
            details);
    }
    return out.dump();
}

std::string runRpcUpdateManifestApprovals(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.validateUpdateSignerThreshold),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.buildDetachedSignerApprovalsJson),
        "invalid_rpc_update_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    const auto bundleRef = parseBundleRef(params);
    const auto manifest = fetchStoredManifestOrThrow(inputs, bundleRef.bundleId);

    std::string governanceReason;
    const bool governanceReady =
        inputs.validateUpdateSignerThreshold(manifest, &governanceReason);

    json out = inputs.buildDetachedSignerApprovalsJson(bundleRef.bundleId);
    out["status"] = "ok";
    out["governanceReady"] = governanceReady;
    out["governanceReason"] = governanceReason;
    out["policy"] = buildPolicyJsonOrThrow(inputs);
    return out.dump();
}

std::string runRpcUpdateInstallState(const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.readInstallerState),
        "invalid_rpc_update_runtime_inputs");

    const auto installerState = inputs.readInstallerState();

    RpcUpdateInstallStateResponseInputs out;
    out.installerState = installerState;
    out.policy = buildPolicyJsonOrThrow(inputs);
    out.policyHashes = buildPolicyHashesOrThrow(inputs);

    if (!installerState.hasPending) {
        return buildRpcUpdateInstallStateResponse(out);
    }

    requireCallback(
        static_cast<bool>(inputs.buildDetachedSignerApprovalsJson),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.validateUpdateSignerThreshold),
        "invalid_rpc_update_runtime_inputs");

    out.includePendingApprovals = true;
    out.pendingApprovals =
        inputs.buildDetachedSignerApprovalsJson(installerState.pendingBundle);
    out.pendingSignerThresholdReason = "manifest_not_found";

    core::UpdateManifest pendingManifest;
    if (inputs.fetchStoredUpdateManifest &&
        inputs.fetchStoredUpdateManifest(installerState.pendingBundle, pendingManifest)) {
        out.pendingManifest = pendingManifest;
        std::string thresholdReason;
        out.pendingSignerThresholdReady =
            inputs.validateUpdateSignerThreshold(pendingManifest, &thresholdReason);
        out.pendingSignerThresholdReason = thresholdReason;
    }

    return buildRpcUpdateInstallStateResponse(out);
}

std::string runRpcUpdateInstallPrepare(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.validateUpdateSignerThreshold),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.installerPolicyForManifest),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(static_cast<bool>(inputs.installManifest), "invalid_rpc_update_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    const auto bundleRef = parseBundleRef(params);
    const auto manifest = fetchStoredManifestOrThrow(inputs, bundleRef.bundleId);

    std::string reason;
    if (!inputs.validateUpdateSignerThreshold(manifest, &reason)) {
        emitRuntimeSecurityEvent(
            inputs,
            "update_signer_threshold_failed",
            "update.install.prepare",
            bundleRef.bundleIdHex,
            reason);
        throw std::runtime_error("update_signer_threshold_failed: " + reason);
    }

    core::UpdateInstallerState installerState;
    const auto policy = inputs.installerPolicyForManifest(manifest);
    if (!inputs.installManifest(manifest, policy, &reason, &installerState)) {
        throw std::runtime_error("install_prepare_failed: " + reason);
    }

    return buildRpcUpdateInstallerActionResponse(
        {installerState, reason, bundleRef.bundleIdHex});
}

std::string runRpcUpdateInstallAdvance(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.validateUpdateSignerThreshold),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(static_cast<bool>(inputs.advanceRollout), "invalid_rpc_update_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    const auto bundleRef = parseBundleRef(params);
    const auto manifest = fetchStoredManifestOrThrow(inputs, bundleRef.bundleId);

    std::string reason;
    if (!inputs.validateUpdateSignerThreshold(manifest, &reason)) {
        emitRuntimeSecurityEvent(
            inputs,
            "update_signer_threshold_failed",
            "update.install.advance",
            bundleRef.bundleIdHex,
            reason);
        throw std::runtime_error("update_signer_threshold_failed: " + reason);
    }

    core::UpdateInstallerState installerState;
    if (!inputs.advanceRollout(bundleRef.bundleId, &reason, &installerState)) {
        throw std::runtime_error("install_advance_failed: " + reason);
    }

    return buildRpcUpdateInstallerActionResponse(
        {installerState, reason, bundleRef.bundleIdHex});
}

std::string runRpcUpdateInstallCommit(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.validateUpdateSignerThreshold),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(static_cast<bool>(inputs.commitPending), "invalid_rpc_update_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    const auto bundleRef = parseBundleRef(params);
    const auto manifest = fetchStoredManifestOrThrow(inputs, bundleRef.bundleId);

    std::string reason;
    if (!inputs.validateUpdateSignerThreshold(manifest, &reason)) {
        emitRuntimeSecurityEvent(
            inputs,
            "update_signer_threshold_failed",
            "update.install.commit",
            bundleRef.bundleIdHex,
            reason);
        throw std::runtime_error("update_signer_threshold_failed: " + reason);
    }

    core::UpdateInstallerState installerState;
    if (!inputs.commitPending(bundleRef.bundleId, &reason, &installerState)) {
        throw std::runtime_error("install_commit_failed: " + reason);
    }

    return buildRpcUpdateInstallerActionResponse(
        {installerState, reason, bundleRef.bundleIdHex});
}

std::string runRpcUpdateInstallRollback(const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(static_cast<bool>(inputs.rollback), "invalid_rpc_update_runtime_inputs");

    std::string reason;
    core::UpdateInstallerState installerState;
    if (!inputs.rollback(&reason, &installerState)) {
        throw std::runtime_error("install_rollback_failed: " + reason);
    }

    return buildRpcUpdateInstallerActionResponse({installerState, reason, ""});
}

std::string runRpcImplantUpdateState(const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.readInstallerState),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.canCommitImplant),
        "invalid_rpc_update_runtime_inputs");

    const auto installerState = inputs.readInstallerState();

    RpcImplantUpdateStateResponseInputs out;
    out.installerState = installerState;
    out.policy = buildPolicyJsonOrThrow(inputs);
    out.policyHashes = buildPolicyHashesOrThrow(inputs);

    if (!installerState.hasPending) {
        return buildRpcImplantUpdateStateResponse(out);
    }

    core::UpdateManifest pendingManifest;
    if (inputs.fetchStoredUpdateManifest &&
        inputs.fetchStoredUpdateManifest(installerState.pendingBundle, pendingManifest)) {
        out.pendingManifest = pendingManifest;
        out.includePendingApprovals = true;
        if (inputs.buildDetachedSignerApprovalsJson) {
            out.pendingApprovals =
                inputs.buildDetachedSignerApprovalsJson(installerState.pendingBundle);
        }
        if (inputs.validateUpdateSignerThreshold) {
            std::string thresholdReason;
            out.includePendingSignerThreshold = true;
            out.pendingSignerThresholdReady =
                inputs.validateUpdateSignerThreshold(pendingManifest, &thresholdReason);
            out.pendingSignerThresholdReason = thresholdReason;
        }
    }

    if (inputs.getImplantSafetyRecord) {
        out.safetyRecord = inputs.getImplantSafetyRecord(installerState.pendingBundle);
    }

    std::string safetyReason;
    out.includeSafetyCommit = true;
    out.safetyCommitReady = inputs.canCommitImplant(installerState.pendingBundle, &safetyReason);
    out.safetyCommitReason = safetyReason;
    return buildRpcImplantUpdateStateResponse(out);
}

std::string runRpcImplantUpdatePrepare(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.validateImplantDistributionManifest),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.markImplantPrepare),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.installerPolicyForManifest),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(static_cast<bool>(inputs.installManifest), "invalid_rpc_update_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    const auto bundleRef = parseBundleRef(params);
    const bool safetyGatePassed =
        readBoolAlias(params, "safetyGatePassed", "safety_gate_passed");
    const bool deterministicTestsPassed = readBoolAlias(
        params,
        "deterministicTestsPassed",
        "deterministic_tests_passed");
    const bool sandboxBoundariesPassed = readBoolAlias(
        params,
        "sandboxBoundariesPassed",
        "sandbox_boundaries_passed");
    const auto manifest = fetchStoredManifestOrThrow(inputs, bundleRef.bundleId);

    std::string reason;
    if (!inputs.validateImplantDistributionManifest(
            manifest,
            safetyGatePassed,
            &reason)) {
        emitRuntimeSecurityEvent(
            inputs,
            "implant_policy_rejected",
            "implant.update.prepare",
            bundleRef.bundleIdHex,
            reason);
        throw std::runtime_error("implant_policy_rejected: " + reason);
    }

    if (!inputs.markImplantPrepare(
            bundleRef.bundleId,
            deterministicTestsPassed,
            sandboxBoundariesPassed,
            &reason)) {
        throw std::runtime_error("safety_pipeline_rejected: " + reason);
    }

    core::UpdateInstallerState installerState;
    const auto policy = inputs.installerPolicyForManifest(manifest);
    if (!inputs.installManifest(manifest, policy, &reason, &installerState)) {
        throw std::runtime_error("install_prepare_failed: " + reason);
    }

    RpcImplantUpdateActionResponseInputs out;
    out.installerState = installerState;
    out.reason = reason;
    out.bundleId = bundleRef.bundleIdHex;
    out.target = manifest.target;
    out.signerHex = crypto::toHex(manifest.signer);
    if (inputs.getImplantSafetyRecord) {
        out.safetyRecord = inputs.getImplantSafetyRecord(bundleRef.bundleId);
    }
    return buildRpcImplantUpdateActionResponse(out);
}

std::string runRpcImplantUpdateAdvance(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.validateImplantDistributionManifest),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.readInstallerState),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.markImplantCanaryHealth),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.markImplantWideHealth),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(static_cast<bool>(inputs.advanceRollout), "invalid_rpc_update_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    const auto bundleRef = parseBundleRef(params);
    const bool canaryHealthPassed =
        readBoolAlias(params, "canaryHealthPassed", "canary_health_passed");
    const bool wideHealthPassed =
        readBoolAlias(params, "wideHealthPassed", "wide_health_passed");
    const auto manifest = fetchStoredManifestOrThrow(inputs, bundleRef.bundleId);

    std::string reason;
    if (!inputs.validateImplantDistributionManifest(manifest, true, &reason)) {
        emitRuntimeSecurityEvent(
            inputs,
            "implant_policy_rejected",
            "implant.update.advance",
            bundleRef.bundleIdHex,
            reason);
        throw std::runtime_error("implant_policy_rejected: " + reason);
    }

    const auto installerBefore = inputs.readInstallerState();
    if (!installerBefore.hasPending) {
        throw std::runtime_error("install_advance_failed: no_pending_update");
    }
    if (installerBefore.pendingBundle != bundleRef.bundleId) {
        throw std::runtime_error("install_advance_failed: bundle_mismatch");
    }

    if (installerBefore.pendingStage == core::UpdateRolloutStage::CANARY) {
        if (!inputs.markImplantCanaryHealth(
                bundleRef.bundleId,
                canaryHealthPassed,
                &reason)) {
            throw std::runtime_error("safety_pipeline_rejected: " + reason);
        }
    } else if (installerBefore.pendingStage == core::UpdateRolloutStage::WIDE) {
        if (!inputs.markImplantWideHealth(bundleRef.bundleId, wideHealthPassed, &reason)) {
            throw std::runtime_error("safety_pipeline_rejected: " + reason);
        }
    } else {
        throw std::runtime_error("install_advance_failed: already_complete");
    }

    core::UpdateInstallerState installerState;
    if (!inputs.advanceRollout(bundleRef.bundleId, &reason, &installerState)) {
        throw std::runtime_error("install_advance_failed: " + reason);
    }

    RpcImplantUpdateActionResponseInputs out;
    out.installerState = installerState;
    out.reason = reason;
    out.bundleId = bundleRef.bundleIdHex;
    if (inputs.getImplantSafetyRecord) {
        out.safetyRecord = inputs.getImplantSafetyRecord(bundleRef.bundleId);
    }
    return buildRpcImplantUpdateActionResponse(out);
}

std::string runRpcImplantUpdateCommit(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.validateImplantDistributionManifest),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(
        static_cast<bool>(inputs.canCommitImplant),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(static_cast<bool>(inputs.commitPending), "invalid_rpc_update_runtime_inputs");

    const auto params = parseRpcParams(paramsJson);
    const auto bundleRef = parseBundleRef(params);
    const auto manifest = fetchStoredManifestOrThrow(inputs, bundleRef.bundleId);

    std::string reason;
    if (!inputs.validateImplantDistributionManifest(manifest, true, &reason)) {
        emitRuntimeSecurityEvent(
            inputs,
            "implant_policy_rejected",
            "implant.update.commit",
            bundleRef.bundleIdHex,
            reason);
        throw std::runtime_error("implant_policy_rejected: " + reason);
    }

    if (!inputs.canCommitImplant(bundleRef.bundleId, &reason)) {
        throw std::runtime_error("safety_pipeline_rejected: " + reason);
    }

    core::UpdateInstallerState installerState;
    if (!inputs.commitPending(bundleRef.bundleId, &reason, &installerState)) {
        throw std::runtime_error("install_commit_failed: " + reason);
    }

    RpcImplantUpdateActionResponseInputs out;
    out.installerState = installerState;
    out.reason = reason;
    out.bundleId = bundleRef.bundleIdHex;
    if (inputs.getImplantSafetyRecord) {
        out.safetyRecord = inputs.getImplantSafetyRecord(bundleRef.bundleId);
    }
    return buildRpcImplantUpdateActionResponse(out);
}

std::string runRpcImplantUpdateRollback(const RpcUpdateRuntimeInputs& inputs) {
    requireCallback(
        static_cast<bool>(inputs.readInstallerState),
        "invalid_rpc_update_runtime_inputs");
    requireCallback(static_cast<bool>(inputs.rollback), "invalid_rpc_update_runtime_inputs");

    crypto::Hash256 pendingBundle{};
    bool hadPending = false;
    const auto installerBefore = inputs.readInstallerState();
    if (installerBefore.hasPending) {
        hadPending = true;
        pendingBundle = installerBefore.pendingBundle;
    }

    std::string reason;
    core::UpdateInstallerState installerAfter;
    if (!inputs.rollback(&reason, &installerAfter)) {
        throw std::runtime_error("install_rollback_failed: " + reason);
    }

    if (hadPending && inputs.clearImplantSafetyRecord) {
        std::string clearReason;
        (void)inputs.clearImplantSafetyRecord(pendingBundle, &clearReason);
    }

    return buildRpcImplantUpdateActionResponse(
        {installerAfter, reason, "", std::nullopt, std::nullopt, std::nullopt});
}

} // namespace synapse::rpc
