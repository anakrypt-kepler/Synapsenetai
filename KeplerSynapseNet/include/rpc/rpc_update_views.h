#pragma once

#include "core/implant_safety_pipeline.h"
#include "core/update_bundle.h"
#include "core/update_installer.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <optional>
#include <string>

namespace synapse::rpc {

nlohmann::json updateManifestToJson(
    const core::UpdateManifest& manifest,
    bool includeChunks = true);

nlohmann::json updateInstallerStateToJson(
    const core::UpdateInstallerState& state);

nlohmann::json implantSafetyRecordToJson(
    const core::ImplantSafetyRecord& record);

struct RpcSecurityPolicyHashes {
    std::string naan;
    std::string implant;
};

struct RpcUpdateInstallStateResponseInputs {
    core::UpdateInstallerState installerState;
    nlohmann::json policy = nlohmann::json::object();
    RpcSecurityPolicyHashes policyHashes;
    bool includePendingApprovals = false;
    nlohmann::json pendingApprovals = nlohmann::json::object();
    std::optional<core::UpdateManifest> pendingManifest;
    bool pendingSignerThresholdReady = false;
    std::string pendingSignerThresholdReason;
};

std::string buildRpcUpdateInstallStateResponse(
    const RpcUpdateInstallStateResponseInputs& inputs);

struct RpcUpdateInstallerActionResponseInputs {
    core::UpdateInstallerState installerState;
    std::string reason;
    std::string bundleId;
};

std::string buildRpcUpdateInstallerActionResponse(
    const RpcUpdateInstallerActionResponseInputs& inputs);

struct RpcImplantUpdateStateResponseInputs {
    core::UpdateInstallerState installerState;
    nlohmann::json policy = nlohmann::json::object();
    RpcSecurityPolicyHashes policyHashes;
    bool includePendingApprovals = false;
    nlohmann::json pendingApprovals = nlohmann::json::object();
    std::optional<core::UpdateManifest> pendingManifest;
    bool includePendingSignerThreshold = false;
    bool pendingSignerThresholdReady = false;
    std::string pendingSignerThresholdReason;
    std::optional<core::ImplantSafetyRecord> safetyRecord;
    bool includeSafetyCommit = false;
    bool safetyCommitReady = false;
    std::string safetyCommitReason;
};

std::string buildRpcImplantUpdateStateResponse(
    const RpcImplantUpdateStateResponseInputs& inputs);

struct RpcImplantUpdateActionResponseInputs {
    core::UpdateInstallerState installerState;
    std::string reason;
    std::string bundleId;
    std::optional<std::string> target;
    std::optional<std::string> signerHex;
    std::optional<core::ImplantSafetyRecord> safetyRecord;
};

std::string buildRpcImplantUpdateActionResponse(
    const RpcImplantUpdateActionResponseInputs& inputs);

} // namespace synapse::rpc
