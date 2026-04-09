#include "rpc/rpc_update_views.h"

namespace synapse::rpc {

namespace {

using json = nlohmann::json;

void applySecurityPolicyHashes(
    json& out,
    const RpcSecurityPolicyHashes& policyHashes) {
    out["policyHashNaan"] = policyHashes.naan;
    out["policyHashImplant"] = policyHashes.implant;
}

} // namespace

json updateManifestToJson(const core::UpdateManifest& manifest, bool includeChunks) {
    json out;
    out["version"] = manifest.version;
    out["bundleId"] = crypto::toHex(manifest.bundleId);
    out["contentHash"] = crypto::toHex(manifest.contentHash);
    out["target"] = manifest.target;
    out["protocolMin"] = manifest.protocolMin;
    out["protocolMax"] = manifest.protocolMax;
    out["signer"] = crypto::toHex(manifest.signer);
    out["signature"] = crypto::toHex(manifest.signature);
    out["chunkCount"] = manifest.chunks.size();
    if (includeChunks) {
        json chunks = json::array();
        for (const auto& chunk : manifest.chunks) {
            json item;
            item["hash"] = crypto::toHex(chunk.hash);
            item["size"] = chunk.size;
            chunks.push_back(std::move(item));
        }
        out["chunks"] = std::move(chunks);
    }
    return out;
}

json updateInstallerStateToJson(const core::UpdateInstallerState& state) {
    json out;
    out["activeSlot"] = std::string(1, state.activeSlot);
    out["hasSlotABundle"] = state.hasSlotABundle;
    out["slotABundle"] = state.hasSlotABundle ? crypto::toHex(state.slotABundle) : "";
    out["hasSlotBBundle"] = state.hasSlotBBundle;
    out["slotBBundle"] = state.hasSlotBBundle ? crypto::toHex(state.slotBBundle) : "";
    out["hasPending"] = state.hasPending;
    out["pendingSlot"] = state.hasPending ? std::string(1, state.pendingSlot) : "";
    out["pendingBundle"] = state.hasPending ? crypto::toHex(state.pendingBundle) : "";
    out["pendingStage"] = state.hasPending ? core::toString(state.pendingStage) : "";
    out["hasLastKnownGood"] = state.hasLastKnownGood;
    out["lastKnownGood"] = state.hasLastKnownGood ? crypto::toHex(state.lastKnownGood) : "";
    return out;
}

json implantSafetyRecordToJson(const core::ImplantSafetyRecord& record) {
    json out;
    out["deterministicTestsPassed"] = record.deterministicTestsPassed;
    out["sandboxBoundariesPassed"] = record.sandboxBoundariesPassed;
    out["canaryHealthPassed"] = record.canaryHealthPassed;
    out["wideHealthPassed"] = record.wideHealthPassed;
    out["updatedAt"] = record.updatedAt;
    return out;
}

std::string buildRpcUpdateInstallStateResponse(
    const RpcUpdateInstallStateResponseInputs& inputs) {
    json out = updateInstallerStateToJson(inputs.installerState);
    out["status"] = "ok";
    out["policy"] = inputs.policy;
    applySecurityPolicyHashes(out, inputs.policyHashes);

    if (!inputs.installerState.hasPending) {
        out["pendingSignerThresholdReady"] = false;
        out["pendingSignerThresholdReason"] = "no_pending_update";
        return out.dump();
    }

    if (inputs.includePendingApprovals) {
        out["pendingApprovals"] = inputs.pendingApprovals;
    }
    out["pendingSignerThresholdReady"] = inputs.pendingSignerThresholdReady;
    out["pendingSignerThresholdReason"] = inputs.pendingSignerThresholdReason;
    if (inputs.pendingManifest.has_value()) {
        out["pendingManifest"] = updateManifestToJson(*inputs.pendingManifest, false);
    }

    return out.dump();
}

std::string buildRpcUpdateInstallerActionResponse(
    const RpcUpdateInstallerActionResponseInputs& inputs) {
    json out = updateInstallerStateToJson(inputs.installerState);
    out["status"] = "ok";
    out["reason"] = inputs.reason;
    if (!inputs.bundleId.empty()) {
        out["bundleId"] = inputs.bundleId;
    }
    return out.dump();
}

std::string buildRpcImplantUpdateStateResponse(
    const RpcImplantUpdateStateResponseInputs& inputs) {
    json out;
    out["status"] = "ok";
    out["installer"] = updateInstallerStateToJson(inputs.installerState);
    out["policy"] = inputs.policy;
    applySecurityPolicyHashes(out, inputs.policyHashes);

    if (inputs.pendingManifest.has_value()) {
        out["pendingManifest"] = updateManifestToJson(*inputs.pendingManifest, false);
    }
    if (inputs.includePendingApprovals) {
        out["pendingApprovals"] = inputs.pendingApprovals;
    }
    if (inputs.includePendingSignerThreshold) {
        out["pendingSignerThresholdReady"] = inputs.pendingSignerThresholdReady;
        out["pendingSignerThresholdReason"] = inputs.pendingSignerThresholdReason;
    }
    if (inputs.safetyRecord.has_value()) {
        out["safetyRecord"] = implantSafetyRecordToJson(*inputs.safetyRecord);
    }
    if (inputs.includeSafetyCommit) {
        out["safetyCommitReady"] = inputs.safetyCommitReady;
        out["safetyCommitReason"] = inputs.safetyCommitReason;
    }

    return out.dump();
}

std::string buildRpcImplantUpdateActionResponse(
    const RpcImplantUpdateActionResponseInputs& inputs) {
    json out;
    out["status"] = "ok";
    out["reason"] = inputs.reason;
    if (!inputs.bundleId.empty()) {
        out["bundleId"] = inputs.bundleId;
    }
    if (inputs.target.has_value()) {
        out["target"] = *inputs.target;
    }
    if (inputs.signerHex.has_value()) {
        out["signer"] = *inputs.signerHex;
    }
    out["installer"] = updateInstallerStateToJson(inputs.installerState);
    if (inputs.safetyRecord.has_value()) {
        out["safetyRecord"] = implantSafetyRecordToJson(*inputs.safetyRecord);
    }
    return out.dump();
}

} // namespace synapse::rpc
